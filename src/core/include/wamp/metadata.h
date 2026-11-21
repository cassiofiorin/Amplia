#pragma once

#include <string>

#include "wamp/types.h"

namespace wamp {

/// Lê só o cabeçalho do arquivo (container + stream de áudio), sem abrir codec nem
/// decodificar. Serve para preencher a playlist sem o custo de um `open()` completo.
/// Retorna false se o arquivo não puder ser lido; `error` recebe o motivo.
bool probeTrack(const std::string& path, TrackInfo* info, std::string* error);

/// O arquivo tem áudio e nenhum vídeo de verdade (capa embutida não conta)?
/// Serve para aceitar `.mka`/`.m4b` e recusar `.mkv` mesmo tendo faixa de áudio.
bool probeAudioOnly(const std::string& path);

}  // namespace wamp
