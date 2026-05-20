#!/usr/bin/env python3
"""Book open & navigation test for microreader2.

Connects to a device (real hardware or QEMU via socket), lists all books on the
SD card, then for each book: opens it, navigates N pages forward, closes it, and
reports pass/fail.  Optional heap tracking with --heap.

Usage:
    # Real device
    python tools/test_books.py [--port COM4] [--pages 20]

    # QEMU (run run_qemu.py --with-books first, in a separate terminal)
    python tools/test_books.py --port socket://localhost:4444 --pages 20

Options:
    --port PORT     Serial port or socket URL (default: COM4)
    --baud N        Baud rate for real hardware (default: 115200)
    --pages N       Pages to navigate per book (default: 20)
    --delay S       Seconds between page presses (default: 0.3)
    --filter STR    Only test books whose filename contains STR
    --clean         Delete all .mrb files before testing (forces fresh conversion)
    --heap          Print heap stats per book (before open / after open / after close)
    -v              Verbose: print per-page timeouts and heap at every page
"""

import argparse
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

MAGIC = b"CMND"
BTN_BACK = 1 << 0  # Button0 — back / exit screen
BTN_DOWN = 1 << 2  # Button2 — next page


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------


def _open_serial(port: str, baud: int) -> serial.SerialBase:
    if "://" in port:
        return serial.serial_for_url(port, baudrate=baud, timeout=2)
    return serial.Serial(port, baud, timeout=2)


def drain(ser: serial.SerialBase, duration: float = 0.2) -> None:
    """Consume and discard buffered serial data."""
    deadline = time.time() + duration
    while time.time() < deadline:
        if not ser.read(4096):
            break


def _readline(ser: serial.SerialBase) -> str:
    return ser.readline().decode("utf-8", errors="replace").strip()


