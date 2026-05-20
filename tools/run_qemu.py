#!/usr/bin/env python3
"""Run the microreader2 firmware in Espressif QEMU (ESP32-C3).

Usage:
    python tools/run_qemu.py                          # start with empty SD card
    python tools/run_qemu.py --with-books             # pre-populate sd/books/ into flash
    python tools/run_qemu.py --with-books path/books  # custom books directory
    python tools/run_qemu.py --epub-only              # skip .mrb files in FAT image
    python tools/run_qemu.py --rebuild                # force firmware + flash rebuild

Then in a separate terminal:
    python tools/test_books.py --port socket://localhost:4444

Notes:
  - UART0 (TCP port) carries both ESP_LOGx console text AND the binary serial
    protocol, exactly as the real device does over USB-JTAG.
  - The flash image (qemu_flash.bin) is reused between runs.  Books stay in the
    FAT until --rebuild is used.
  - The e-ink SPI commands are silently ignored (QEMU has no SPI simulation).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent.parent
BUILD_DIR = WORKSPACE / ".pio" / "build" / "esp32c3-qemu"
FLASH_IMAGE = BUILD_DIR / "qemu_flash.bin"
PARTITION_CSV = WORKSPACE / "platforms" / "esp32" / "default_qemu.csv"
SD_BOOKS_DIR = WORKSPACE / "sd" / "books"

# Flash region layout: (offset, filename)  binaries live under BUILD_DIR.
FLASH_REGIONS = (
    (0x0000, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, "ota_data_initial.bin"),
    (0x10000, "firmware.bin"),
)


# ---------------------------------------------------------------------------
# QEMU discovery
# ---------------------------------------------------------------------------

def _prepend_msys2_to_path():
    """Add MSYS2 mingw64/bin to PATH on Windows (no-op elsewhere).

    Espressif QEMU Windows binaries dynamically link MinGW runtime DLLs that
    are NOT bundled in the package.  Without this they silently exit with
    STATUS_DLL_NOT_FOUND (0xC0000135).
    """
    if os.name != "nt":
        return
    for p in ("C:/msys64/mingw64/bin", "C:/tools/msys64/mingw64/bin"):
        if os.path.isdir(p) and p not in os.environ.get("PATH", ""):
            os.environ["PATH"] = p + ";" + os.environ.get("PATH", "")
            return


def find_qemu():
    """Search for qemu-system-riscv32 in PATH and common IDF tool locations."""
    _prepend_msys2_to_path()

    qemu = shutil.which("qemu-system-riscv32")
    if qemu:
        return Path(qemu)

    for espressif_root in (
        Path("C:/Espressif"),
        Path(os.environ.get("USERPROFILE", "~")).expanduser() / ".espressif",
    ):
        fw_root = espressif_root / "frameworks"
        if not fw_root.exists():
            continue
        for idf_dir in sorted(fw_root.iterdir(), reverse=True):
            candidate_dir = idf_dir / "tools" / "qemu-riscv32"
            if not candidate_dir.exists():
                continue
            for ver_dir in sorted(candidate_dir.iterdir(), reverse=True):
                candidate = ver_dir / "qemu" / "bin" / "qemu-system-riscv32.exe"
                if candidate.exists():
                    return candidate

    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        candidate = Path(idf_path) / "tools" / "qemu-system-riscv32"
        if candidate.exists():
            return candidate

    return None


# ---------------------------------------------------------------------------
# Firmware / flash helpers
# ---------------------------------------------------------------------------

def _find_esptool_cmd():
    """Return command prefix to invoke esptool (as a list ready for subprocess)."""
    userprofile = Path(os.environ.get("USERPROFILE", "~")).expanduser()
    pio_scripts = userprofile / ".platformio" / "penv" / "Scripts"

    candidate = pio_scripts / "esptool.exe"
    if candidate.exists():
        return [str(candidate)]

    esptool_on_path = shutil.which("esptool")
    if esptool_on_path and "msys" not in esptool_on_path.lower():
        return [esptool_on_path]

    pio_python = pio_scripts / "python.exe"
    if pio_python.exists():
        return [str(pio_python), "-m", "esptool"]

    return [sys.executable, "-m", "esptool"]


def _find_pio():
    userprofile = Path(os.environ.get("USERPROFILE", "~")).expanduser()
    for candidate in (
        userprofile / ".platformio" / "penv" / "Scripts" / "pio.exe",
        userprofile / ".platformio" / "penv" / "bin" / "pio",
    ):
        if candidate.exists():
            return str(candidate)
    return shutil.which("pio")


def build_flash_image():
    """Merge individual firmware binaries into a single 16 MB flash image."""
    cmd = _find_esptool_cmd() + [
        "--chip", "esp32c3", "merge_bin",
        "--output", str(FLASH_IMAGE),
        "--fill-flash-size", "16MB",
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", "16MB",
    ]
    for offset, filename in FLASH_REGIONS:
        path = BUILD_DIR / filename
        if path.exists():
            cmd += [hex(offset), str(path)]
        elif filename != "ota_data_initial.bin":
            print(f"ERROR: required binary missing: {path}", file=sys.stderr)
            print("Run: pio run -e esp32c3-qemu", file=sys.stderr)
            sys.exit(1)

    print("Building flash image...")
    subprocess.run(cmd, check=True)
    print(f"Flash image: {FLASH_IMAGE}")
    FLASH_IMAGE.touch()


def _ensure_firmware(rebuild):
    firmware_bin = BUILD_DIR / "firmware.bin"
    if rebuild or not firmware_bin.exists():
        pio = _find_pio()
        if not pio:
            print("ERROR: PlatformIO not found. Build manually: pio run -e esp32c3-qemu",
                  file=sys.stderr)
            sys.exit(1)
        print("Building firmware (pio run -e esp32c3-qemu)...")
        subprocess.run([pio, "run", "-e", "esp32c3-qemu"], cwd=str(WORKSPACE), check=True)
    if not (BUILD_DIR / "firmware.bin").exists():
        print("ERROR: firmware.bin missing after build.", file=sys.stderr)
        sys.exit(1)


# ---------------------------------------------------------------------------
# FAT pre-population (for --with-books)
# ---------------------------------------------------------------------------

def _parse_fat_partition(csv_path):
    """Return (offset, size) for the fat partition from a PlatformIO CSV."""
    with open(csv_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("#") or not line:
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 5 and parts[0] == "fat":
                def _int(s):
                    return int(s, 16) if s.startswith("0x") else int(s)
                return _int(parts[3]), _int(parts[4])
    raise RuntimeError(f"No 'fat' partition found in {csv_path}")


def _find_wl_fatfsgen():
    """Locate wl_fatfsgen.py from PlatformIO's ESP-IDF package."""
    userprofile = Path(os.environ.get("USERPROFILE", "~")).expanduser()
    candidates = [
        userprofile / ".platformio" / "packages" / "framework-espidf"
        / "components" / "fatfs" / "wl_fatfsgen.py",
    ]
    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        candidates.append(Path(idf_path) / "components" / "fatfs" / "wl_fatfsgen.py")
    for c in candidates:
        if c.exists():
            return c
    raise FileNotFoundError(
        "wl_fatfsgen.py not found.\n"
        "Install: pio pkg install -g --platform espressif32"
    )


