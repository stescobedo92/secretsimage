# Reproducible test plan

All secrets in every test are **fake but format-real**: credit-card
numbers carry a valid Luhn digit, IBANs a valid mod-97 check pair, tokens
the real vendor lengths. The corpus is regenerated deterministically from
a seed by `gen_synthetic`, so every run sees the same 60 secret + 20
clean screenshots unless parameters say otherwise.

## PR-3 — Human review

Goal: catch what automation cannot — secrets still readable to a person,
and non-sensitive text needlessly covered.

1. Generate the corpus:
   `gen_synthetic --out corpus --secret-cases 60 --clean-cases 20 --seed 20261105`
2. Redact it:
   `secretsimage redact corpus/*.png --output-dir corpus-redacted`
3. A reviewer opens each original/redacted pair side by side at **200%
   zoom** and records, per image:
   - any secret (from `corpus/manifest.json`) still legible, even
     partially, after redaction;
   - any non-sensitive text that was covered.
4. Acceptance: zero legible secrets; covered clean text within the
   over-redaction budget below. File every miss as an issue with the case
   name (e.g. `secret-17`) and the OCR text — the case is reproducible
   from the seed.

## PR-4 — Automated matrix

The maintained suite runs under CTest:

| CTest entry | Label | What it proves |
|---|---|---|
| `unit` | `unit` | Detector formats and validators, solid-fill invariants, audit log never leaks values, and the metric gate below at ground-truth OCR level across font sizes 12/18 px and light/dark themes, JPEG q60/q80/q95 geometry. |
| `ocr_e2e` | `ocr`, `requires-ocr` | Real Tesseract at 14 px and 18 px: redact, OCR again, require **zero** recoverable secrets, every painted region a single solid color, and ≥ 90% ground-truth ink coverage. Skips itself when Tesseract is absent (reported as skipped, not passed). |

```sh
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L ocr  --output-on-failure
```

## Metrics

| Metric | Definition | Target |
|---|---|---|
| Secret recall | ground-truth secrets whose pixels are fully solid-filled / total | report |
| Token/key recall | same, restricted to token & key kinds | ≥ 99% |
| Residual leak | secrets recoverable by OCR after redaction | 0 |
| Over-redaction | clean words covered / clean words total | ≤ 5% |
| Latency p95 | per image, end to end | ≤ 2 s |
| Field signal | images with secrets found in the monthly attachment review | trending down |

### Last measured run

Environment: Windows 11 (MinGW GCC 13.2) for unit metrics; WSL Ubuntu
(GCC 15.2, Tesseract 5.5.0) for the real-OCR pass. Corpus seed `20261105`
(metrics), `99` (OCR e2e), 60 + 20 cases as specified.

- Ground-truth level: recall 120/120, token/key recall 76/76,
  over-redaction 0/3774 clean words, pipeline p95 ≈ 0.6 ms (OCR excluded).
- Real OCR level (16 secret cases × 14 px and 18 px): ground-truth ink
  coverage 32/32, residual findings on a second OCR pass: 0. A full
  `scan → redact → scan` CLI round-trip exits 2, then 0; the 4 clean
  cases in that run produced zero findings, and the full 20-case clean
  corpus is covered at ground-truth level above.
- Windows with Tesseract 5.4 installed: the OCR e2e test also reports
  32/32 coverage with residual 0; a full CLI pass over the 60-case secret
  corpus detects 60/60 and, after redaction, re-scans with residual 0/60
  (scan p95 ≈ 0.54 s per image). Clipboard watch was verified end to end:
  an image carrying an AWS key and an e-mail address placed by another
  process was intercepted, redacted in 373 ms, logged with the foreground
  application, and the clipboard afterwards contained the redacted image.

Re-run before every release and update this section with the date and
environment; a metric that regresses below target blocks the release.
