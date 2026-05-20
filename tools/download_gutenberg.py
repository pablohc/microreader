#!/usr/bin/env python3
"""
Download EPUB files from Project Gutenberg.
Uses direct download URLs from www.gutenberg.org (no aleph mirror).
Respects 2-second delay policy: https://www.gutenberg.org/policy/robot_access.html

Usage:
    python download_gutenberg.py [--count 50] [--output ./books/gutenberg]
"""

import argparse
import re
import time
import urllib.request
import urllib.error
from html.parser import HTMLParser
from pathlib import Path


# Well-known popular Gutenberg book IDs (mix of genres, sizes, with/without images)
POPULAR_IDS = [
    1342,  # Pride and Prejudice
    11,  # Alice's Adventures in Wonderland
    1661,  # Sherlock Holmes
    84,  # Frankenstein
    1952,  # The Yellow Wallpaper
    98,  # A Tale of Two Cities
    2701,  # Moby Dick
    1080,  # A Modest Proposal
    174,  # The Picture of Dorian Gray
    16328,  # Beowulf
    345,  # Dracula
    5200,  # Metamorphosis
    2600,  # War and Peace
    1260,  # Jane Eyre
    46,  # A Christmas Carol
    76,  # Adventures of Huckleberry Finn
    74,  # Adventures of Tom Sawyer
    1232,  # The Prince
    219,  # Heart of Darkness
    2542,  # A Doll's House
    1400,  # Great Expectations
    25344,  # The Scarlet Letter
    1184,  # The Count of Monte Cristo
    120,  # Treasure Island
    44881,  # A Room with a View
    16,  # Peter Pan
    6130,  # The Iliad
    58585,  # The Brothers Karamazov
    3207,  # Leviathan
    36,  # The War of the Worlds
    43,  # The Strange Case of Dr. Jekyll and Mr. Hyde
    55,  # The Wonderful Wizard of Oz
    1497,  # Republic (Plato)
    514,  # Little Women
    4300,  # Ulysses
    100,  # Complete Works of Shakespeare
    1727,  # The Odyssey
    160,  # The Awakening
    2591,  # Grimms' Fairy Tales
    205,  # Walden
    768,  # Wuthering Heights
    1998,  # Thus Spake Zarathustra
    996,  # Don Quixote
    730,  # Oliver Twist
    1250,  # Anthem
    28054,  # The Brothers Grimm
    3600,  # Essays of Michel de Montaigne
    2500,  # Siddhartha
    8800,  # The Divine Comedy
    2852,  # The Hound of the Baskervilles
    5740,  # Tractatus Logico-Philosophicus
    161,  # Sense and Sensibility
    2554,  # Crime and Punishment
    1399,  # Anna Karenina
    28885,  # The Federalist Papers
    1023,  # Bleak House
    244,  # A Study in Scarlet
    35,  # The Time Machine
    135,  # Les Misérables
    4363,  # Pygmalion
    158,  # Emma
    1259,  # Twenty Thousand Leagues Under the Seas
    779,  # The Trial
    408,  # The Souls of Black Folk
    1951,  # The Food of the Gods
    5827,  # The Problems of Philosophy
    41,  # The Legend of Sleepy Hollow
    2814,  # Dubliners
    33,  # The Scarlet Pimpernel
    2097,  # The Sign of the Four
    544,  # Anne of Green Gables
    23,  # Narrative of Frederick Douglass
    61,  # The Communist Manifesto
    103,  # Around the World in 80 Days
    45,  # Anne of the Island
    829,  # Gulliver's Travels
    1661,  # Adventures of Sherlock Holmes
    30254,  # The Romance of Lust
    2148,  # The Works of Edgar Allan Poe Volume 2
    215,  # The Call of the Wild
    236,  # The Jungle Book
    3825,  # Psychopathia Sexualis
    27827,  # The Kama Sutra
    69087,  # The Art of War (Sun Tzu)
    14264,  # Paradise Lost
    42324,  # Frankenstein (1818 text)
    1518,  # The Complete Poetical Works of Percy Bysshe Shelley
    2680,  # Meditations
    2097,  # The Sign of Four
    4517,  # Ethan Frome
    2147,  # The Works of Edgar Allan Poe Volume 1
    10007,  # Twelfth Night
    5197,  # My Life and Work
    1934,  # Winesburg, Ohio
]


class CatalogPageParser(HTMLParser):
    """Parse a Gutenberg catalog/search page to extract book IDs."""

    def __init__(self):
        super().__init__()
        self.book_ids = []

    def handle_starttag(self, tag, attrs):
        if tag != "a":
            return
        href = dict(attrs).get("href", "")
        m = re.match(r"/ebooks/(\d+)$", href)
        if m:
            self.book_ids.append(int(m.group(1)))


