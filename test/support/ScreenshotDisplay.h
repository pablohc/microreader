#pragma once

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "microreader/display/DrawBuffer.h"

// Lightweight IDisplay that does nothing on refresh.
// Provides save_image() to capture the current DrawBuffer frame as a PNG.
//
// Usage:
//   ScreenshotDisplay display;
//   DrawBuffer buf(display);
//   screen.start(buf);
//   ScreenshotDisplay::save_image(buf, "page_1.png");
class ScreenshotDisplay final : public microreader::IDisplay {
 public:
  void full_refresh(const uint8_t*, microreader::RefreshMode, bool) override {}
  void partial_refresh(const uint8_t*, const uint8_t*) override {}

  // Save the current render buffer as a 1-bpp uncompressed PNG (logical portrait orientation).
  // Uses deflate STORE blocks (no compression) — ~10x faster than stb for large books.
  // File size is ~50KB/page; binary B&W text is already sparse at 1 bit per pixel.
  static bool save_image(microreader::DrawBuffer& buf, const std::string& path) {
    const int W = microreader::DrawBuffer::kWidth;
    const int H = microreader::DrawBuffer::kHeight;
    const int STRIDE = microreader::DisplayFrame::kStride;
    const int PHYS_H = microreader::DisplayFrame::kPhysicalHeight;
    const uint8_t* phys = buf.render_buf();
    const int ROW_BYTES = (W + 7) / 8;

    // Build raw PNG scanlines: 1 filter byte (None=0) + ROW_BYTES of 1-bpp pixels per row.
    // Rotation: logical (x, y) → physical (phys_x=y, phys_y=PHYS_H-1-x).
    const size_t SCAN = 1 + (size_t)ROW_BYTES;
    std::vector<uint8_t> raw((size_t)H * SCAN);
    for (int y = 0; y < H; ++y) {
      uint8_t* row = raw.data() + (size_t)y * SCAN;
      row[0] = 0;
      const int col_byte = y / 8;
      const int col_shift = 7 - (y & 7);
      // W=480 is an exact multiple of 8, so all x values are valid — no bounds check needed.
      for (int bx = 0; bx < ROW_BYTES; ++bx) {
        uint8_t out = 0;
        for (int b = 0; b < 8; ++b) {
          const int py = PHYS_H - 1 - (bx * 8 + b);
          out |= (uint8_t)(((phys[py * STRIDE + col_byte] >> col_shift) & 1u) << (7 - b));
        }
        row[1 + bx] = out;
      }
    }

    // Adler32 over raw scanline data (zlib trailer).
    // Lazy reduction: accumulate up to NMAX=5552 bytes before each modulo (per zlib spec).
    uint32_t s1 = 1, s2 = 0;
    {
      const uint8_t* p = raw.data();
      size_t n = raw.size();
      while (n > 0) {
        size_t chunk = n < 5552 ? n : 5552;
        n -= chunk;
        while (chunk--) {
          s1 += *p++;
          s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
      }
    }

    // Pack raw data into deflate STORE blocks (BTYPE=00, no compression)
    const size_t RAW = raw.size();
    const size_t BMAX = 65535;
    const size_t nblocks = (RAW + BMAX - 1) / BMAX;
    std::vector<uint8_t> idat;
    idat.reserve(2 + nblocks * 5 + RAW + 4);
    idat.push_back(0x78);
    idat.push_back(0x01);  // zlib CMF/FLG
    const uint8_t* src = raw.data();
    size_t rem = RAW;
    while (rem > 0) {
      const size_t blen = rem < BMAX ? rem : BMAX;
      const auto len16 = (uint16_t)blen;
      const uint16_t nlen = ~len16;
      idat.push_back(rem == blen ? 0x01u : 0x00u);  // BFINAL | BTYPE=00
      idat.push_back(len16 & 0xFF);
      idat.push_back(len16 >> 8);
      idat.push_back(nlen & 0xFF);
      idat.push_back(nlen >> 8);
      idat.insert(idat.end(), src, src + blen);
      src += blen;
      rem -= blen;
    }
    const uint32_t adler = (s2 << 16) | s1;
    idat.push_back(adler >> 24);
    idat.push_back((adler >> 16) & 0xFF);
    idat.push_back((adler >> 8) & 0xFF);
    idat.push_back(adler & 0xFF);

    // PNG helpers
    auto be32 = [](uint8_t* p, uint32_t v) {
      p[0] = v >> 24;
      p[1] = (v >> 16) & 0xFF;
      p[2] = (v >> 8) & 0xFF;
      p[3] = v & 0xFF;
    };
    // CRC32 lookup table — built once, thread-safe (C++11 static init guarantee).
    static const auto crc_tab = []() {
      std::array<uint32_t, 256> t{};
      for (int n = 0; n < 256; ++n) {
        uint32_t c = (uint32_t)n;
        for (int k = 0; k < 8; ++k)
          c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        t[n] = c;
      }
      return t;
    }();
    auto crc32_chunk = [&](const char* type4, const uint8_t* data, uint32_t dlen) -> uint32_t {
      uint32_t c = 0xFFFFFFFFu;
      for (int i = 0; i < 4; ++i)
        c = (c >> 8) ^ crc_tab[(c ^ (uint8_t)type4[i]) & 0xFF];
      for (uint32_t i = 0; i < dlen; ++i)
        c = (c >> 8) ^ crc_tab[(c ^ data[i]) & 0xFF];
      return c ^ 0xFFFFFFFFu;
    };

    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
      return false;

    auto write_chunk = [&](const char* type, const uint8_t* data, uint32_t dlen) {
      uint8_t tmp[4];
      be32(tmp, dlen);
      fwrite(tmp, 1, 4, f);
      fwrite(type, 1, 4, f);
      if (dlen)
        fwrite(data, 1, dlen, f);
      be32(tmp, crc32_chunk(type, data, dlen));
      fwrite(tmp, 1, 4, f);
    };

    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(SIG, 1, 8, f);

    uint8_t ihdr[13];
    be32(ihdr + 0, (uint32_t)W);
    be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 1;
    ihdr[9] = 0;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk("IHDR", ihdr, 13);
    write_chunk("IDAT", idat.data(), (uint32_t)idat.size());
    write_chunk("IEND", nullptr, 0);
    fclose(f);
    return true;
  }
};
