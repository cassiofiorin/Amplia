#include "wamp/playlist_file.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace wamp {
namespace {

std::string lower(const std::string& text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string extensionOf(const std::string& file) {
  const std::size_t dot = file.find_last_of('.');
  const std::size_t slash = file.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
  return lower(file.substr(dot + 1));
}

bool isRemoteUrl(const std::string& entry) {
  static const char* kSchemes[] = {"http://", "https://", "mms://", "mmsh://", "rtsp://",
                                   "rtmp://", "icy://"};
  for (const char* scheme : kSchemes) {
    if (entry.compare(0, std::char_traits<char>::length(scheme), scheme) == 0) return true;
  }
  return false;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string decodePercent(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hexValue(text[i + 1]);
      const int lo = hexValue(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

std::string encodePath(const std::string& path) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(path.size() + 8);
  for (unsigned char c : path) {
    const bool safe = std::isalnum(c) != 0 || c == '/' || c == '-' || c == '_' || c == '.' ||
                      c == '~' || c == ':' || c == '\\';
    if (safe) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::string trim(const std::string& text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

std::string xmlEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string xmlUnescape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }
    const std::size_t end = text.find(';', i);
    if (end == std::string::npos) {
      out.push_back(text[i]);
      continue;
    }
    const std::string entity = text.substr(i + 1, end - i - 1);
    if (entity == "amp") out.push_back('&');
    else if (entity == "lt") out.push_back('<');
    else if (entity == "gt") out.push_back('>');
    else if (entity == "quot") out.push_back('"');
    else if (entity == "apos") out.push_back('\'');
    else if (!entity.empty() && entity[0] == '#') {
      const int code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                           ? std::stoi(entity.substr(2), nullptr, 16)
                           : std::stoi(entity.substr(1));
      if (code > 0 && code < 0x80) out.push_back(static_cast<char>(code));
    } else {
      out.append(text, i, end - i + 1);
    }
    i = end;
  }
  return out;
}

/// Valor de um atributo dentro de uma tag aberta (`<media src="...">`).
bool attributeOf(const std::string& tag, const char* name, std::string* value) {
  const std::string needle = std::string(name) + "=\"";
  std::size_t at = tag.find(needle);
  char quote = '"';
  if (at == std::string::npos) {
    at = tag.find(std::string(name) + "='");
    quote = '\'';
    if (at == std::string::npos) return false;
    at += std::char_traits<char>::length(name) + 2;
  } else {
    at += needle.size();
  }
  const std::size_t end = tag.find(quote, at);
  if (end == std::string::npos) return false;
  *value = tag.substr(at, end - at);
  return true;
}

std::string resolveEntry(const fs::path& base, const std::string& raw) {
  const std::string path = playlistEntryToPath(raw);
  if (path.empty() || isRemoteUrl(path)) return path;
  fs::path entry(path);
  if (entry.is_absolute() || entry.has_root_name() || base.empty()) {
    return entry.lexically_normal().string();
  }
  return (base / entry).lexically_normal().string();
}

std::string readWhole(const std::string& file, std::string* error) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    if (error) *error = "não abriu " + file;
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  // BOM do Bloco de Notas/Windows: sem descartar, a primeira chave não casa.
  if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
  return text;
}

bool readM3u(const fs::path& base, const std::string& text, std::vector<std::string>* out) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string clean = trim(line);
    if (clean.empty() || clean.front() == '#') continue;
    const std::string entry = resolveEntry(base, clean);
    if (!entry.empty()) out->push_back(entry);
  }
  return !out->empty();
}

bool readPls(const fs::path& base, const std::string& text, std::vector<std::string>* out) {
  // .pls é INI: File1=, Title1=, Length1=… A ordem vem do número, não da linha.
  std::map<int, std::string> files;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const std::string clean = trim(line);
    if (clean.empty() || clean.front() == '[' || clean.front() == ';' ||
        clean.front() == '#') {
      continue;
    }
    const std::size_t eq = clean.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = lower(trim(clean.substr(0, eq)));
    if (key.rfind("file", 0) != 0) continue;
    const std::string number = key.substr(4);
    if (number.empty() ||
        !std::all_of(number.begin(), number.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
      continue;
    }
    files[std::stoi(number)] = trim(clean.substr(eq + 1));
  }
  for (const auto& [index, raw] : files) {
    const std::string entry = resolveEntry(base, raw);
    if (!entry.empty()) out->push_back(entry);
  }
  return !out->empty();
}

bool readTaggedEntries(const fs::path& base, const std::string& text, const char* tag,
                       const char* attribute, std::vector<std::string>* out) {
  const std::string open = std::string("<") + tag;
  std::size_t at = text.find(open);
  while (at != std::string::npos) {
    const std::size_t end = text.find('>', at);
    if (end == std::string::npos) break;
    const std::string element = text.substr(at, end - at + 1);
    std::string value;
    if (attributeOf(element, attribute, &value)) {
      const std::string entry = resolveEntry(base, xmlUnescape(value));
      if (!entry.empty()) out->push_back(entry);
    }
    at = text.find(open, end);
  }
  return !out->empty();
}

std::string base64Name(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool saveM3u(const std::string& file, const std::vector<std::string>& items,
             std::string* error) {
  std::ofstream out(file, std::ios::trunc);
  if (!out) {
    if (error) *error = "não criou " + file;
    return false;
  }
  out << "#EXTM3U\n";
  for (const std::string& item : items) out << item << '\n';
  if (!out) {
    if (error) *error = "falha ao escrever " + file;
    return false;
  }
  return true;
}

bool savePls(const std::string& file, const std::vector<std::string>& items,
             std::string* error) {
  std::ofstream out(file, std::ios::trunc);
  if (!out) {
    if (error) *error = "não criou " + file;
    return false;
  }
  out << "[playlist]\n";
  out << "NumberOfEntries=" << items.size() << "\n\n";
  int index = 1;
  for (const std::string& item : items) {
    out << "File" << index << '=' << item << "\n";
    out << "Title" << index << '=' << base64Name(item) << "\n";
    out << "Length" << index << "=-1\n\n";
    ++index;
  }
  out << "Version=2\n";
  if (!out) {
    if (error) *error = "falha ao escrever " + file;
    return false;
  }
  return true;
}

bool saveB4s(const std::string& file, const std::vector<std::string>& items,
             std::string* error) {
  std::ofstream out(file, std::ios::trunc);
  if (!out) {
    if (error) *error = "não criou " + file;
    return false;
  }
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<WinampXML>\n";
  out << "  <playlist num_entries=\"" << items.size() << "\" label=\"\">\n";
  for (const std::string& item : items) {
    out << "    <entry playstring=\"" << xmlEscape(pathToPlaylistUrl(item))
        << "\" length=\"-1\"/>\n";
  }
  out << "  </playlist>\n";
  out << "</WinampXML>\n";
  if (!out) {
    if (error) *error = "falha ao escrever " + file;
    return false;
  }
  return true;
}

bool saveWpl(const std::string& file, const std::vector<std::string>& items,
             std::string* error) {
  std::ofstream out(file, std::ios::trunc);
  if (!out) {
    if (error) *error = "não criou " + file;
    return false;
  }
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<smil>\n";
  out << "  <head>\n    <title></title>\n  </head>\n";
  out << "  <body>\n    <seq>\n";
  for (const std::string& item : items) {
    out << "      <media src=\"" << xmlEscape(pathToPlaylistUrl(item)) << "\"/>\n";
  }
  out << "    </seq>\n  </body>\n</smil>\n";
  if (!out) {
    if (error) *error = "falha ao escrever " + file;
    return false;
  }
  return true;
}

}  // namespace

std::string playlistEntryToPath(const std::string& entry) {
  std::string text = trim(entry);
  if (text.empty()) return {};
  if (isRemoteUrl(text)) return text;
  // "file:///home/x/a%20b.mp3" -> "/home/x/a b.mp3"; "file:C:\x.mp3" também cai aqui.
  const std::string prefix = "file:";
  if (lower(text).rfind(prefix, 0) == 0) {
    text = text.substr(prefix.size());
    while (text.size() > 1 && text.front() == '/' && text[1] == '/') text.erase(0, 1);
    text = decodePercent(text);
  }
  return text;
}

std::string pathToPlaylistUrl(const std::string& path) {
  if (isRemoteUrl(path)) return path;
  return "file://" + encodePath(path);
}

bool loadPlaylistFile(const std::string& file, std::vector<std::string>* out,
                      std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  const std::string ext = extensionOf(file);
  const fs::path base = fs::path(file).parent_path();
  const std::string text = readWhole(file, error);
  if (text.empty() && error != nullptr && !error->empty()) return false;

  bool ok = false;
  if (ext == "m3u" || ext == "m3u8") ok = readM3u(base, text, out);
  else if (ext == "pls") ok = readPls(base, text, out);
  else if (ext == "b4s") ok = readTaggedEntries(base, text, "entry", "playstring", out);
  else if (ext == "wpl") ok = readTaggedEntries(base, text, "media", "src", out);
  else {
    if (error) *error = "formato de playlist não suportado: ." + ext;
    return false;
  }

  if (!ok) {
    if (error) *error = "nenhuma faixa válida em " + file;
    return false;
  }
  return true;
}

bool savePlaylistFile(const std::string& file, const std::vector<std::string>& items,
                      std::string* error) {
  const std::string ext = extensionOf(file);
  if (ext == "m3u" || ext == "m3u8" || ext.empty()) return saveM3u(file, items, error);
  if (ext == "pls") return savePls(file, items, error);
  if (ext == "b4s") return saveB4s(file, items, error);
  if (ext == "wpl") return saveWpl(file, items, error);
  if (error) *error = "formato de playlist não suportado: ." + ext;
  return false;
}

}  // namespace wamp