def download_file(url, dest_path, retries=3):
    """Download a file with retries."""
    headers = {
        "User-Agent": "MicroreaderTestDownloader/1.0 (epub test corpus; contact: github)"
    }
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=60) as resp:
                data = resp.read()
                if len(data) < 100:
                    # Too small, probably an error page
                    return 0
                with open(dest_path, "wb") as f:
                    f.write(data)
                return len(data)
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
            if attempt < retries - 1:
                print(f"    Retry {attempt + 1}/{retries}: {e}")
                time.sleep(3)
    return 0


def fetch_page(url, retries=3):
    """Fetch a URL with retries."""
    headers = {
        "User-Agent": "MicroreaderTestDownloader/1.0 (epub test corpus; contact: github)"
    }
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=30) as resp:
                return resp.read().decode("utf-8", errors="replace")
        except (urllib.error.URLError, TimeoutError) as e:
            if attempt < retries - 1:
                print(f"    Retry {attempt + 1}/{retries}: {e}")
                time.sleep(5)
    return None


def get_book_ids_from_catalog(start_page=1, max_pages=20, existing_count=0, target=100):
    """Scrape book IDs from the Gutenberg catalog sorted by popularity."""
    ids = []
    for page in range(start_page, start_page + max_pages):
        if len(ids) + existing_count >= target:
            break
        offset = (page - 1) * 25 + 1
        url = f"https://www.gutenberg.org/ebooks/search/?sort_order=downloads&start_index={offset}"
        print(f"  Fetching catalog page {page} (offset {offset})...")
        time.sleep(2)
        html = fetch_page(url)
        if not html:
            print("    Failed to fetch catalog page, stopping.")
            break
        parser = CatalogPageParser()
        parser.feed(html)
        new_ids = [bid for bid in parser.book_ids if bid not in ids]
        ids.extend(new_ids)
        print(f"    Found {len(new_ids)} book IDs (total: {len(ids)})")
        if len(new_ids) == 0:
            break
    return ids


def main():
    parser = argparse.ArgumentParser(description="Download Gutenberg EPUBs for testing")
    parser.add_argument(
        "--count", type=int, default=50, help="Number of EPUBs to download"
    )
    parser.add_argument("--output", type=str, default=None, help="Output directory")
    parser.add_argument(
        "--catalog",
        action="store_true",
        help="Scrape catalog for more IDs beyond the built-in popular list",
    )
    args = parser.parse_args()

    if args.output:
        output_dir = Path(args.output)
    else:
        output_dir = Path(__file__).parent.parent / "test" / "books" / "gutenberg"

    output_dir.mkdir(parents=True, exist_ok=True)

    # Deduplicate IDs
    book_ids = list(dict.fromkeys(POPULAR_IDS))

    # If we need more than our built-in list, scrape the catalog
    if args.catalog or args.count > len(book_ids):
        print("Fetching additional book IDs from Gutenberg catalog...")
        extra = get_book_ids_from_catalog(
            existing_count=len(book_ids), target=args.count
        )
        for bid in extra:
            if bid not in book_ids:
                book_ids.append(bid)

    existing = set(f.stem.split("-")[0] for f in output_dir.glob("*.epub"))
    print(f"Output directory: {output_dir}")
    print(f"Already downloaded: {len(existing)} EPUBs")
    print(f"Target: {args.count} EPUBs")
    print(f"Book IDs available: {len(book_ids)}")
    print()

    downloaded = len([f for f in output_dir.glob("*.epub")])
    total_bytes = 0
    errors = 0
    skipped = 0

    for book_id in book_ids:
        if downloaded >= args.count:
            break

        filename = f"pg{book_id}-images.epub"
        tag = f"pg{book_id}"

        if any(tag in e for e in existing):
            skipped += 1
            continue

        # Direct download URL from www.gutenberg.org
        url = f"https://www.gutenberg.org/cache/epub/{book_id}/{filename}"
        dest = output_dir / filename

        time.sleep(2)  # Respect delay policy

        size = download_file(url, str(dest))
        if size > 0:
            downloaded += 1
            total_bytes += size
            existing.add(tag)
            print(f"  [{downloaded}/{args.count}] {filename} ({size/1024:.0f} KB)")
        else:
            # Try without images
            filename_noimages = f"pg{book_id}.epub"
            url2 = f"https://www.gutenberg.org/cache/epub/{book_id}/{filename_noimages}"
            dest2 = output_dir / filename_noimages
            time.sleep(2)
            size = download_file(url2, str(dest2))
            if size > 0:
                downloaded += 1
                total_bytes += size
                existing.add(tag)
                print(
                    f"  [{downloaded}/{args.count}] {filename_noimages} ({size/1024:.0f} KB)"
                )
            else:
                errors += 1
                print(f"  FAILED: pg{book_id}")

    print()
    print(f"=== Done ===")
    print(f"Downloaded: {downloaded} EPUBs")
    print(f"Skipped (existing): {skipped}")
    print(f"Errors: {errors}")
    print(f"Total size: {total_bytes / (1024*1024):.1f} MB")
    print(f"Output: {output_dir}")


if __name__ == "__main__":
    main()
