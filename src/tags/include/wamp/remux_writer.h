#pragma once

#include <string>

#include "wamp/tag_editor.h"

namespace wamp {

/// A escrita pelo TagLib cobre quase tudo, mas não o MP4 do runtime (2.1.1, `save()` volta
/// verdadeiro sem escrever) e não limpa campo no ID3 quando o arquivo também tem ID3v1.
/// Nos casos em que ela falha, a saída é reescrever o arquivo por remuxagem: os fluxos são
/// copiados sem recodificar e o cabeçalho novo leva exatamente os campos pedidos.
bool remuxCanWrite(const std::string& path);

/// Copia `path` para um temporário com as tags dadas, confere relendo e troca o original.
/// A cópia segura é parte do desenho: até a conferência passar, o original não é tocado.
bool remuxWrite(const std::string& path, const TagFields& fields, std::string* error);

}  // namespace wamp
