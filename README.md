# microreader2

Minimal EPUB reader for ESP32-C3 + SSD1677 e-ink display (480×800 portrait).
Includes a desktop SDL2 emulator for development without hardware.

## Hardware

| | |
|---|---|
| MCU | ESP32-C3 (RISC-V, 160 MHz) |
| Display | 4.26" e-ink 800×480 (SSD1677), rotated → 480×800 portrait |
| Storage | SD card (FAT32, SPI) |
| Flash | 16 MB |
| Input | ADC buttons |

## Project Structure

```
lib/microreader/       shared core (platform-agnostic C++17)
  content/             EPUB parsing, layout, MRB binary format
  display/             Canvas, DisplayQueue, Font interfaces
  screens/             UI screen implementations
platforms/desktop/     SDL2 emulator
platforms/esp32/       ESP-IDF + PlatformIO firmware
test/                  Google Test suite
tools/                 Python scripts
resources/             Fonts, sleep images
```

## Building

### Desktop (emulator)

```powershell
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
.\build\desktop-debug\Debug\microreader_desktop.exe
```

### ESP32 (PlatformIO)

```powershell
# Build + flash
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload

# Serial monitor
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```

COM4, upload baud 921600.

### Tests

```powershell
cd test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
cmake --build build2 --config Debug

.\build2\Debug\unit_tests.exe          # fast (~375 tests, <1s)
.\build2\Debug\microreader_tests.exe   # includes real EPUB integration tests
```

## Device Management

Books (`.epub`) can go anywhere on the SD card — the device scans recursively from the root. Fonts (`.mfb`) go in `/fonts/`. You can copy files directly or use `tools/serial_cmd.py` to transfer over serial:

```powershell
# Upload an EPUB book
python tools/serial_cmd.py --port COM4 --upload path/to/book.epub

# Upload an SD card font (no firmware rebuild needed)
python tools/serial_cmd.py --port COM4 --upload-sd-font "resources/sd fonts/Cartisse.mfb"

# Upload all SD fonts
foreach ($f in (Get-ChildItem "resources/sd fonts/*.mfb")) {
    python tools/serial_cmd.py --port COM4 --upload-sd-font $f.FullName
}

# Interactive console (status, button injection, benchmarks)
python tools/serial_cmd.py --port COM4
```

## Font Generation

Reader fonts are FNTS bundles (`.mfb`), generated from TTF/OTF sources via `tools/generate_font.py`.

Two kinds:
- **Built-in** (`resources/fonts/`) — embedded in the firmware asset blob. Require a firmware rebuild to update.
- **SD card** (`resources/sd fonts/`) — loaded from `/sdcard/fonts/` at runtime. No firmware rebuild needed; just copy or upload.

The generation command is the same for both:

```powershell
python tools/generate_font.py "resources/sd fonts/ttf/Cartisse-Regular.ttf" `
  -o "resources/sd fonts/Cartisse.mfb" --with-styles `
  --bold "resources/sd fonts/ttf/Cartisse-Bold.ttf" `
  --italic "resources/sd fonts/ttf/Cartisse-Italic.ttf" `
  --bold-italic "resources/sd fonts/ttf/Cartisse-BoldItalic.ttf" `
  --bundle --bundle-sizes 20 22 24 26 28 30 32 --font-name Cartisse

# Regenerate all SD fonts
$ttf = "resources/sd fonts/ttf"; $out = "resources/sd fonts"
foreach ($f in @("Bitter","Cartisse","NV_Bitter","NV_Charis","NV_Cooper","NV_Garamond","NV_Jost","NV_Palatium","Readerly")) {
    python tools/generate_font.py "$ttf/$f-Regular.ttf" -o "$out/$f.mfb" --with-styles `
      --bold "$ttf/$f-Bold.ttf" --italic "$ttf/$f-Italic.ttf" --bold-italic "$ttf/$f-BoldItalic.ttf" `
      --bundle --bundle-sizes 20 22 24 26 28 30 32 --font-name $f
}

# Font preview (generates tools/font_overview.html)
python tools/font_overview.py

# UI fonts (bitmap, bw-only)
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 14 -o resources/fonts/ui-small.mbf --header lib/microreader/display/ui_font_small.h --bw-only
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 32 -o resources/fonts/ui-header.mbf --header lib/microreader/display/ui_font_header.h --bw-only
```

> **Font partition limit**: SD card fonts must fit within 3.375 MB. The font data + 4 KB header must not exceed `0x360000` bytes.

## Firmware Backup & Restore

```powershell
# Backup running firmware partition
python -m esptool --port COM4 read_flash 0x10000 0x650000 app0_backup.bin

# Restore
python -m esptool --port COM4 write_flash 0x10000 app0_backup.bin

# Switch OTA boot partition
python tools/switch_partition.py app0 --port COM4 --flash
python tools/switch_partition.py app1 --port COM4 --flash
```

## Sleep Screen

Sleep screen images (`.mgr`) go in `/sleep/` on the SD card. Convert a JPEG first, then copy manually or upload via serial:

```powershell
python tools/image_to_mgr.py "resources/sleep/sleep_2.jpg" --out-prefix "resources/sleep/sleep_2" --bin
python tools/serial_cmd.py --port COM4 --upload-sleep "resources/sleep/sleep_2.mgr"
```

## QEMU Testing (no hardware needed)

```powershell
# Terminal 1
python tools/run_qemu.py --with-books

# Terminal 2
python tools/test_books.py --port socket://localhost:4444 --pages 20 --delay 0.1
```
