#include "wamp/work_key.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace wamp {
namespace {

// ---------------------------------------------------------------------------------------------
// UTF-8 ↔ pontos de código
// ---------------------------------------------------------------------------------------------

std::vector<char32_t> decodeUtf8(const std::string& text) {
  std::vector<char32_t> out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    char32_t code = 0;
    std::size_t extra = 0;
    if (lead < 0x80) {
      code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      code = lead & 0x1F;
      extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      code = lead & 0x0F;
      extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
      code = lead & 0x07;
      extra = 3;
    } else {
      // Byte inválido: entra como '?' em vez de derrubar a chave.
      out.push_back('?');
      ++i;
      continue;
    }
    if (i + extra >= text.size()) {
      out.push_back('?');
      break;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char cont = static_cast<unsigned char>(text[i + k]);
      if ((cont & 0xC0) != 0x80) {
        code = '?';
        break;
      }
      code = (code << 6) | (cont & 0x3F);
    }
    out.push_back(code);
    i += extra + 1;
  }
  return out;
}

void appendUtf8(std::string& out, char32_t code) {
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

/// Marca combinante (acento solto). Cobre os blocos que aparecem em título de música.
bool isCombiningMark(char32_t code) {
  return (code >= 0x0300 && code <= 0x036F) || (code >= 0x1AB0 && code <= 0x1AFF) ||
         (code >= 0x1DC0 && code <= 0x1DFF) || (code >= 0x20D0 && code <= 0x20F0) ||
         (code >= 0xFE20 && code <= 0xFE2F);
}

/// Pontuação "de teclado de editor" → ASCII.
///
/// O MusicBrainz escreve `Don’t Do Me Any Favours` com apóstrofo tipográfico, e o arquivo costuma
/// ter o reto: sem dobrar isso, dois títulos iguais não casam e a busca por disco falha em
/// silêncio. Vale para aspas, travessões e reticências — todos com o mesmo som.
bool foldPunctuation(char32_t code, std::string& out) {
  switch (code) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: out += '\''; return true;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: out += '"'; return true;
    case 0x2013: case 0x2014: case 0x2015: case 0x2212: out += '-'; return true;
    case 0x2026: out += "..."; return true;
    case 0x00AB: case 0x00BB: out += '"'; return true;
    default: return false;
  }
}

