#!/usr/bin/env python3
"""
Convert a PNG image to Microreader grayscale layers (BW, LSB, MSB).
Outputs 3 PNGs for debugging and a C++ header file with packed binary data.

Usage:
    python png_to_grayscale_layers.py input.png --out-prefix outname

Requires: Pillow
"""
import sys
import os
import argparse
from PIL import Image, ImageOps
import numpy as np


def quantize_5level_atkinson(img):
    """
    Quantize to 5 levels using Atkinson dithering.
    Atkinson dithering preserves higher local contrast and eliminates the 'wormy' noise
    often seen in Floyd-Steinberg, making it the visually superior choice for e-ink screens.
    """
    # Convert image to numpy array in [0.0, 1.0] range (L mode: 0=black, 1=white)
    arr = np.array(img.convert("L"), dtype=float) / 255.0
    h, w = arr.shape
    out = np.zeros((h, w), dtype=np.uint8)

    for y in range(h):
        for x in range(w):
            old_val = arr[y, x]
            # Clamp before quantizing
            old_val = max(0.0, min(1.0, old_val))
            
            # Quantize to 5 levels (0.0, 0.25, 0.5, 0.75, 1.0)
            new_val = np.round(old_val * 4) / 4.0
            out[y, x] = int(new_val * 4)
            
            # Calculate and distribute error
            err = old_val - new_val
            err8 = err / 8.0  # Atkinson scatters 6/8 of the error across 6 neighbors
            
            if x + 1 < w: arr[y, x + 1] += err8
            if x + 2 < w: arr[y, x + 2] += err8
            if y + 1 < h:
                if x - 1 >= 0: arr[y + 1, x - 1] += err8
                arr[y + 1, x] += err8
                if x + 1 < w: arr[y + 1, x + 1] += err8
            if y + 2 < h:
                arr[y + 2, x] += err8

    # PIL 'L' mode output mapped above: 0=black, ..., 4=white.
    # Microreader expects: 0=white, 1=light, 2=gray, 3=dark, 4=black.
    return 4 - out


def main():
    parser = argparse.ArgumentParser(
        description="Convert PNG to Microreader grayscale layers."
    )
    parser.add_argument("input", help="Input PNG image")
    parser.add_argument(
        "--out-prefix", default="output", help="Prefix for output files"
    )
    parser.add_argument(
        "--bin", action="store_true", help="Output a raw binary file instead of C++ header"
    )
    parser.add_argument(
        "--save-layers", action="store_true", help="Save the individual BW, LSB, MSB layers as PNGs for debugging"
    )
    args = parser.parse_args()

    img = Image.open(args.input).convert("L")
    
    # Microreader memory layout expects physical landscape orientation (e.g., 800x480).
    # If the user passes a portrait image, transpose it before processing.
    if img.width < img.height:
        img = img.transpose(Image.Transpose.ROTATE_90)
        
    # Scale and crop to fit the 800x480 screen
    img = ImageOps.fit(img, (800, 480), method=Image.Resampling.LANCZOS)
        
    levels = quantize_5level_atkinson(img)

    # BW: 1 if black/gray/dark, 0 otherwise (levels 2,3,4)
    bw = ((levels >= 2).astype(np.uint8)) * 255
    # LSB: 1 if light/dark, 0 otherwise (levels 1,3)
    lsb = (((levels == 1) | (levels == 3)).astype(np.uint8)) * 255
    # MSB: 1 if gray/dark, 0 otherwise (levels 2,3)
    msb = (((levels == 2) | (levels == 3)).astype(np.uint8)) * 255

    # Save debug PNGs
    if args.save_layers:
        Image.fromarray(bw).save(f"{args.out_prefix}_bw.png")
        Image.fromarray(lsb).save(f"{args.out_prefix}_lsb.png")
        Image.fromarray(msb).save(f"{args.out_prefix}_msb.png")
        print(f"Saved {args.out_prefix}_bw.png, _lsb.png, _msb.png")

    # Pack bits into bytes (row-major, left-to-right, top-to-bottom)
    def pack_bits(arr, invert=False):
        h, w = arr.shape
        row_stride = (w + 7) // 8
        out = bytearray(row_stride * h)
        for y in range(h):
            for x in range(w):
                val = arr[y, x]
                if invert:
                    if not val:
                        out[y * row_stride + x // 8] |= 1 << (7 - (x % 8))
                else:
                    if val:
                        out[y * row_stride + x // 8] |= 1 << (7 - (x % 8))
        return out

    bw_bytes = pack_bits(bw, invert=True)
    lsb_bytes = pack_bits(lsb, invert=False)
    msb_bytes = pack_bits(msb, invert=False)

    # Write C++ header or Binary file
    if args.bin:
        bin_path = f"{args.out_prefix}.mgr"
        with open(bin_path, "wb") as f:
            import struct
            # Simple header: 4 bytes "MGR1", then width (u16), height (u16)
            f.write(b"MGR1")
            f.write(struct.pack("<HH", img.width, img.height))
            f.write(bw_bytes)
            f.write(lsb_bytes)
            f.write(msb_bytes)
        print(f"Wrote {bin_path}")
    else:
        header_path = f"{args.out_prefix}_grayscale.h"
        var_base = os.path.basename(args.out_prefix)
        with open(header_path, "w") as f:
            f.write("#pragma once\n\n")
            f.write(f"// Auto-generated from {args.input}\n")
            f.write(f"constexpr int {var_base}_width = {img.width};\n")
            f.write(f"constexpr int {var_base}_height = {img.height};\n\n")
            for name, data in zip(["bw", "lsb", "msb"], [bw_bytes, lsb_bytes, msb_bytes]):
                f.write(f"constexpr unsigned char {var_base}_{name}[] = {{\n    ")
                for i, b in enumerate(data):
                    f.write(f"0x{b:02x}, ")
                    if (i + 1) % 16 == 0:
                        f.write("\n    ")
                f.write("\n};\n\n")
        print(f"Wrote {header_path}")


if __name__ == "__main__":
    main()
