#!/usr/bin/env python3
"""
Generate a self-contained HTML font overview page using real bitmap rendering
via FreeType — the same 5-level grayscale pipeline used by generate_font.py.

Each font family is shown at 3 sizes (24, 28, 32px) × 4 styles (R/B/I/BI).
Bitmaps are scaled 2× (nearest-neighbour) so individual pixels are visible.

Usage:
    python tools/font_overview.py [--dir <ttf_dir>] [--out <output.html>] [--scale N]

Defaults:
    --dir    resources/sd fonts/ttf
    --out    tools/font_overview.html
    --scale  2
"""

import argparse
import base64
import io
import os
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Import render_glyph from generate_font (same tools/ directory)
# ---------------------------------------------------------------------------
_tools_dir = Path(__file__).parent
sys.path.insert(0, str(_tools_dir))
from generate_font import render_glyph  # noqa: E402

try:
    import freetype
except ImportError:
    print("ERROR: freetype-py not installed. Run: pip install freetype-py")
    sys.exit(1)

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

SIZES = [24, 28, 32]
SAMPLE_SENTENCES = [
    "The quick brown fox jumps over the lazy dog.",
    "Pack my box with five dozen liquor jugs!",
]

# 5-level grayscale palette: 0=white … 4=black
_PALETTE = [255, 200, 140, 80, 0]

STYLE_ORDER = ["Regular", "Bold", "Italic", "BoldItalic"]
STYLE_LABEL = {
    "Regular":    "Regular",
    "Bold":       "Bold",
    "Italic":     "Italic",
    "BoldItalic": "Bold Italic",
}


def _normalize_style(filename: str) -> str:
    stem = Path(filename).stem
    for sep in ("-", "_"):
        if sep in stem:
            candidate = stem.rsplit(sep, 1)[-1].replace("-", "").replace("_", "")
            if candidate in STYLE_ORDER:
                return candidate
    stem_clean = stem.replace("-", "").replace("_", "").replace(" ", "")
    for style in ["BoldItalic", "Bold", "Italic", "Regular"]:
        if style.lower() in stem_clean.lower():
            return style
    return "Regular"


def _family_name(filename: str) -> str:
    stem = Path(filename).stem
    for style in ["BoldItalic", "Bold_Italic", "Bold-Italic", "Bold", "Italic", "Regular"]:
        if stem.endswith("-" + style) or stem.endswith("_" + style):
            stem = stem[:-(len(style) + 1)]
            break
    return stem.replace("_", " ")


def collect_families(ttf_dir: Path) -> dict:
    """Returns {family_display_name: {style_key: Path}}"""
    families = {}
    for f in sorted(ttf_dir.iterdir()):
        if f.suffix.lower() not in {".ttf", ".otf"}:
            continue
        fam = _family_name(f.name)
        style = _normalize_style(f.name)
        families.setdefault(fam, {})[style] = f
    return families


# ---------------------------------------------------------------------------
# Bitmap rendering
# ---------------------------------------------------------------------------

