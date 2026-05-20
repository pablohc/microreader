#include "Hyphenation.h"

#include <cstdint>
#include <cstring>

#include "Liang/hyph-de.h"
#include "Liang/hyph-en-us.h"
#include "Liang/liang_hyphenation_patterns.h"

// ---------------------------------------------------------------------------
// Liang hyphenation algorithm (stack-only, no heap allocation)
// ---------------------------------------------------------------------------

#ifndef MAX_WORD_LEN
#define MAX_WORD_LEN 128
#endif

static int compare_pattern_segment(const std::uint8_t* pat, int patlen, const char* seg, int seglen) {
  int len = patlen < seglen ? patlen : seglen;
  for (int i = 0; i < len; ++i) {
    unsigned char a = (unsigned char)pat[i];
    unsigned char b = (unsigned char)seg[i];
    if (a != b)
      return (int)a - (int)b;
  }
  if (patlen < seglen)
    return -1;
  if (patlen > seglen)
    return 1;
  return 0;
}

static int find_pattern_index(const char* seg, int seglen, const HyphenationPatterns& pats) {
  if (pats.count == 0)
    return -1;
  int lo = 0, hi = (int)pats.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    int cmp = compare_pattern_segment(pats.patterns[mid].letters, pats.patterns[mid].letters_len, seg, seglen);
    if (cmp == 0)
      return mid;
    if (cmp < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

static int liang_hyphenate(const char* word, size_t leftmin, size_t rightmin, char boundary_char, size_t* out_positions,
                           int max_positions, const HyphenationPatterns& pats) {
  if (!word)
    return 0;
  int word_len = (int)std::strlen(word);
  if (word_len <= 0)
    return 0;
  if (word_len > MAX_WORD_LEN)
    word_len = MAX_WORD_LEN;

  int M = word_len + 2;
  char ext[MAX_WORD_LEN + 3];
  ext[0] = boundary_char;
  std::memcpy(ext + 1, word, word_len);
  ext[1 + word_len] = boundary_char;
  ext[1 + word_len + 1] = '\0';

  uint8_t H[MAX_WORD_LEN + 3];
  std::memset(H, 0, sizeof(H));

  for (int i = 0; i < M; ++i) {
    for (int j = i + 1; j <= M; ++j) {
      int len = j - i;
      int idx = find_pattern_index(ext + i, len, pats);
      if (idx >= 0) {
        const uint8_t* vals = pats.patterns[idx].values;
        int vlen = pats.patterns[idx].values_len;
        for (int l = 0; l < vlen && (i + l) < (int)sizeof(H); ++l)
          if (H[i + l] < vals[l])
            H[i + l] = vals[l];
      }
    }
  }

  int leftmin_i = (leftmin > (size_t)word_len) ? word_len : static_cast<int>(leftmin);
  int rightmin_i = (rightmin > (size_t)word_len) ? word_len : static_cast<int>(rightmin);

  int count = 0;
  for (int k = 1; k < word_len; ++k) {
    int ext_pos = k + 1;
    if ((H[ext_pos] & 1) && k >= leftmin_i && (word_len - k) >= rightmin_i) {
      if (count < max_positions)
        out_positions[count] = static_cast<size_t>(k);
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace microreader {

int hyphenate_word(const char* word, size_t /*len*/, HyphenationLang lang, size_t* out_positions, int max_positions) {
  if (lang == HyphenationLang::None)
    return 0;
  const HyphenationPatterns& pats = (lang == HyphenationLang::German) ? de_patterns : en_us_patterns;
  return liang_hyphenate(word, 2, 2, '.', out_positions, max_positions, pats);
}

size_t find_hyphen_break(const IFont& font, const char* word_ptr, size_t len, FontStyle style, uint8_t size_pct,
                         HyphenationLang lang, uint16_t avail, bool& out_prefix_has_hyphen) {
  out_prefix_has_hyphen = false;
  if (len == 0 || avail == 0)
    return 0;

  // Prefer breaking at an existing '-' in the token (right-most that fits).
  for (size_t i = len - 1; i > 0; --i) {
    if (word_ptr[i - 1] == '-') {
      uint16_t prefix_w = font.word_width(word_ptr, static_cast<uint16_t>(i), style, size_pct);
      if (prefix_w <= avail) {
        out_prefix_has_hyphen = true;
        return i;
      }
    }
  }

  // Fall back to Liang algorithmic hyphenation.
  if (lang == HyphenationLang::None || len < 6)
    return 0;
  char buf[129];
  size_t copy_len = len < 128 ? len : 128;
  memcpy(buf, word_ptr, copy_len);
  buf[copy_len] = '\0';

  // Strip trailing punctuation before hyphenating so Liang doesn't produce
  // ugly splits like "befördert-|." where the suffix is just punctuation.
  // Add new entries to either table to extend the set of stripped characters.
  static const char kStripAscii[] = ".,!?:;)]\"'";  // single ASCII bytes to strip
  struct MultiByteSeq {
    unsigned char b0, b1;
  };
  static const MultiByteSeq kStripMulti[] = {
      {0xC2, 0xBB}, // » U+00BB RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
      {0xC2, 0xAB}, // « U+00AB LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
  };
  size_t hyph_len = copy_len;
  while (hyph_len > 0) {
    bool stripped = false;
    for (const auto& seq : kStripMulti) {
      if (hyph_len >= 2 && (unsigned char)buf[hyph_len - 2] == seq.b0 && (unsigned char)buf[hyph_len - 1] == seq.b1) {
        hyph_len -= 2;
        stripped = true;
        break;
      }
    }
    if (!stripped) {
      unsigned char c = (unsigned char)buf[hyph_len - 1];
      if (c < 0x80 && std::strchr(kStripAscii, c)) {
        --hyph_len;
      } else {
        break;
      }
    }
  }
  buf[hyph_len] = '\0';

  // Count UTF-8 code points in a byte range (continuation bytes 0x80–0xBF don't start a codepoint).
  auto utf8_codepoint_count = [](const char* s, size_t byte_len) -> int {
    int n = 0;
    for (size_t i = 0; i < byte_len; ++i)
      if (((unsigned char)s[i] & 0xC0) != 0x80)
        ++n;
    return n;
  };

  static constexpr int kMinCharsEachSide = 2;

  size_t positions[32];
  int n = hyphenate_word(buf, hyph_len, lang, positions, 32);
  if (n <= 0)
    return 0;
  const uint16_t hyphen_w = font.char_width('-', style, size_pct);
  for (int i = n - 1; i >= 0; --i) {
    size_t pos = positions[i];
    if (pos == 0 || pos >= len)
      continue;
    // Enforce leftmin/rightmin in Unicode code points, not bytes.
    // Without this, a 2-byte character like ß counts as 2 bytes remaining
    // and passes rightmin=2 even though only 1 character follows.
    // Use hyph_len (stripped word) for the suffix check so trailing punctuation
    // that was stripped (e.g. the comma in "weiß,") doesn't inflate the count.
    if (utf8_codepoint_count(word_ptr, pos) < kMinCharsEachSide)
      continue;
    size_t suffix_byte_len = pos < hyph_len ? hyph_len - pos : 0;
    if (utf8_codepoint_count(word_ptr + pos, suffix_byte_len) < kMinCharsEachSide)
      continue;
    uint16_t prefix_w = font.word_width(word_ptr, static_cast<uint16_t>(pos), style, size_pct);
    bool ends_with_hyphen = (word_ptr[pos - 1] == '-');
    uint16_t extra = ends_with_hyphen ? 0 : hyphen_w;
    if (prefix_w + extra <= avail) {
      out_prefix_has_hyphen = ends_with_hyphen;
      return pos;
    }
  }
  return 0;
}

HyphenationLang detect_language(const std::optional<std::string>& lang_tag) {
  if (!lang_tag)
    return HyphenationLang::None;

  std::string_view sv = *lang_tag;

  auto ieq = [](std::string_view s, const char* expected) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
      ++i;
    }
    for (size_t j = 0; expected[j]; ++j) {
      if (i >= s.size())
        return false;
      char c = s[i++];
      if (c >= 'A' && c <= 'Z')
        c += 32;
      if (c != expected[j])
        return false;
    }
    if (i < s.size()) {
      char c = s[i];
      if (c != '-' && c != ' ' && c != '\t' && c != '\n' && c != '\r')
        return false;
    }
    return true;
  };

  if (ieq(sv, "de") || ieq(sv, "ger") || ieq(sv, "deu"))
    return HyphenationLang::German;
  if (ieq(sv, "en") || ieq(sv, "eng"))
    return HyphenationLang::English;

  return HyphenationLang::None;
}

}  // namespace microreader
