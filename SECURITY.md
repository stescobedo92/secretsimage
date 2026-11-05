# Security policy

## Reporting a vulnerability

Please report security issues privately to the repository maintainer
instead of opening a public issue. Include a reproduction (for detector
misses, a synthetic case name or a fake-secret screenshot is ideal —
never send real credentials).

## What this tool does and does not guarantee

`secretsimage` reduces accidental disclosure of secrets in screenshots.
It is one layer of defense, not a guarantee:

- OCR is probabilistic. Very small, rotated, low-contrast or heavily
  compressed text can be missed. Keep server-side controls (attachment
  scanning, secret rotation on exposure) in place.
- Watch mode polls the clipboard (default 200 ms). A program reading the
  clipboard in that window right after a screenshot could see the
  original image. Kernel/EDR-grade DLP is out of scope.
- Redaction is irreversible solid fill on the pixels this tool writes,
  and re-encoding drops input metadata. Images that never passed through
  the tool are obviously unaffected.
- Audit logs never contain secret values, matched text, or paths beyond
  file names. Temporary OCR images live in the OS temp directory for the
  duration of one OCR run and are deleted immediately; on shared machines
  treat the temp directory with the usual care.

## Supported versions

The latest release on the default branch. Secrets that leaked despite
the tool must still be rotated — redaction after the fact cannot unpublish
an image.
