# Brand assets

| File | Used for |
|---|---|
| `icon.svg` | Editable vector source of the app icon. |
| `icon.png` (512×512) | README header, Linux hicolor icon, release packages. |
| `icon-256.png` | Smaller README/web use. |
| `secretsimage.ico` | Embedded in `secretsimage.exe` via `apps/cli/secretsimage.rc` (16–256 px). |
| `secretsimage.icns` | macOS icon, installed with the app. |
| `secretsimage.desktop` | Linux desktop entry (`secretsimage watch` in a terminal). |

Regenerate the raster icons after editing the artwork:

```sh
python tools/assets/generate_icon.py
```

The icon shows a screenshot window crossed by a solid black redaction
bar — the one operation this tool exists to do.
