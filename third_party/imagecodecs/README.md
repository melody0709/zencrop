# Image codec runtime assets

- WebP: libwebp 1.6.0 headers/static library from https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0-windows-x64.zip
- AVIF: libavif 1.4.2 windows-artifacts avifenc/avifdec from https://github.com/AOMediaCodec/libavif/releases/tag/v1.4.2

ZenCrop links libwebp directly for WebP encode/decode and shells out to bundled avifenc/avifdec for AVIF encode/decode.
