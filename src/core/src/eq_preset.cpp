#include "wamp/eq_preset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <vector>

namespace wamp {
namespace {

/// Assinatura da biblioteca binária do Winamp (o próprio EQ grava assim).
constexpr char kBinarySignature[] = "Winamp EQ library file v1.1\x1a!--";
// O registro é `_MAX_FNAME + 1` bytes de nome, 10 de bandas e o pré-amp. O valor de
// `_MAX_FNAME` muda conforme quem gravou o arquivo (o Winamp 5 escreve 257+10+1,
// 268 bytes; a variante com pré-amp de 4 bytes também aparece por aí), então o
// tamanho do registro é descoberto dividindo o que sobrou da assinatura.


std::string trim(std::string text) {
  const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
  text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
  return text;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

/// O Winamp guarda a posição do slider (0..63), não o dB: 31 é o meio, e a
/// escala é a mesma dos 12 dB para cada lado — corte e reforço têm passos
/// ligeiramente diferentes porque dividem 31 e 32 posições.
double sliderToDb(int value) {
  const int v = std::clamp(value, 0, 63) - 31;
  if (v > 0) return -12.0 * (static_cast<double>(v) / 32.0);
  if (v < 0) return -12.0 * (static_cast<double>(v) / 31.0);
  return 0.0;
}

bool parseTextEqf(const std::string& text, EqPreset* preset, std::string* error) {
  std::istringstream input(text);
  std::string line;
  bool hasSection = false;
  bool sawBand = false;

  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[') {
      hasSection = lower(line).find("equalizer") != std::string::npos;
      continue;
    }
    if (!hasSection) continue;

    const auto equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = lower(trim(line.substr(0, equals)));
    const std::string value = trim(line.substr(equals + 1));
    if (key.rfind("band", 0) == 0) {
      const int index = std::atoi(key.c_str() + 4);
      if (index < 0 || index >= 10) continue;
      preset->bandsDb[static_cast<std::size_t>(index)] = std::atof(value.c_str());
      sawBand = true;
    } else if (key == "preamp") {
      preset->preampDb = std::atof(value.c_str());
    } else if (key == "presetname") {
      if (!value.empty()) preset->name = value;
    }
  }

  if (!sawBand) {
    if (error != nullptr) *error = "o arquivo não tem nenhuma banda (Band0..Band9)";
    return false;
  }
  return true;
}

bool parseBinaryEqf(const std::string& bytes, EqPreset* preset, std::string* error) {
  const std::size_t signatureLength = std::strlen(kBinarySignature);
  if (bytes.size() <= signatureLength + 16) {
    if (error != nullptr) *error = "o arquivo .eqf está truncado";
    return false;
  }

  const std::size_t payload = bytes.size() - signatureLength;

  // Descobre o tamanho do registro: precisa dividir o arquivo e as bandas têm de
  // estar no domínio do slider (0..63). O pré-amp de 1 byte é o que o Winamp
  // escreve; o de 4 bytes é a variante que também circula.
  std::size_t nameSize = 0;
  std::size_t preampSize = 0;
  for (const std::size_t candidatePreamp : {std::size_t{1}, std::size_t{4}}) {
    for (std::size_t size = 251; size <= 262; ++size) {
      const std::size_t record = size + 10 + candidatePreamp;
      if (payload % record != 0) continue;
      const auto* bands = reinterpret_cast<const unsigned char*>(bytes.data() + signatureLength + size);
      bool valid = true;
      for (int band = 0; band < 10; ++band) {
        if (bands[band] > 63) { valid = false; break; }
      }
      if (!valid) continue;
      nameSize = size;
      preampSize = candidatePreamp;
      break;
    }
    if (nameSize != 0) break;
  }

  if (nameSize == 0) {
    if (error != nullptr) {
      *error = "não reconheci o formato (arquivo com " + std::to_string(bytes.size()) +
               " bytes) — talvez seja um .eqf de texto ou de outra versão";
    }
    return false;
  }

  // A biblioteca guarda várias curvas; o .eqf exportado normalmente tem uma só
  // ("Entry1"). Pegamos a primeira, que é o que o menu de presets do Winamp usa.
  const std::size_t base = signatureLength;
  const char* name = bytes.data() + base;
  preset->name = trim(std::string(name, ::strnlen(name, nameSize)));
  if (preset->name.empty()) preset->name = "Importado";

  const auto* values = reinterpret_cast<const unsigned char*>(bytes.data() + base + nameSize);
  for (int band = 0; band < 10; ++band) {
    preset->bandsDb[static_cast<std::size_t>(band)] = sliderToDb(values[band]);
  }

  int preamp = 31;
  const char* preampPtr = bytes.data() + base + nameSize + 10;
  if (preampSize == 1) {
    preamp = static_cast<unsigned char>(*preampPtr);
  } else {
    std::memcpy(&preamp, preampPtr, sizeof(preamp));
  }
  preset->preampDb = sliderToDb(preamp);
  return true;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
        field += '"';
        ++i;
      } else {
        quoted = !quoted;
      }
    } else if (c == ',' && !quoted) {
      fields.push_back(trim(field));
      field.clear();
    } else {
      field += c;
    }
  }
  fields.push_back(trim(field));
  return fields;
}

}  // namespace

