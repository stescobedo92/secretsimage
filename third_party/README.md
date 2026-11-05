# Vendored third-party code

Everything under this directory is third-party code kept in-tree so the
project builds offline with no package manager. First-party code never
includes these headers directly except where noted.

| Component | Location | License | Notes |
|---|---|---|---|
| stb_image, stb_image_write, stb_truetype | `stb/` | Public domain / MIT (see file headers) | Single-header libraries by Sean Barrett et al., fetched from the `nothings/stb` repository. `stb_truetype` is used only by the synthetic corpus renderer. |
| font8x8 (basic) | `../tools/synthetic/font8x8_basic.h` | Public domain | By Daniel Hepper / Marcel Sondaar. Local patch: the glyph array type was changed from `char` to `unsigned char` so 0x80-0xFF bytes are not a narrowing conversion under MSVC/GCC. |
| DejaVu Sans Mono | `../tools/synthetic/fonts/` | Bitstream Vera / public domain (see `DejaVu-LICENSE.txt`) | Runtime input for the synthetic corpus renderer only; never linked into the application. |

The OCR engine (Tesseract) is intentionally **not** vendored: it is invoked
as an external program, which keeps this GPL-3.0 application free of
additional linked dependencies and isolates OCR crashes.