def _wait_for(
    ser: serial.SerialBase, predicates, timeout: float = 5.0, echo: bool = False
) -> Optional[str]:
    """Read lines until one satisfies any predicate; return that line or None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = _readline(ser)
        if not line:
            continue
        if echo:
            print(f"    [dev] {line}")
        for pred in predicates:
            if pred(line):
                return line
    return None


# ---------------------------------------------------------------------------
# Protocol commands
# ---------------------------------------------------------------------------


def cmd_status(ser: serial.SerialBase) -> Optional[str]:
    drain(ser, 0.05)
    ser.write(MAGIC + b"S")
    return _wait_for(ser, [lambda l: l.startswith("STATUS:")], timeout=3.0)


def cmd_list_books(ser: serial.SerialBase) -> list:
    drain(ser, 0.05)
    ser.write(MAGIC + b"L")
    books = []
    started = False
    deadline = time.time() + 5.0
    while time.time() < deadline:
        line = _readline(ser)
        if not line:
            continue
        if line.startswith("BOOKS:"):
            started = True
        elif line == "END":
            break
        elif line.startswith("ERR:"):
            break
        elif started:
            books.append(line.strip())
    return books


def cmd_open(ser: serial.SerialBase, path: str, timeout: float = 120.0) -> str:
    """Send Open command; return 'BOOK_OK', 'BOOK_FAIL', or 'ERR:...'."""
    drain(ser, 0.05)
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)
    # Wait for command acknowledgement.
    ack = _wait_for(
        ser, [lambda l: l.startswith("OK") or l.startswith("ERR:")], timeout=5.0
    )
    if not ack or ack.startswith("ERR:"):
        return ack or "ERR:no_ack"
    # Wait for the book to actually finish loading (may include MRB conversion).
    result = _wait_for(
        ser,
        [lambda l: "BOOK_OK:" in l or "BOOK_FAIL:" in l],
        timeout=timeout,
        echo=True,
    )
    if result and "BOOK_OK:" in result:
        return "BOOK_OK"
    if result and "BOOK_FAIL:" in result:
        return "BOOK_FAIL"
    return "ERR:timeout"


def cmd_button(ser: serial.SerialBase, mask: int, timeout: float = 30.0) -> bool:
    drain(ser, 0.02)
    ser.write(MAGIC + b"B" + bytes([mask & 0xFF]))
    resp = _wait_for(
        ser, [lambda l: l.startswith("OK") or l.startswith("ERR:")], timeout=timeout
    )
    return resp is not None and resp.startswith("OK")


def cmd_clear(ser: serial.SerialBase) -> str:
    drain(ser, 0.05)
    ser.write(MAGIC + b"C")
    resp = _wait_for(
        ser, [lambda l: l.startswith("CLEARED:") or l.startswith("ERR:")], timeout=10.0
    )
    return resp or "TIMEOUT"


# ---------------------------------------------------------------------------
# Heap parsing
# ---------------------------------------------------------------------------


@dataclass
class Heap:
    free: int
    largest: int

    @staticmethod
    def parse(line: Optional[str]) -> Optional["Heap"]:
        if not line or not line.startswith("STATUS:"):
            return None
        try:
            free = int(line.split("free=")[1].split(",")[0])
            largest = int(line.split("largest=")[1])
            return Heap(free, largest)
        except (IndexError, ValueError):
            return None

    def __str__(self) -> str:
        return f"free={self.free // 1024}KB largest={self.largest // 1024}KB"


# ---------------------------------------------------------------------------
# Per-book test
# ---------------------------------------------------------------------------


@dataclass
class BookResult:
    name: str
    status: str = "?"
    pages_navigated: int = 0
    baseline: Optional[Heap] = None
    after_open: Optional[Heap] = None
    after_close: Optional[Heap] = None

    def heap_cost(self) -> Optional[int]:
        if self.baseline and self.after_open:
            return self.after_open.free - self.baseline.free
        return None

    def leak(self) -> Optional[int]:
        """Positive = heap recovered; negative = leaked."""
        if self.baseline and self.after_close:
            return self.after_close.free - self.baseline.free
        return None


def test_book(
    ser: serial.SerialBase,
    name: str,
    pages: int,
    delay: float,
    verbose: bool,
    track_heap: bool,
    button_timeout: float = 30.0,
) -> BookResult:
    result = BookResult(name=name)
    path = name if name.startswith("/") else f"/sdcard/books/{name}"

    # Baseline heap.
    if track_heap:
        result.baseline = Heap.parse(cmd_status(ser))
        if verbose and result.baseline:
            print(f"    baseline: {result.baseline}")

    # Open book.
    print(f"  Opening {name} ...", flush=True)
    open_result = cmd_open(ser, path)
    if open_result != "BOOK_OK":
        result.status = f"FAIL ({open_result})"
        print(f"  FAILED: {open_result}")
        return result

    # Heap after open.
    if track_heap:
        result.after_open = Heap.parse(cmd_status(ser))
        if result.after_open:
            if result.baseline:
                kb = (result.after_open.free - result.baseline.free) // 1024
                print(f"  Opened — heap cost: {kb:+d}KB  ({result.after_open})")
            else:
                print(f"  Opened — heap: {result.after_open}")

    # Navigate pages.
    consecutive_fails = 0
    for page_num in range(1, pages + 1):
        ok = cmd_button(ser, BTN_DOWN, timeout=button_timeout)  # noqa: E501
        if not ok:
            consecutive_fails += 1
            if verbose:
                print(f"    page {page_num}: timeout ({consecutive_fails} consecutive)")
            if consecutive_fails >= 3:
                result.status = f"CRASH (3 consecutive timeouts at page {page_num})"
                print(f"  {result.status}")
                return result
        else:
            consecutive_fails = 0
            result.pages_navigated = page_num
        time.sleep(delay)

    # Close (back button).
    cmd_button(ser, BTN_BACK, timeout=button_timeout)  # noqa: E501
    time.sleep(1.0)
    drain(ser, 0.3)

    # Heap after close.
    if track_heap:
        result.after_close = Heap.parse(cmd_status(ser))
        leak = result.leak()
        if leak is not None:
            kb = leak // 1024
            flag = "  <-- LEAK" if kb < -4 else ""
            print(f"  Closed — heap: {result.after_close}  (Δbaseline={kb:+d}KB){flag}")

    result.status = "OK"
    return result


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="Book open & navigation test for microreader2.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python tools/test_books.py                              # real device, COM4\n"
            "  python tools/test_books.py --port socket://localhost:4444 --pages 30\n"
            "  python tools/test_books.py --filter alice --heap -v"
        ),
    )
    parser.add_argument(
        "--port", default="COM4", help="Serial port or socket URL (default: COM4)"
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Baud rate for real hardware (default: 115200)",
    )
    parser.add_argument(
        "--pages", type=int, default=20, help="Pages to navigate per book (default: 20)"
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=0.3,
        help="Seconds between page presses (default: 0.3; try 0.1 for QEMU)",
    )
    parser.add_argument(
        "--filter",
        default="",
        help="Only test books whose filename contains this string",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete all .mrb files before testing (forces fresh conversion)",
    )
    parser.add_argument(
        "--heap", action="store_true", help="Track and print heap stats per book"
    )
    parser.add_argument(
        "--button-timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for each button press OK (default: 30; increase for slow QEMU)",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print per-page detail"
    )
    args = parser.parse_args()

    print(f"Connecting to {args.port} ...", flush=True)
    try:
        ser = _open_serial(args.port, args.baud)
    except Exception as e:
        print(f"Cannot open port: {e}", file=sys.stderr)
        sys.exit(1)

    time.sleep(1.5)
    drain(ser, 0.5)

    # Sanity check.
    status = cmd_status(ser)
    heap = Heap.parse(status)
    if not heap:
        print(f"ERROR: Device not responding (got {status!r}).", file=sys.stderr)
        ser.close()
        sys.exit(1)
    print(f"Device OK.  Boot heap: {heap}\n")

    # Optionally clear .mrb files.
    if args.clean:
        print("Clearing .mrb files ...", end=" ", flush=True)
        print(cmd_clear(ser))
        time.sleep(1.0)

    # List books.
    print("Listing books ...", flush=True)
    all_books = cmd_list_books(ser)
    if not all_books:
        print("ERROR: No books found on device.", file=sys.stderr)
        ser.close()
        sys.exit(1)

    # Prefer .epub over .mrb when both exist for the same title.
    epub_set = {b for b in all_books if b.endswith(".epub")}
    mrb_only = [
        b for b in all_books if b.endswith(".mrb") and b[:-4] + ".epub" not in epub_set
    ]
    books = sorted(epub_set) + sorted(mrb_only)

    if args.filter:
        books = [b for b in books if args.filter in b]
    if not books:
        print(f"No books matching '{args.filter}'.", file=sys.stderr)
        ser.close()
        sys.exit(1)

    print(f"Testing {len(books)} book(s), {args.pages} pages each:\n")
    for b in books:
        print(f"  {b}")
    print()

    # Run tests.
    results: list = []
    for i, book in enumerate(books, 1):
        print(f"[{i}/{len(books)}] {book}")
        r = test_book(
            ser,
            book,
            args.pages,
            args.delay,
            args.verbose,
            args.heap,
            args.button_timeout,
        )
        results.append(r)
        print()
        time.sleep(0.5)

    ser.close()

    # Summary.
    COL = 65
    print("=" * COL)
    print("SUMMARY")
    print("=" * COL)
    hdr = f"{'Book':<35} {'Status':<16} {'Pages':>5}"
    if args.heap:
        hdr += f"  {'Open cost':>9}  {'Leak':>7}"
    print(hdr)
    print("-" * COL)

    failed = []
    for r in results:
        pages_str = f"{r.pages_navigated}/{args.pages}"
        line = f"{r.name:<35} {r.status:<16} {pages_str:>5}"
        if args.heap:
            cost = r.heap_cost()
            leak = r.leak()
            cost_str = f"{cost // 1024:+d}KB" if cost is not None else "?"
            leak_str = f"{leak // 1024:+d}KB" if leak is not None else "?"
            line += f"  {cost_str:>9}  {leak_str:>7}"
        print(line)
        if r.status != "OK":
            failed.append(r.name)

    print("-" * COL)
    if failed:
        print(f"\nFAILED ({len(failed)}/{len(results)}): {', '.join(failed)}")
        sys.exit(1)
    else:
        print(f"\nAll {len(results)} book(s) passed.")


if __name__ == "__main__":
    main()
