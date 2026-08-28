import sys

import fitz


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: make_encrypted_pdf.py input.pdf output.pdf password", file=sys.stderr)
        return 2

    input_pdf, output_pdf, password = sys.argv[1], sys.argv[2], sys.argv[3]
    doc = fitz.open(input_pdf)
    doc.save(
        output_pdf,
        encryption=fitz.PDF_ENCRYPT_AES_128,
        owner_pw=password,
        user_pw=password,
        permissions=0,
    )
    doc.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
