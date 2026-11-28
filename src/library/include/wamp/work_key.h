// Identidade da obra — norm/v1.
//
// A chave diz "esta é a mesma obra", não "este é o mesmo arquivo": rip, remaster e cópia em
// outra pasta caem na mesma chave; gravação ao vivo, remix e demo caem em chaves diferentes,
// porque são outra gravação. As regras estão em docs/COMPARTILHAMENTO.md §2.
//
// Função pura: sem rede, sem banco, sem Qt — o mesmo código roda no player, no importador do
// dataset e num teste avulso.
#pragma once

#include <cstdint>
#include <string>

namespace wamp {

/// Campo que está sendo normalizado. O título perde número de faixa e convidado; o álbum perde
/// marcador de disco; o artista só passa pelo tratamento comum (Unicode, caixa, espaços).
enum class WorkField { Artist, Album, Title };

/// Versão corrente do algoritmo. Muda a regra, muda a versão — e o arquivo compartilhado
/// carrega a versão no cabeçalho.
inline constexpr const char* kWorkKeyVersion = "norm/v1";

/// Normaliza um campo pelas regras de `norm/v1`.
std::string normalizeWorkField(WorkField field, const std::string& value);

/// `sha1(norm(artist) 0x1f norm(album) 0x1f norm(title))`, em hexadecimal minúsculo.
/// A duração fica **fora** da chave: ela entra no casamento, não na identidade.
std::string workKey(const std::string& artist, const std::string& album, const std::string& title);

/// Troca os hífens tipográficos (U+2010..U+2014) pelo hífen comum. Vale para nome de artista e
/// de artista do álbum: o MusicBrainz grava `a‐ha`, o arquivo e o resto do mundo usam
/// `a-ha`, e o byte diferente quebra busca, filtro e comparação. Título fica de fora — ali a
/// pontuação é escolha editorial e não se mexe.
std::string asciiDashes(const std::string& value);

/// Duração em segundos cheios, como sai no arquivo compartilhado.
int workDurationSeconds(std::int64_t durationMs);

/// SHA-1 de um bloco de bytes, em hexadecimal minúsculo. Exposto para o teste conferir os
/// vetores conhecidos e para quem precisar do mesmo hash no futuro.
std::string sha1Hex(const std::string& data);

}  // namespace wamp