def _generate_fat_image(books_dir, partition_size, out, epub_only):
    """Create a WL FAT image with books under books/."""
    wl_fatfsgen = _find_wl_fatfsgen()
    with tempfile.TemporaryDirectory() as tmpdir:
        staging_books = Path(tmpdir) / "sdcard_root" / "books"
        staging_books.mkdir(parents=True)

        total, count = 0, 0
        for f in sorted(books_dir.iterdir()):
            if not f.is_file():
                continue
            ext = f.suffix.lower()
            if epub_only and ext == ".mrb":
                continue
            if ext in (".epub", ".mrb"):
                shutil.copy2(f, staging_books / f.name)
                total += f.stat().st_size
                count += 1

        if count == 0:
            print("  WARNING: No books found  FAT image will be empty.")
        else:
            print(f"  {count} files ({total / (1024*1024):.1f} MB) -> "
                  f"{partition_size / (1024*1024):.0f} MB partition")
            if total > partition_size * 0.95:
                print("  WARNING: Total size may exceed the FAT partition.")

        result = subprocess.run(
            [sys.executable, str(wl_fatfsgen), str(staging_books.parent),
             "--output_file", str(out),
             "--partition_size", str(partition_size),
             "--sector_size", "4096",
             "--long_name_support",
             "--use_default_datetime"],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"wl_fatfsgen.py failed:\n{result.stderr}")

    print(f"  FAT image: {out} ({out.stat().st_size // 1024} KB)")
    return out


