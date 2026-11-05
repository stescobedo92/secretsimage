#!/usr/bin/env python3
"""Generate the README before/after screenshot with the REAL app.

Renders a synthetic screenshot containing only fake, well-known example
secrets (the AWS documentation key AKIAIOSFODNN7EXAMPLE, a sequential
ghp_ token, an @example.com address), runs the built `secretsimage`
binary on it, verifies the redaction with `scan`, and composes a labelled
side-by-side image. Nothing here is hand-redacted: if the app stops
detecting these, this script fails instead of shipping a fake picture.

    python tools/assets/generate_screenshots.py [path/to/secretsimage[.exe]]
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "images"
W, H = 1280, 760

# Fake example secrets, assembled from fragments so secret scanners (and
# redacting tooling) do not rewrite this file: the AWS documentation example
# key and a sequential, obviously-fake GitHub token payload.
FAKE_AWS_KEY = "AKIA" + "IOSFODNN7" + "EXAMPLE"
FAKE_AWS_SECRET = "wJalrXUtnFEMI/" + "K7MDENG/" + "bPxRfiCY" + "EXAMPLEKEY"
FAKE_GH_TOKEN = "ghp_" + "0123456789" + "abcdefghijklmnopqrstuvwxyz"

LINES = [
    ("# deploy.env  --  fake example data, safe to publish", (100, 116, 139)),
    ("", None),
    (f"AWS_ACCESS_KEY_ID = {FAKE_AWS_KEY}", (15, 23, 42)),
    (f"AWS_SECRET_ACCESS_KEY = {FAKE_AWS_SECRET}", (15, 23, 42)),
    (f"GITHUB_TOKEN = {FAKE_GH_TOKEN}", (15, 23, 42)),
    ("password = Sunflower#42", (15, 23, 42)),
    ("", None),
    ("contact: maria.garcia@example.com", (15, 23, 42)),
    ("db_host: 192.168.10.25", (15, 23, 42)),
    ("", None),
    ("region: eu-west-1        build: 1.4.2   status: OK", (15, 23, 42)),
]


def font(size: int, mono: bool = True) -> ImageFont.FreeTypeFont:
    # Mono: the bundled DejaVu Sans Mono first - it is the font the project's
    # OCR end-to-end corpus is rendered with, so Tesseract reads it reliably
    # (e.g. Consolas made it misread AKIA.../ghp_ tokens in this very image).
    candidates = (
        [str(ROOT / "tools" / "synthetic" / "fonts" / "DejaVuSansMono.ttf"),
         "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
         "C:/Windows/Fonts/consola.ttf", "/System/Library/Fonts/Menlo.ttc"]
        if mono else
        ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/segoeui.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/System/Library/Fonts/Helvetica.ttc"]
    )
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    bundled = ROOT / "tools" / "synthetic" / "fonts" / "DejaVuSansMono.ttf"
    return ImageFont.truetype(str(bundled), size)


def render_before() -> Path:
    img = Image.new("RGB", (W, H), (241, 245, 249))
    d = ImageDraw.Draw(img)
    # Window chrome.
    d.rounded_rectangle([24, 24, W - 24, H - 24], radius=18, fill=(255, 255, 255),
                        outline=(148, 163, 184), width=2)
    d.rounded_rectangle([24, 24, W - 24, 96], radius=18, fill=(226, 232, 240))
    d.rectangle([24, 60, W - 24, 96], fill=(226, 232, 240))
    for i, color in enumerate([(220, 38, 38), (245, 197, 66), (34, 197, 94)]):
        cx = 64 + i * 36
        d.ellipse([cx - 10, 50, cx + 10, 70], fill=color)
    d.text((185, 47), "deploy.env  -  screenshot about to be pasted into a ticket",
           font=font(24, mono=False), fill=(51, 65, 85))
    # Line numbers + content.
    body, num = font(27), font(22)
    y = 128
    for i, (text, color) in enumerate(LINES, start=1):
        d.text((52, y + 3), f"{i:>2}", font=num, fill=(148, 163, 184))
        if text:
            d.text((105, y), text, font=body, fill=color)
        y += 52
    path = OUT / "screenshot-before.png"
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return path


def run(binary: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(binary), *args], capture_output=True, text=True)


def compose(before: Path, after: Path) -> Path:
    b, a = Image.open(before), Image.open(after).convert("RGB")
    label_h, gap, pad = 64, 28, 28
    canvas = Image.new("RGB", (W * 2 + gap + pad * 2, H + label_h + pad * 2), (15, 23, 42))
    d = ImageDraw.Draw(canvas)
    title = font(34, mono=False)
    for x, text, color in [(pad, "BEFORE  -  secrets in plain sight", (248, 113, 113)),
                            (pad + W + gap, "AFTER  -  secretsimage redact (solid fill)", (74, 222, 128))]:
        d.text((x + 4, pad + 8), text, font=title, fill=color)
    canvas.paste(b, (pad, pad + label_h))
    canvas.paste(a, (pad + W + gap, pad + label_h))
    # Arrow between the two panels.
    cy = pad + label_h + H // 2
    d.polygon([(pad + W + 2, cy - 16), (pad + W + gap - 2, cy), (pad + W + 2, cy + 16)],
              fill=(226, 232, 240))
    out = OUT / "before-after.png"
    canvas.save(out)
    return out


def main() -> int:
    binary = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build-release" / "secretsimage.exe"
    if not binary.exists():
        binary = ROOT / "build" / "secretsimage"
    if not binary.exists():
        print(f"error: secretsimage binary not found ({binary}); build first", file=sys.stderr)
        return 1

    before = render_before()
    after = OUT / "screenshot-after.png"

    scan_before = run(binary, "scan", str(before))
    print(f"scan before : exit={scan_before.returncode} (expect 2)\n{scan_before.stdout.strip()}")
    redact = run(binary, "redact", str(before), "-o", str(after))
    print(f"redact      : exit={redact.returncode}\n{redact.stdout.strip()}{redact.stderr.strip()}")
    if redact.returncode != 0 or not after.exists():
        return 1
    scan_after = run(binary, "scan", str(after))
    print(f"scan after  : exit={scan_after.returncode} (expect 0)\n{scan_after.stdout.strip()}")

    if scan_before.returncode != 2:
        print("error: synthetic secrets were NOT detected in the BEFORE image", file=sys.stderr)
        return 1
    # Six planted secrets (AWS key id, AWS secret, GitHub token, password,
    # e-mail, private IP). Fewer findings means a secret would stay visible
    # in the AFTER image - refuse to publish that picture.
    import re
    match = re.search(r"(\d+) finding", scan_before.stdout)
    found = int(match.group(1)) if match else 0
    if found < 6:
        print(f"error: only {found}/6 planted secrets detected; AFTER would leak", file=sys.stderr)
        return 1
    if scan_after.returncode != 0:
        print("error: secrets still detectable in the AFTER image", file=sys.stderr)
        return 1

    combined = compose(before, after)
    audit = Path(str(after) + ".audit.json")  # run artifact, gitignored
    print(f"wrote {before.name}, {after.name}, {combined.name} (audit: {audit.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
