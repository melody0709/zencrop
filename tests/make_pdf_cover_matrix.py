import pathlib
import sys

import fitz


def add_page(
    document: fitz.Document,
    width: float,
    height: float,
    label: str,
    rotation: int = 0,
) -> None:
    page = document.new_page(width=width, height=height)
    page.insert_text((36, 54), label, fontsize=18)
    page.draw_rect(fitz.Rect(32, 72, min(width - 32, 260), min(height - 32, 180)))
    if rotation:
        page.set_rotation(rotation)


def save_fixture(path: pathlib.Path, pages: list[tuple[float, float, str, int]]) -> None:
    if path.exists():
        path.unlink()
    document = fitz.open()
    for width, height, label, rotation in pages:
        add_page(document, width, height, label, rotation)
    document.save(path, garbage=4, deflate=True)
    document.close()


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: make_pdf_cover_matrix.py output-dir", file=sys.stderr)
        return 2

    output_dir = pathlib.Path(sys.argv[1]).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    portrait = (612.0, 792.0)
    landscape = (792.0, 612.0)
    save_fixture(
        output_dir / "single_portrait.pdf",
        [(portrait[0], portrait[1], "single portrait page", 0)],
    )
    save_fixture(
        output_dir / "multi_portrait.pdf",
        [
            (portrait[0], portrait[1], "multi portrait page 1", 0),
            (portrait[0], portrait[1], "multi portrait page 2", 0),
            (portrait[0], portrait[1], "multi portrait page 3", 0),
        ],
    )
    save_fixture(
        output_dir / "landscape.pdf",
        [(landscape[0], landscape[1], "landscape page", 0)],
    )
    save_fixture(
        output_dir / "rotated_90.pdf",
        [(portrait[0], portrait[1], "rotated page", 90)],
    )
    save_fixture(
        output_dir / "long_101_pages.pdf",
        [
            (288.0, 360.0, f"long document page {page_index}", 0)
            for page_index in range(1, 102)
        ],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