def populate_books(books_dir, epub_only=False, rebuild=False):
    """Generate a FAT image with books and splice it into the flash image."""
    fat_image = BUILD_DIR / "fat_partition.bin"

    need_fat = rebuild or not fat_image.exists()
    if not need_fat:
        fat_mtime = fat_image.stat().st_mtime
        for f in books_dir.iterdir():
            if f.is_file() and f.suffix.lower() in (".epub", ".mrb"):
                if f.stat().st_mtime > fat_mtime:
                    need_fat = True
                    break

    fat_offset, fat_size = _parse_fat_partition(PARTITION_CSV)

    if need_fat:
        print("Generating FAT image with books...")
        _generate_fat_image(books_dir, fat_size, fat_image, epub_only)
    else:
        print("FAT image up to date.")

    firmware_bin = BUILD_DIR / "firmware.bin"
    need_flash = (
        rebuild or not FLASH_IMAGE.exists()
        or firmware_bin.stat().st_mtime > FLASH_IMAGE.stat().st_mtime
        or need_fat
    )
    if need_flash:
        build_flash_image()
        print(f"Splicing FAT at offset 0x{fat_offset:X} ...")
        fat_data = fat_image.read_bytes()
        with open(FLASH_IMAGE, "r+b") as fh:
            fh.seek(fat_offset)
            fh.write(fat_data)
        print("Flash image ready.")
    else:
        print("Flash image up to date.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Run microreader2 in Espressif QEMU (ESP32-C3).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Typical workflow:\n"
            "  Terminal 1: python tools/run_qemu.py --with-books\n"
            "  Terminal 2: python tools/test_books.py --port socket://localhost:4444"
        ),
    )
    parser.add_argument("--port", type=int, default=4444,
                        help="TCP port for UART0 serial (default: 4444)")
    parser.add_argument("--wait", action="store_true",
                        help="Wait for a serial client to connect before starting firmware")
    parser.add_argument("--rebuild", action="store_true",
                        help="Force rebuild of firmware + flash image")
    parser.add_argument("--with-books", nargs="?", const=str(SD_BOOKS_DIR), metavar="DIR",
                        help="Pre-populate books into flash FAT (default: sd/books/)")
    parser.add_argument("--epub-only", action="store_true",
                        help="Only include .epub files (skip .mrb) in FAT image")
    args = parser.parse_args()

    qemu = find_qemu()
    if not qemu:
        print("ERROR: qemu-system-riscv32 not found.", file=sys.stderr)
        print("Install: python %IDF_PATH%\\tools\\idf_tools.py install qemu-riscv32",
              file=sys.stderr)
        sys.exit(1)

    if args.with_books:
        books_dir = Path(args.with_books)
        if not books_dir.exists():
            print(f"ERROR: Books directory not found: {books_dir}", file=sys.stderr)
            sys.exit(1)
        _ensure_firmware(args.rebuild)
        print(f"Pre-populating books from {books_dir} ...")
        populate_books(books_dir, epub_only=args.epub_only, rebuild=args.rebuild)
    else:
        if args.rebuild or not FLASH_IMAGE.exists():
            _ensure_firmware(args.rebuild)
            build_flash_image()

    nowait = "" if args.wait else ",nowait"
    cmd = [
        str(qemu),
        "-nographic",
        "-machine", "esp32c3",
        "-drive", f"file={FLASH_IMAGE},if=mtd,format=raw",
        "-serial", f"tcp::{args.port},server{nowait}",
    ]

    print(f"Starting QEMU (UART0 on TCP port {args.port}) ...")
    print(f"  Connect: python tools/test_books.py --port socket://localhost:{args.port}")
    subprocess.run(cmd)


if __name__ == "__main__":
    main()