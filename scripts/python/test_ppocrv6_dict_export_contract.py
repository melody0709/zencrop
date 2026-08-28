#!/usr/bin/env python3
"""
Contract test: PP-OCRv6 recognition dictionary export.

Validates small/medium:
  - YAML character_dict length 18708
  - exported TXT matches YAML char-for-char
  - U+3000 preserved at index 1748
  - manifest v2 CTC contract fields
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML required: pip install pyyaml") from exc

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_BASE = 18708
IDEOGRAPHIC_INDEX = 1748
IDEOGRAPHIC = "　"


def check_variant(variant: str) -> list[str]:
    errors: list[str] = []
    rec = ROOT / "ocr" / "pp-ocrv6" / variant / "rec"
    yml = rec / "inference.yml"
    txt = rec / "ppocrv6_rec_dict.txt"
    man = rec / "manifest.json"

    if not yml.is_file():
        return [f"{variant}: missing {yml}"]
    if not txt.is_file():
        return [f"{variant}: missing {txt}"]
    if not man.is_file():
        return [f"{variant}: missing {man}"]

    cfg = yaml.safe_load(yml.read_text(encoding="utf-8"))
    chars = cfg["PostProcess"]["character_dict"]
    if not isinstance(chars, list):
        errors.append(f"{variant}: character_dict is not a list")
        return errors
    if len(chars) != EXPECTED_BASE:
        errors.append(f"{variant}: yaml size {len(chars)} != {EXPECTED_BASE}")
    if chars[IDEOGRAPHIC_INDEX] != IDEOGRAPHIC:
        errors.append(
            f"{variant}: yaml[{IDEOGRAPHIC_INDEX}]={chars[IDEOGRAPHIC_INDEX]!r} != U+3000"
        )

    txt_chars = txt.read_text(encoding="utf-8").splitlines()
    if len(txt_chars) != EXPECTED_BASE:
        errors.append(f"{variant}: txt size {len(txt_chars)} != {EXPECTED_BASE}")
    if len(txt_chars) == len(chars):
        diffs = [i for i, (a, b) in enumerate(zip(chars, txt_chars)) if a != b]
        if diffs:
            i = diffs[0]
            errors.append(
                f"{variant}: diff_count={len(diffs)} first idx={i} "
                f"yaml={chars[i]!r} txt={txt_chars[i]!r}"
            )
    if len(txt_chars) > IDEOGRAPHIC_INDEX and txt_chars[IDEOGRAPHIC_INDEX] != IDEOGRAPHIC:
        errors.append(
            f"{variant}: txt[{IDEOGRAPHIC_INDEX}]={txt_chars[IDEOGRAPHIC_INDEX]!r} != U+3000"
        )

    manifest = json.loads(man.read_text(encoding="utf-8"))
    if manifest.get("manifest_version") != 2:
        errors.append(f"{variant}: manifest_version != 2")
    if manifest.get("base_dict_size") != EXPECTED_BASE:
        errors.append(f"{variant}: base_dict_size mismatch")
    if manifest.get("append_space") is not True:
        errors.append(f"{variant}: append_space must be true")
    if manifest.get("effective_dict_size") != EXPECTED_BASE + 1:
        errors.append(f"{variant}: effective_dict_size mismatch")
    if manifest.get("expected_output_classes") != EXPECTED_BASE + 2:
        errors.append(f"{variant}: expected_output_classes mismatch")
    if manifest.get("blank_index") != 0:
        errors.append(f"{variant}: blank_index must be 0")
    # Legacy field is base size only.
    if manifest.get("dict_size") != EXPECTED_BASE:
        errors.append(f"{variant}: legacy dict_size must equal base")
    # Must come from PreProcess.transform_ops.RecResizeImg.image_shape, not silent default.
    if manifest.get("rec_image_shape") != [3, 48, 320]:
        errors.append(
            f"{variant}: rec_image_shape={manifest.get('rec_image_shape')!r} "
            "!= [3, 48, 320] (transform_ops path)"
        )

    return errors


def main() -> int:
    all_errors: list[str] = []
    for variant in ("small", "medium"):
        errs = check_variant(variant)
        if errs:
            all_errors.extend(errs)
            print(f"FAIL {variant}")
            for e in errs:
                print(f"  {e}")
        else:
            print(f"PASS {variant}: YAML/TXT/manifest CTC contract OK")

    if all_errors:
        print(f"\n{len(all_errors)} failure(s)")
        return 1
    print("\nALL PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
