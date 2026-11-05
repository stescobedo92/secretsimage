# Usage

## Install dependencies

Tesseract OCR is the only runtime dependency.

| OS | Command |
|---|---|
| Debian/Ubuntu | `sudo apt install tesseract-ocr libx11-dev` |
| Fedora | `sudo dnf install tesseract libX11-devel` |
| macOS | `brew install tesseract` |
| Windows | `choco install tesseract` (or the UB Mannheim installer) |

If `tesseract` is not on `PATH`, point `SECRETSIMAGE_TESSERACT` at the
executable. Build requirements: CMake 3.20+ and a C++23 compiler
(GCC 13+, Clang 16+, MSVC from Visual Studio 2022, Apple Clang 15+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build   # optional
```

## Watch mode (paste interception)

```sh
secretsimage watch --audit-log ~/.local/state/secretsimage/audit.jsonl
```

Run it from your session startup (Task Scheduler / Startup folder on
Windows, a systemd user service on Linux, a LaunchAgent on macOS). Then
take screenshots and copy images as usual: whatever you paste — into a
terminal, Word, Excel, PowerPoint, Teams, Slack, Jira in the browser, a
pull request — is already redacted. Each event prints one line, for
example:

```
REDACTED clipboard image 1920x1080: 2 finding(s): aws_access_key, email (412 ms) [app: WINWORD.EXE]
clean image 800x600 left on clipboard (180 ms) [app: Code]
ERROR: clipboard poll failed: tesseract executable was not found ...
```

Options: `--poll-ms 200` (clipboard poll interval), `--timeout <seconds>`
(stop automatically; 0 = until Ctrl+C), plus the shared detection flags
below. On native Wayland, watch mode is unavailable (see
[ARCHITECTURE.md](ARCHITECTURE.md)); use XWayland or file mode.

## File mode

```sh
secretsimage redact screenshot.png -o screenshot.redacted.png
secretsimage redact attachments/*.png --output-dir redacted/
```

Writes `<output>.audit.json` next to each output unless `--report` says
otherwise. The input file is never modified, and output must not equal
input.

## CI mode

`scan` exits 2 when any image contains secrets:

```yaml
- name: No secrets in documentation screenshots
  run: |
    sudo apt-get install -y tesseract-ocr
    ./secretsimage scan docs/images/*.png
```

To *fix* instead of fail, run `redact --output-dir` in the job and upload
the redacted images and audit reports as artifacts; add
`--fail-on-secret` if the job should also go red when something was found.

## Shared options

| Flag | Meaning |
|---|---|
| `--ip-mode private\|all\|off` | Which IPv4 addresses are sensitive (default `private`: RFC 1918, loopback, link-local, CGNAT). |
| `--padding <px>` | Extra solid-fill margin around findings (default 4). |
| `--no-high-entropy` | Disable the generic high-entropy token rule. |
| `--ocr-upscale <n>` | Also run OCR at this upscale factor (default 2; 1 = native only). |

## Troubleshooting

- **"tesseract executable was not found"** — install Tesseract or set
  `SECRETSIMAGE_TESSERACT`. In watch mode the clipboard is left untouched
  (fail-closed) and the error is printed once per new image.
- **Nothing happens in watch mode on Linux** — check you are on X11 or
  XWayland (`echo $XDG_SESSION_TYPE`) and that the build found
  `libx11-dev`.
- **A secret was missed** — OCR is probabilistic. Treat the tool as one
  layer: keep server-side scanning of attachments and rotate anything
  that leaked. Small or low-contrast text is hardest; `--ocr-upscale 3`
  sometimes helps.
