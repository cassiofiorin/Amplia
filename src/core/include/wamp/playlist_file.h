#pragma once

#include <string>
#include <vector>

namespace wamp {

/// Formatos de playlist do Winamp e do Windows, além do .m3u:
/// `.pls` (radio), `.b4s` (Winamp 5) e `.wpl` (Windows Media Player).
/// Caminhos relativos são resolvidos contra a pasta do próprio arquivo.
bool loadPlaylistFile(const std::string& file, std::vector<std::string>* out,
                      std::string* error);

/// Grava no formato indicado pela extensão (`.m3u`, `.pls`, `.b4s`, `.wpl`).
bool savePlaylistFile(const std::string& file, const std::vector<std::string>& items,
                      std::string* error);

/// Transforma `file:///home/x/y.mp3` (com %20 e afins) em caminho local.
/// URLs de rede (`http://`, `mms://`) são devolvidas como estão: o FFmpeg abre.
std::string playlistEntryToPath(const std::string& entry);

/// Caminho local -> `file://` percent-encoded, como os formatos XML esperam.
std::string pathToPlaylistUrl(const std::string& path);

}  // namespace wamp
