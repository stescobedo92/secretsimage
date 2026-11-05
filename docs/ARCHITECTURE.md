# Architecture

## Design principles

1. **Fail closed.** If OCR or redaction fails, the image is left untouched
   and the failure is surfaced (exit code / watch event / audit log). The
   tool never silently passes an unprocessed image off as clean.
2. **Secrets never leave the image.** Findings and audit logs carry only a
   kind, a bounding box and a length — never the matched text. Temporary
   OCR files are created by the tool, in the system temp directory, and
   deleted immediately.
3. **Solid fill only.** Blur and pixelation are partially reversible and
   are never used. Re-encoding from raw pixels also drops all input
   metadata (EXIF, text chunks) by construction.
4. **Minimal dependencies.** C++23 standard library plus vendored
   public-domain stb headers for PNG/JPEG/BMP. Tesseract runs as an
   external process behind an interface, so no OCR library is linked.

## Components

```
apps/cli            secretsimage redact | scan | watch
apps/gen_synthetic  deterministic test-corpus generator (PR-3/PR-4)

secretsimage::core (include/secretsimage, src/)
  Image             RGBA8 buffer, stb-backed decode/encode, solid fill
  IOcrEngine        OCR abstraction
    TesseractCliOcrEngine   runs `tesseract ... tsv`, parses word boxes
  detect_secrets    pattern + checksum detection over reconstructed lines
  redact_image      padded, merged, irreversible solid fill
  Pipeline          multi-scale OCR -> detect -> fill; file mode + audit
  AuditRecord       JSON proof of what was covered (no secret values)
  IClipboard        per-OS clipboard (Win32 / NSPasteboard / X11)
  ClipboardWatcher  poll -> fingerprint -> redact -> replace -> log
  Sha256            image fingerprints for the log and loop prevention
```

## Clipboard interception model

There is no supported API to hook "paste" inside Word, Slack, a terminal
and every other application individually. `secretsimage` instead
intercepts at the one place all of them share: **the clipboard itself**.

`watch` mode polls the OS change counter (`GetClipboardSequenceNumber` on
Windows, `NSPasteboard.changeCount` on macOS, image fingerprinting on
X11). When a new image appears it is redacted and written back within a
few hundred milliseconds, so any subsequent paste — into any application —
delivers the redacted version. The tool recognizes its own write by
SHA-256 fingerprint and never reprocesses it.

For the audit trail the watcher also asks the OS which application is in
the foreground (process image name + window title on Windows,
`NSWorkspace.frontmostApplication` on macOS, `_NET_ACTIVE_WINDOW` /
`WM_CLASS` on X11). This is best-effort metadata for the log line, e.g.
`REDACTED ... [app: WINWORD.EXE]`; the redaction decision never depends
on it.

Known limits, by physics rather than by design: polling means an
application that reads the clipboard within the poll interval (default
200 ms) after a screenshot could theoretically see the original. The
watcher is a strong seatbelt, not a kernel-level DLP agent; see
[SECURITY.md](../SECURITY.md).

## Detection

OCR words arrive as `(text, box, block, paragraph, line)`. The detector
rebuilds each line, then runs two families of checks:

- **Format patterns** (bounded `std::regex` with cheap prefilters):
  private-key headers, AWS/GCP/GitHub/Slack token formats, JWTs,
  `Bearer` values, credentialed URIs, labelled assignments, e-mail.
- **Validated patterns**: credit cards must pass Luhn, IBANs ISO 13616
  mod-97, IPv4 octets are range-checked and classified private/public,
  SSNs must have a valid area/group/serial structure, generic tokens must
  pass a Shannon-entropy and character-class gate that excludes git SHAs
  and UUIDs.

OCR noise measured against Tesseract 5.5 drove three robustness layers,
each covered by tests:

- `=` read as `-` in `Password-...` is accepted only when the value does
  not look like an ordinary word, so "password-protected" stays untouched.
- Matches are snapped to whole words and grown across adjacent
  token-like fragments, because OCR splits long tokens with spaces;
  vendor-prefixed tokens additionally get single-space-removal variants.
- The pipeline runs OCR at native scale **and** 2x upscaled, then merges
  findings — each scale recovers text the other mangles.

A line with at least two semicolons, key-value fragments and a
password-like key is treated as a connection string and covered whole.

## Redaction and audit

Findings are padded (default 4 px), merged, clipped and painted with one
solid color; the exact painted boxes are returned and logged. The audit
record (`*.audit.json`, or JSON lines in watch mode) contains the tool
version, UTC timestamp, source (`file`/`clipboard`), file *names* only
(no directories), the foreground application, image dimensions, SHA-256
of input and output bytes, elapsed time, and the findings/painted boxes.

## Platform notes

| Platform | Clipboard read | Clipboard write | Foreground app |
|---|---|---|---|
| Windows | `PNG` format, `CF_DIB(V5)`, `CF_BITMAP` via `GetDIBits` | `CF_DIB` + `PNG` | process image + window title |
| macOS | `NSPasteboard` PNG, TIFF normalized via `NSImage` | PNG | localized name (no title: needs Accessibility) |
| Linux/X11 | `CLIPBOARD` selection, `image/png` target | selection ownership, serves `SelectionRequest` | `WM_CLASS` + `_NET_WM_NAME` |

Native Wayland sessions are not yet supported for watch mode (the
protocol has no global clipboard snooping by design); run under XWayland
or use file/CI mode. Very large X11 selections using the `INCR` protocol
report a clear error instead of truncating.