std::string eqPresetCsvHeader() {
  return "nome,preamp_db,70,180,320,600,1000,3000,6000,12000,14000,16000";
}

std::string presetFileName(const std::string& name) {
  std::string slug;
  bool pendingDash = false;
  for (unsigned char c : name) {
    if (c >= 0x80) {  // acentos: mantém a letra, sem tentar transliterar
      slug += static_cast<char>(c);
      pendingDash = false;
    } else if (std::isalnum(c) != 0) {
      if (pendingDash && !slug.empty()) slug += '-';
      slug += static_cast<char>(std::tolower(c));
      pendingDash = false;
    } else {
      pendingDash = true;
    }
  }
  return slug;
}

std::vector<std::string> listUserPresets(const std::string& dir) {
  namespace fs = std::filesystem;
  std::vector<std::string> files;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return files;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string ext = lower(entry.path().extension().string());
    if (ext == ".csv") files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool loadEqPreset(const std::string& file, EqPreset* preset, std::string* error) {
  if (preset == nullptr) return false;
  std::ifstream input(file, std::ios::binary);
  if (!input) {
    if (error != nullptr) *error = "não foi possível abrir " + file;
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string bytes = buffer.str();
  if (bytes.empty()) {
    if (error != nullptr) *error = "arquivo vazio";
    return false;
  }

  if (bytes.compare(0, std::strlen(kBinarySignature), kBinarySignature) == 0) {
    return parseBinaryEqf(bytes, preset, error);
  }
  return parseTextEqf(bytes, preset, error);
}

bool saveEqPresetCsv(const std::string& file, const EqPreset& preset, std::string* error) {
  std::ofstream output(file, std::ios::trunc);
  if (!output) {
    if (error != nullptr) *error = "não foi possível gravar " + file;
    return false;
  }
  output << eqPresetCsvHeader() << '\n';
  output << preset.name << ',' << preset.preampDb;
  for (double band : preset.bandsDb) output << ',' << band;
  output << '\n';
  if (!output) {
    if (error != nullptr) *error = "falha ao gravar " + file;
    return false;
  }
  return true;
}

bool loadEqPresetCsv(const std::string& file, EqPreset* preset, std::string* error) {
  if (preset == nullptr) return false;
  std::ifstream input(file);
  if (!input) {
    if (error != nullptr) *error = "não foi possível abrir " + file;
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty()) continue;
    const std::vector<std::string> fields = splitCsvLine(line);
    if (fields.size() < 12) continue;
    // Pula o cabeçalho: a segunda coluna do cabeçalho é "preamp_db", não número.
    char* end = nullptr;
    std::strtod(fields[1].c_str(), &end);
    if (end == fields[1].c_str()) continue;

    preset->name = fields[0].empty() ? "Importado" : fields[0];
    preset->preampDb = std::atof(fields[1].c_str());
    for (std::size_t band = 0; band < 10; ++band) {
      preset->bandsDb[band] = std::atof(fields[2 + band].c_str());
    }
    return true;
  }

  if (error != nullptr) *error = "o CSV não tem nenhuma linha de curva";
  return false;
}

}  // namespace wamp
