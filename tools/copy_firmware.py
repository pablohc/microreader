Import("env")
import os
import shutil
import struct
import sys

# Make tools/ importable so we can reuse build_assets.py.  SCons exec's this
# file without setting __file__, so resolve the path via PROJECT_DIR instead.
_TOOLS_DIR = os.path.join(env.subst("$PROJECT_DIR"), "tools")
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)
import build_assets  # noqa: E402


def _image_end_offset(data: bytes) -> int:
    """Return the byte offset *immediately after* the IDF image inside firmware.bin.

    Image layout: [24-byte header][N × 8-byte segment header + data]
                  [pad to 16-byte boundary including 1-byte checksum]
                  [optional 32-byte SHA-256]
    """
    if not data or data[0] != 0xE9:
        raise SystemExit("copy_firmware: firmware.bin doesn't start with 0xE9 magic")
    seg_count = data[1]
    hash_appended = data[23]
    off = 24
    for _ in range(seg_count):
        _load_addr, data_len = struct.unpack_from("<II", data, off)
        off += 8 + data_len
    # 1-byte checksum + padding so this region is a multiple of 16 bytes.
    off = (off + 16) & ~15
    if hash_appended:
        off += 32
    return off


def _append_assets(firmware_path: str, project_dir: str) -> None:
    build_dir = env.subst("$BUILD_DIR")
    assets_path = os.path.join(build_dir, "assets.bin")
    build_assets.build(project_dir, assets_path)

    with open(firmware_path, "rb") as f:
        firmware = bytearray(f.read())

    image_end = _image_end_offset(firmware)
    aligned = (image_end + 0xFFF) & ~0xFFF
    # Truncate any stale trailer from a previous build and re-pad cleanly.
    if len(firmware) > image_end:
        firmware = firmware[:image_end]
    if aligned > len(firmware):
        firmware += b"\xff" * (aligned - len(firmware))

    with open(assets_path, "rb") as f:
        assets = f.read()
    firmware += assets

    with open(firmware_path, "wb") as f:
        f.write(firmware)

    print(f"[copy_firmware] image_end=0x{image_end:x}  assets@0x{aligned:x}  "
          f"final firmware size={len(firmware):,} bytes")


def copy_firmware(source, target, env):
    target_firmpath = str(target[0])
    project_dir = env.get("PROJECT_DIR")

    _append_assets(target_firmpath, project_dir)

    dest_firmpath = os.path.join(project_dir, "firmware.bin")
    print(f"Copying firmware to root: {dest_firmpath}")
    shutil.copy(target_firmpath, dest_firmpath)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware)
