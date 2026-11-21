#pragma once

#include <array>
#include <string>
#include <vector>

namespace wamp {

/// Uma curva de equalização, no formato que o resto do app entende (dB).
struct EqPreset {
  std::string name = "Personalizado";
  double preampDb = 0.0;
  std::array<double, 10> bandsDb{};
};

/// Lê um `.eqf` do Winamp. Aceita os dois formatos que circulam por aí:
/// o texto (`[Equalizer Preset]` com `Band0..9`), usado por players que
/// copiaram o formato, e a biblioteca binária ("Winamp EQ library file
/// v1.1") que o próprio Winamp grava — mesma extensão, conteúdo diferente.
bool loadEqPreset(const std::string& file, EqPreset* preset, std::string* error);

/// Nossa planilha de compartilhamento: cabeçalho com os nomes das bandas e uma
/// linha por curva, valores em dB. CSV porque abre em qualquer lugar e não exige
/// código para ler.
bool saveEqPresetCsv(const std::string& file, const EqPreset& preset, std::string* error);
bool loadEqPresetCsv(const std::string& file, EqPreset* preset, std::string* error);

/// Cabeçalho usado por `saveEqPresetCsv` (exposto para teste e para a UI mostrar).
std::string eqPresetCsvHeader();

/// Nome de arquivo seguro para uma curva: "Rock Pesado!" vira "rock-pesado".
/// Vazio quando o nome não tem nenhuma letra ou número.
std::string presetFileName(const std::string& name);

/// As curvas do usuário são os `.csv` de `dir` (o mesmo formato do
/// compartilhamento), em ordem alfabética. Diretório inexistente devolve vazio.
std::vector<std::string> listUserPresets(const std::string& dir);

}  // namespace wamp
