// Teste do carimbo "tag corrigida pelo app" (D-153): a fila do autotag só ignora a faixa
// enquanto o arquivo estiver exatamente como o app o deixou. Usa um banco temporário.
#include "wamp/tag_cache.h"

#include <chrono>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main() {
  const fs::path db = fs::temp_directory_path() / "amplia-fixed-test.db";
  const fs::path audio = fs::temp_directory_path() / "amplia-fixed-test.mp3";
  fs::remove(db);
  fs::remove(audio);
  { std::ofstream(audio) << "nao e audio de verdade"; }

  wamp::TagCache cache(db.string());
  std::string error;
  if (!cache.open(&error)) {
    std::printf("FALHA ao abrir o banco: %s\n", error.c_str());
    return 1;
  }

  wamp::TrackInfo info;
  info.path = audio.string();
  info.fileSize = static_cast<std::int64_t>(fs::file_size(audio));
  info.modifiedMs = wamp::fileMtimeMs(audio.string());
  info.title = "Teste";
  cache.store(info);
  cache.flush();

  int failures = 0;
  const auto check = [&failures](bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FALHA", what);
    if (!ok) ++failures;
  };

  check(cache.notFixedByApp({audio.string()}).size() == 1,
        "faixa nunca corrigida continua elegível");

  cache.markTagFixed(audio.string(), wamp::fileMtimeMs(audio.string()));
  cache.flush();
  check(cache.notFixedByApp({audio.string()}).empty(),
        "faixa corrigida e intacta sai da fila");

  fs::last_write_time(audio, fs::last_write_time(audio) + std::chrono::seconds(5));
  check(cache.notFixedByApp({audio.string()}).size() == 1,
        "arquivo mexido por fora volta para a fila");

  fs::remove(db);
  fs::remove(audio);
  std::printf("%s\n", failures == 0 ? "\ntudo ok" : "\nhouve falha");
  return failures == 0 ? 0 : 1;
}