/// Letra precomposta (Latin-1 Supplement e Latin Extended-A) → letra base.
///
/// É o pedido do `norm/v1` ("NFD, sem acentos") sem arrastar ICU: cobre o latim que aparece em
/// tag de música ocidental. Outros alfabetos passam como estão — quando isso importar, a regra
/// muda de versão. Devolve false quando o ponto de código não precisa de decomposição.
bool decomposeLatin(char32_t code, std::string& out) {
  if (code < 0x00C0) return false;  // ASCII e Latin-1 baixo seguem direto.

  if (code <= 0x00FF) {
    const char* base = nullptr;
    switch (code) {
      case 0x00C6: case 0x00E6: base = "ae"; break;
      case 0x00D0: case 0x00F0: base = "d"; break;
      case 0x00D8: case 0x00F8: base = "o"; break;
      case 0x00DE: case 0x00FE: base = "th"; break;
      case 0x00DF: base = "ss"; break;
      default: break;
    }
    if (base != nullptr) {
      out += base;
      return true;
    }
    if ((code >= 0x00C0 && code <= 0x00C5) || (code >= 0x00E0 && code <= 0x00E5)) { out += 'a'; return true; }
    if (code == 0x00C7 || code == 0x00E7) { out += 'c'; return true; }
    if ((code >= 0x00C8 && code <= 0x00CB) || (code >= 0x00E8 && code <= 0x00EB)) { out += 'e'; return true; }
    if ((code >= 0x00CC && code <= 0x00CF) || (code >= 0x00EC && code <= 0x00EF)) { out += 'i'; return true; }
    if (code == 0x00D1 || code == 0x00F1) { out += 'n'; return true; }
    if ((code >= 0x00D2 && code <= 0x00D6) || (code >= 0x00F2 && code <= 0x00F6)) { out += 'o'; return true; }
    if ((code >= 0x00D9 && code <= 0x00DC) || (code >= 0x00F9 && code <= 0x00FC)) { out += 'u'; return true; }
    if (code == 0x00DD || code == 0x00FD || code == 0x00FF) { out += 'y'; return true; }
    return false;
  }

  if (code >= 0x0100 && code <= 0x017F) {
    struct Range { char32_t first, last; const char* base; };
    static constexpr Range kRanges[] = {
        {0x0100, 0x0105, "a"}, {0x0106, 0x010D, "c"}, {0x010E, 0x0111, "d"},
        {0x0112, 0x011B, "e"}, {0x011C, 0x0123, "g"}, {0x0124, 0x0127, "h"},
        {0x0128, 0x0131, "i"}, {0x0132, 0x0133, "ij"}, {0x0134, 0x0135, "j"},
        {0x0136, 0x0138, "k"}, {0x0139, 0x0142, "l"}, {0x0143, 0x014B, "n"},
        {0x014C, 0x0153, "o"}, {0x0154, 0x0159, "r"}, {0x015A, 0x0161, "s"},
        {0x0162, 0x0167, "t"}, {0x0168, 0x0173, "u"}, {0x0174, 0x0177, "w"},
        {0x0178, 0x0178, "y"}, {0x0179, 0x017E, "z"}, {0x017F, 0x017F, "s"},
    };
    for (const Range& range : kRanges) {
      if (code >= range.first && code <= range.last) {
        out += range.base;
        return true;
      }
    }
    if (code == 0x0149) { out += 'n'; return true; }  // ŉ
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Limpeza comum
// ---------------------------------------------------------------------------------------------

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/// Caixa baixa, sem acento, espaços colapsados (regras 1 e 2).
std::string lowerWithoutAccents(const std::string& text) {
  std::string flat;
  flat.reserve(text.size());
  for (const char32_t code : decodeUtf8(text)) {
    if (isCombiningMark(code)) continue;
    if (decomposeLatin(code, flat)) continue;
    if (foldPunctuation(code, flat)) continue;
    if (code >= 'A' && code <= 'Z') {
      flat.push_back(static_cast<char>(code - 'A' + 'a'));
      continue;
    }
    if (code > 0x7F) {
      appendUtf8(flat, code);
      continue;
    }
    flat.push_back(static_cast<char>(code));
  }

  std::string out;
  out.reserve(flat.size());
  bool pendingSpace = false;
  for (const char c : flat) {
    if (isSpace(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }
  return out;
}

std::string trim(const std::string& text) {
  std::size_t first = 0;
  while (first < text.size() && isSpace(text[first])) ++first;
  std::size_t last = text.size();
  while (last > first && isSpace(text[last - 1])) --last;
  return text.substr(first, last - first);
}

/// Sufixos que **saem** da chave por serem a mesma obra em outra tiragem (regra 5).
bool isRemovableSuffix(const std::string& value) {
  static const char* const kSuffixes[] = {
      "remaster", "remastered", "remasterizado", "remasterizada", "deluxe", "deluxe edition",
      "bonus", "bonus track", "mono", "stereo", "explicit", "clean", "clean version",
  };
  for (const char* suffix : kSuffixes) {
    if (value == suffix) return true;
    // "(Remastered 2011)": o ano não muda a obra.
    if (value.compare(0, std::char_traits<char>::length(suffix), suffix) == 0 &&
        (suffix[0] == 'r') && value.size() > std::char_traits<char>::length(suffix)) {
      const std::string rest = trim(value.substr(std::char_traits<char>::length(suffix)));
      if (!rest.empty() && rest.size() <= 4 && std::all_of(rest.begin(), rest.end(), [](char c) {
            return c >= '0' && c <= '9';
          })) {
        return true;
      }
    }
  }
  return false;
}

/// Controle que abre e fecha um grupo final: "(...)" ou "[...]".
bool stripTrailingGroup(std::string& text) {
  if (text.size() < 3) return false;
  const char last = text.back();
  const char open = last == ')' ? '(' : (last == ']' ? '[' : '\0');
  if (open == '\0') return false;
  const std::size_t at = text.rfind(open);
  if (at == std::string::npos || at == 0) return false;
  const std::string inner = trim(text.substr(at + 1, text.size() - at - 2));
  if (!isRemovableSuffix(inner)) return false;
  text = trim(text.substr(0, at));
  return true;
}

/// Cauda depois de " - ": só sai se for a mesma obra em outra tiragem.
bool stripTrailingDash(std::string& text) {
  const std::size_t at = text.rfind(" - ");
  if (at == std::string::npos || at == 0) return false;
  const std::string tail = trim(text.substr(at + 3));
  if (!isRemovableSuffix(tail)) return false;
  text = trim(text.substr(0, at));
  return true;
}

/// "01 ", "1. ", "02-", "03_ " no começo do título (regra 3).
bool stripLeadingTrackNumber(std::string& text) {
  std::size_t digits = 0;
  while (digits < text.size() && digits < 3 && text[digits] >= '0' && text[digits] <= '9') ++digits;
  if (digits == 0) return false;
  std::size_t at = digits;
  bool separator = false;
  while (at < text.size() && (text[at] == ' ' || text[at] == '.' || text[at] == '-' || text[at] == '_' ||
                              text[at] == ')')) {
    separator = true;
    ++at;
  }
  if (!separator || at >= text.size()) return false;
  text = trim(text.substr(at));
  return true;
}

/// "cd1", "cd 2", "disc 3", "disco 2" no fim do álbum, com ou sem parênteses (regra 4).
bool isDiscMarker(const std::string& inner) {
  static const char* const kMarkers[] = {"cd", "disc", "disco"};
  for (const char* marker : kMarkers) {
    const std::size_t size = std::char_traits<char>::length(marker);
    if (inner.compare(0, size, marker) != 0) continue;
    std::string rest = trim(inner.substr(size));
    if (rest.empty()) continue;
    if (std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; })) return true;
  }
  return false;
}

bool stripDiscMarker(std::string& text) {
  static const char* const kMarkers[] = {"cd", "disc", "disco"};
  bool changed = false;

  // "(CD1)", "[disc 2]" no fim do álbum.
  for (;;) {
    if (text.size() < 3) break;
    const char last = text.back();
    const char open = last == ')' ? '(' : (last == ']' ? '[' : '\0');
    if (open == '\0') break;
    const std::size_t at = text.rfind(open);
    if (at == std::string::npos || at == 0) break;
    if (!isDiscMarker(trim(text.substr(at + 1, text.size() - at - 2)))) break;
    text = trim(text.substr(0, at));
    changed = true;
  }

  for (;;) {
    bool removed = false;
    // Forma solta no fim: "album cd1", "album (disc 2)".
    if (text.size() >= 2) {
      std::size_t digits = text.size();
      while (digits > 0 && text[digits - 1] >= '0' && text[digits - 1] <= '9') --digits;
      if (digits < text.size()) {
        std::size_t end = digits;
        while (end > 0 && text[end - 1] == ' ') --end;
        for (const char* marker : kMarkers) {
          const std::size_t size = std::char_traits<char>::length(marker);
          if (end >= size && text.compare(end - size, size, marker) == 0) {
            std::size_t cut = end - size;
            // Consome parênteses/colchetes que envolviam o marcador.
            while (cut > 0 && (text[cut - 1] == '(' || text[cut - 1] == '[')) --cut;
            std::size_t begin = cut;
            while (begin > 0 && text[begin - 1] == ' ') --begin;
            text = trim(text.substr(0, begin));
            removed = true;
            break;
          }
        }
      }
    }
    if (!removed) break;
    changed = true;
  }
  return changed;
}

/// Convidado sai do título, e não entra no artista (regra 6).
void stripFeatured(std::string& text) {
  static const char* const kWords[] = {"feat.", "feat ", "ft.", "ft ", "featuring "};
  // Dentro de parênteses/colchetes: remove o grupo inteiro.
  for (const char* word : kWords) {
    for (const char open : {'(', '['}) {
      std::size_t from = 0;
      while (from < text.size()) {
        const std::size_t group = text.find(open, from);
        if (group == std::string::npos) break;
        const char closeChar = open == '(' ? ')' : ']';
        const std::size_t closeAt = text.find(closeChar, group);
        if (closeAt == std::string::npos) break;
        const std::string inner = text.substr(group + 1, closeAt - group - 1);
        if (inner.find(word) != std::string::npos) {
          std::size_t begin = group;
          while (begin > 0 && text[begin - 1] == ' ') --begin;
          std::size_t end = closeAt + 1;
          while (end < text.size() && text[end] == ' ') ++end;
          text = trim(text.substr(0, begin) + " " + text.substr(end));
          from = begin;
        } else {
          from = closeAt + 1;
        }
      }
    }
  }
  // Fora de parêntese: "song feat. fulano" → corta na palavra.
  const std::size_t at = text.find(" feat.");
  const std::size_t at2 = text.find(" ft.");
  const std::size_t at3 = text.find(" featuring ");
  std::size_t cut = std::string::npos;
  for (const std::size_t candidate : {at, at2, at3}) {
    if (candidate != std::string::npos && (cut == std::string::npos || candidate < cut)) cut = candidate;
  }
  if (cut != std::string::npos) text = trim(text.substr(0, cut));
}

void stripTrailingPunctuation(std::string& text) {
  while (!text.empty()) {
    const char last = text.back();
    if (last == '.' || last == ',' || last == ';' || last == ':' || last == '!') {
      text.pop_back();
      continue;
    }
    break;
  }
  text = trim(text);
}

}  // namespace

std::string normalizeWorkField(WorkField field, const std::string& value) {
  std::string text = lowerWithoutAccents(value);
  switch (field) {
    case WorkField::Title:
      stripLeadingTrackNumber(text);
      stripFeatured(text);
      // "(Remastered)", "[Deluxe Edition]", " - Bonus Track" — de dentro para fora.
      while (stripTrailingGroup(text) || stripTrailingDash(text)) {
      }
      break;
    case WorkField::Album:
      stripDiscMarker(text);
      while (stripTrailingGroup(text) || stripTrailingDash(text)) {
      }
      break;
    case WorkField::Artist:
      break;
  }
  stripTrailingPunctuation(text);
  return trim(text);
}

std::string workKey(const std::string& artist, const std::string& album, const std::string& title) {
  std::string text = normalizeWorkField(WorkField::Artist, artist);
  text.push_back('\x1f');
  text += normalizeWorkField(WorkField::Album, album);
  text.push_back('\x1f');
  text += normalizeWorkField(WorkField::Title, title);
  return sha1Hex(text);
}

int workDurationSeconds(std::int64_t durationMs) {
  if (durationMs <= 0) return 0;
  return static_cast<int>(durationMs / 1000);
}

// ---------------------------------------------------------------------------------------------
// SHA-1 (FIPS 180-1) — 40 bytes de estado, sem dependência.
// ---------------------------------------------------------------------------------------------

std::string sha1Hex(const std::string& data) {
  std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476,
                h4 = 0xC3D2E1F0;

  std::string message = data;
  const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
  message.push_back(static_cast<char>(0x80));
  while (message.size() % 64 != 56) message.push_back('\0');
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<char>((bitLength >> shift) & 0xFF));
  }

  const auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };

  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(message.data() + chunk + i * 4);
      w[i] = (static_cast<std::uint32_t>(bytes[0]) << 24) |
             (static_cast<std::uint32_t>(bytes[1]) << 16) |
             (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f = 0, k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  const std::array<std::uint32_t, 5> state{h0, h1, h2, h3, h4};
  std::string hex;
  hex.reserve(40);
  for (const std::uint32_t word : state) {
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08x", word);
    hex.append(buffer, 8);
  }
  return hex;
}

std::string asciiDashes(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  std::size_t i = 0;
  while (i < value.size()) {
    // Os quatro hífens tipográficos vivem no BMP: 3 bytes em UTF-8, prefixo E2 80.
    const unsigned char first = static_cast<unsigned char>(value[i]);
    if (first == 0xE2 && i + 2 < value.size() &&
        static_cast<unsigned char>(value[i + 1]) == 0x80) {
      const unsigned char third = static_cast<unsigned char>(value[i + 2]);
      if (third == 0x90 || third == 0x91 || third == 0x92 || third == 0x93 || third == 0x94) {
        out.push_back('-');
        i += 3;
        continue;
      }
    }
    out.push_back(value[i]);
    ++i;
  }
  return out;
}

}  // namespace wamp
