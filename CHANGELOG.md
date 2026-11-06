# Changelog

All notable changes to this project are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.1] - 2026-11-06

### Changed

- Windows executable now carries complete VERSIONINFO metadata (product,
  company, file/product version, description, copyright) generated from
  the project version alongside the embedded icon. Unsigned binaries with
  blank version metadata are a classic Microsoft Defender ML
  false-positive trigger; the 0.1.0 package was flagged this way during
  winget Installation Validation even though signature scans are clean.
- End-user release archives no longer bundle `gen_synthetic`. It is a
  test-corpus development tool that embeds fake credential strings by
  design; it remains available in per-commit CI artifacts and from source
  builds (`SECRETSIMAGE_BUILD_TOOLS`).

## [0.1.0] - 2026-11-05

### Added

- C++23 core library: PNG/JPEG/BMP image handling, multi-scale OCR via an
  external Tesseract process, and a secret/PII detector (vendor tokens,
  JWTs, connection strings, labelled assignments, e-mail, private IPs,
  IPv6, Luhn-validated cards, mod-97-validated IBANs, SSNs, high-entropy
  tokens) with OCR-noise tolerance for split tokens and `=` misread as `-`.
- Irreversible solid-fill redactor with padded, merged boxes, and an
  audit log (JSON) that records kinds and locations but never values.
- Clipboard watch mode for Windows (Win32), macOS (NSPasteboard) and
  Linux/X11, with foreground-application identification for the log.
- CLI modes `redact` (file/CI), `scan` (CI gate, exit code 2) and `watch`.
- Deterministic synthetic corpus generator and metrics suite covering the
  PR-3 human-review corpus and the PR-4 automated matrix.
- GitHub Actions pipelines: build + test matrix (Linux, Windows, macOS)
  and a SemVer-tag release workflow with checksummed artifacts.