def render_string_image(face, text: str, size: int, scale: int = 2) -> Image.Image:
    """Render `text` at `size` px using the FreeType face.
    Returns a PIL Image with 5-level grayscale, scaled up by `scale`."""
    face.set_pixel_sizes(0, size)
    ascender  = face.size.ascender  >> 6
    descender = face.size.descender >> 6
    line_h    = ascender - descender

    glyphs = []
    total_qpx = 0
    for ch in text:
        g = render_glyph(face, ord(ch), size)
        if g is None:
            total_qpx += size * 2
            glyphs.append(None)
        else:
            total_qpx += g["x_advance"]
            glyphs.append(g)

    canvas_w = max(1, (total_qpx + 3) // 4 + 4)
    canvas_h = max(1, line_h)
    pixels   = [0] * (canvas_w * canvas_h)  # 0 = white

    x_qpx = 0
    for g in glyphs:
        if g is None:
            x_qpx += size * 2
            continue
        if g["bitmap_width"] > 0 and g["bitmap_height"] > 0:
            x_px = (x_qpx + 2) // 4 + g["x_offset"]
            y_px = ascender + g["y_offset"]
            bw, bh = g["bitmap_width"], g["bitmap_height"]
            gs5 = g["grayscale5"]
            for gy in range(bh):
                cy = y_px + gy
                if cy < 0 or cy >= canvas_h:
                    continue
                row = cy * canvas_w
                src = gy * bw
                for gx in range(bw):
                    cx = x_px + gx
                    if 0 <= cx < canvas_w:
                        v = gs5[src + gx]
                        if v > pixels[row + cx]:
                            pixels[row + cx] = v
        x_qpx += g["x_advance"]

    img = Image.new("L", (canvas_w, canvas_h))
    img.putdata([_PALETTE[v] for v in pixels])
    if scale != 1:
        img = img.resize((canvas_w * scale, canvas_h * scale), Image.NEAREST)
    return img


def _img_to_data_uri(img: Image.Image) -> str:
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    b64 = base64.b64encode(buf.getvalue()).decode("ascii")
    return f"data:image/png;base64,{b64}"


# ---------------------------------------------------------------------------
# HTML generation
# ---------------------------------------------------------------------------

def build_html(families: dict, scale: int) -> str:
    family_blocks = []
    sample_text = "  ".join(SAMPLE_SENTENCES)

    for fam_name, styles in sorted(families.items()):
        print(f"  {fam_name} ({', '.join(s for s in STYLE_ORDER if s in styles)})")

        style_blocks = []
        for style_key in STYLE_ORDER:
            if style_key not in styles:
                continue
            path = styles[style_key]
            try:
                face = freetype.Face(str(path))
            except Exception as e:
                print(f"    SKIP {style_key}: {e}")
                continue

            size_rows = []
            for size in SIZES:
                try:
                    img = render_string_image(face, sample_text, size, scale=scale)
                    uri = _img_to_data_uri(img)
                    cell = (f'<img src="{uri}" alt="{size}px" '
                            f'style="display:block;image-rendering:pixelated;">')
                except Exception as e:
                    cell = f'<span style="color:red;font-size:11px">Error: {e}</span>'
                size_rows.append(
                    f'<tr><td class="sz">{size}px</td>'
                    f'<td class="gc">{cell}</td></tr>'
                )

            style_blocks.append(
                f'<div class="sb">'
                f'<div class="sn">{STYLE_LABEL[style_key]}</div>'
                f'<table>{"".join(size_rows)}</table>'
                f'</div>'
            )

        family_blocks.append(
            f'<section class="family">'
            f'<h2>{fam_name}</h2>'
            f'{"".join(style_blocks)}'
            f'</section>'
        )

    num_families  = len(families)
    total_styles  = sum(len(v) for v in families.values())
    sizes_str     = " / ".join(f"{s}px" for s in SIZES)
    all_families  = "\n\n".join(family_blocks)

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Font Overview — microreader2</title>
<style>
*,*::before,*::after{{box-sizing:border-box}}
body{{background:#e0e0e0;color:#111;font-family:system-ui,sans-serif;margin:0;padding:24px}}
h1{{font-size:20px;font-weight:600;margin:0 0 4px}}
.meta{{font-size:13px;color:#666;margin-bottom:28px}}
.family{{background:#fff;border:1px solid #bbb;border-radius:6px;padding:16px 20px 12px;margin-bottom:22px;box-shadow:0 1px 4px rgba(0,0,0,.09)}}
.family h2{{font-size:11px;font-weight:700;letter-spacing:.07em;text-transform:uppercase;color:#888;margin:0 0 12px}}
.sb{{margin-bottom:12px;padding-bottom:10px;border-bottom:1px solid #f0f0f0}}
.sb:last-child{{border-bottom:none;margin-bottom:0;padding-bottom:0}}
.sn{{font-size:10px;font-weight:600;letter-spacing:.05em;text-transform:uppercase;color:#bbb;margin-bottom:4px}}
table{{border-collapse:collapse}}
td{{padding:2px 6px 2px 0;vertical-align:middle}}
.sz{{font-size:10px;color:#ccc;white-space:nowrap;width:30px;text-align:right;padding-right:8px}}
.gc{{background:#fff}}
img{{display:block;image-rendering:-moz-crisp-edges;image-rendering:-webkit-optimize-contrast;image-rendering:pixelated}}
</style>
</head>
<body>
<h1>Font Overview</h1>
<p class="meta">{num_families} families &nbsp;·&nbsp; {total_styles} styles &nbsp;·&nbsp; {sizes_str} &nbsp;·&nbsp; {scale}× scale &nbsp;·&nbsp; 5-level grayscale (as rendered on device)</p>

{all_families}
</body>
</html>
"""


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    here        = Path(__file__).parent
    default_dir = here.parent / "resources" / "sd fonts" / "ttf"
    default_out = here / "font_overview.html"

    p = argparse.ArgumentParser(description="Generate bitmap font overview page")
    p.add_argument("--dir",   default=str(default_dir), help="Directory with TTF/OTF files")
    p.add_argument("--out",   default=str(default_out), help="Output HTML path")
    p.add_argument("--scale", type=int, default=1,       help="Pixel scale factor (default: 1)")
    args = p.parse_args()

    ttf_dir = Path(args.dir)
    if not ttf_dir.is_dir():
        print(f"ERROR: directory not found: {ttf_dir}")
        sys.exit(1)

    families = collect_families(ttf_dir)
    if not families:
        print(f"ERROR: no TTF/OTF files found in {ttf_dir}")
        sys.exit(1)

    print(f"Rendering {len(families)} families at {SIZES} px (scale {args.scale}×)...")
    html = build_html(families, args.scale)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(html, encoding="utf-8")
    kb = out_path.stat().st_size / 1024
    print(f"\nWritten: {out_path} ({kb:.0f} KB)")


if __name__ == "__main__":
    main()
