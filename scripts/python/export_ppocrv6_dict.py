#!/usr/bin/env python3
"""
Export PP-OCRv6 recognition dictionary from inference.yml.

Usage:
  python scripts/python/export_ppocrv6_dict.py ocr/pp-ocrv6/small/rec/inference.yml
  python scripts/python/export_ppocrv6_dict.py ocr/pp-ocrv6/medium/rec/inference.yml

Writes (next to the YAML):
  - ppocrv6_rec_dict.txt  : base dictionary only (one character per line, UTF-8)
  - manifest.json         : base/effective sizes and CTC contract

Official CTCLabelDecode (use_space_char=true) appends ASCII space at runtime:
  base dict size:          18708
  append ASCII space:      +1
  effective dict size:     18709
  blank index:             0
  expected output classes: 18710
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "PyYAML is required. Install with: pip install pyyaml"
    ) from exc

# PP-OCRv6 small/medium rec contract (official ONNX export).
EXPECTED_BASE_DICT_SIZE = 18708
IDEOGRAPHIC_SPACE_INDEX = 1748
IDEOGRAPHIC_SPACE = "　"


def export_dict(yml_path: Path) -> None:
    raw = yml_path.read_text(encoding="utf-8")
    config = yaml.safe_load(raw)
    if not isinstance(config, dict):
        raise SystemExit(f"Invalid YAML root in {yml_path}")

    post = config.get("PostProcess")
    if not isinstance(post, dict):
        raise SystemExit(f"Missing PostProcess in {yml_path}")

    chars = post.get("character_dict")
    if not isinstance(chars, list) or not chars:
        raise SystemExit(f"No PostProcess.character_dict list in {yml_path}")

    cleaned: list[str] = []
    for i, item in enumerate(chars):
        if item is None:
            raise SystemExit(f"character_dict[{i}] is null in {yml_path}")
        if not isinstance(item, str):
            raise SystemExit(
                f"character_dict[{i}] is {type(item).__name__}, expected str"
            )
        if len(item) != 1:
            raise SystemExit(
                f"character_dict[{i}] must be a single character, got {item!r}"
            )
        cleaned.append(item)

    if len(cleaned) != EXPECTED_BASE_DICT_SIZE:
        raise SystemExit(
            f"Unexpected base dict size {len(cleaned)} in {yml_path}; "
            f"expected {EXPECTED_BASE_DICT_SIZE}"
        )

    if cleaned[IDEOGRAPHIC_SPACE_INDEX] != IDEOGRAPHIC_SPACE:
        raise SystemExit(
            f"character_dict[{IDEOGRAPHIC_SPACE_INDEX}] must be U+3000, "
            f"got {cleaned[IDEOGRAPHIC_SPACE_INDEX]!r}"
        )

    global_cfg = config.get("Global") if isinstance(config.get("Global"), dict) else {}
    model_name = str(global_cfg.get("model_name") or "")
    if not model_name:
        # Some exports put model_name at top level.
        model_name = str(config.get("model_name") or yml_path.parent.parent.name)

    decode_type = str(post.get("name") or "CTCLabelDecode")

    pre = config.get("PreProcess") if isinstance(config.get("PreProcess"), dict) else {}
    # Official PP-OCRv6 ONNX export uses PreProcess.transform_ops (not transforms).
    # Layout: PreProcess.transform_ops[*].RecResizeImg.image_shape = [3, 48, 320]
    rec_shape: list[int] = []
    ops = None
    if isinstance(pre, dict):
        if isinstance(pre.get("transform_ops"), list):
            ops = pre["transform_ops"]
        elif isinstance(pre.get("transforms"), list):
            # Legacy / alternate layout — still accept, but prefer transform_ops.
            ops = pre["transforms"]
    if isinstance(ops, list):
        for step in ops:
            if not isinstance(step, dict):
                continue
            # Prefer the RecResizeImg op explicitly.
            rec_cfg = step.get("RecResizeImg")
            if isinstance(rec_cfg, dict):
                shape = rec_cfg.get("image_shape")
                if isinstance(shape, list) and len(shape) == 3:
                    try:
                        rec_shape = [int(x) for x in shape]
                    except (TypeError, ValueError):
                        rec_shape = []
                    if rec_shape:
                        break
            # Fallback: any op that carries image_shape.
            if not rec_shape:
                for _key, val in step.items():
                    if not isinstance(val, dict):
                        continue
                    shape = val.get("image_shape")
                    if isinstance(shape, list) and len(shape) == 3:
                        try:
                            rec_shape = [int(x) for x in shape]
                        except (TypeError, ValueError):
                            rec_shape = []
                        if rec_shape:
                            break
            if rec_shape:
                break
    if not rec_shape:
        raise SystemExit(
            f"Missing RecResizeImg.image_shape under PreProcess.transform_ops in {yml_path}. "
            "Refusing silent default [3,48,320] — fix the YAML or exporter field path."
        )
    if rec_shape != [3, 48, 320]:
        # PP-OCRv6 small/medium contract; still write but surface loudly.
        print(
            f"WARNING: rec_image_shape={rec_shape} (expected [3, 48, 320] for PP-OCRv6 small/medium)",
            file=sys.stderr,
        )

    append_space = True
    base_size = len(cleaned)
    effective_size = base_size + (1 if append_space else 0)
    expected_classes = effective_size + 1  # blank

    out_dir = yml_path.parent
    dict_path = out_dir / "ppocrv6_rec_dict.txt"
    manifest_path = out_dir / "manifest.json"

    # Base dictionary only — runtime appends ASCII space (use_space_char=true).
    # Do not write a lone space line into the TXT; editors/trim tools corrupt it.
    dict_path.write_text("\n".join(cleaned) + "\n", encoding="utf-8", newline="\n")

    manifest = {
        "manifest_version": 2,
        "model_name": model_name,
        "rec_image_shape": rec_shape,
        "decode_type": decode_type,
        "base_dict_size": base_size,
        "append_space": append_space,
        "effective_dict_size": effective_size,
        "blank_index": 0,
        "expected_output_classes": expected_classes,
        # Legacy field: base size only (not including runtime ASCII space).
        "dict_size": base_size,
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    # Round-trip sanity: re-read TXT and compare to YAML chars byte-for-byte.
    roundtrip = dict_path.read_text(encoding="utf-8").splitlines()
    if roundtrip != cleaned:
        diffs = [i for i, (a, b) in enumerate(zip(roundtrip, cleaned)) if a != b]
        raise SystemExit(
            f"Round-trip dict mismatch for {dict_path}; first diffs={diffs[:5]}"
        )
    if roundtrip[IDEOGRAPHIC_SPACE_INDEX] != IDEOGRAPHIC_SPACE:
        raise SystemExit(
            f"U+3000 lost at index {IDEOGRAPHIC_SPACE_INDEX} in {dict_path}"
        )

    print(f"Wrote {dict_path} ({base_size} base chars)")
    print(f"Wrote {manifest_path}")
    print(
        f"CTC contract: base={base_size} append_space={append_space} "
        f"effective={effective_size} classes={expected_classes}"
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    export_dict(Path(sys.argv[1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
