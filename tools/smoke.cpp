// Teste de fumaça do pipeline de áudio, sem GUI.
//
// Uso: wamp-smoke <arquivo>
//
// Verifica a máquina de estados do PlayerEngine: play, pause (a posição precisa
// congelar) e retomada (a posição precisa voltar a subir). Sai com código 0 quando
// tudo passa e 1 quando algo falha.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <cstring>

#include "wamp/eq_preset.h"
#include "wamp/replay_gain.h"

#include "wamp/equalizer.h"
#include "wamp/metadata.h"
#include "wamp/metadata_scanner.h"
#include "wamp/audio_analyzer.h"
#include "wamp/player_engine.h"
#include "wamp/scrobble_types.h"
#include "wamp/tag_cache.h"
#include "wamp/tag_editor.h"
#include "wamp/playlist.h"

namespace {

using namespace std::chrono_literals;

bool waitFor(bool (*pred)(const wamp::PlayerEngine&), const wamp::PlayerEngine& engine,
             std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred(engine)) return true;
    std::this_thread::sleep_for(20ms);
  }
  return pred(engine);
}

/// Escreve um WAV PCM 16 bits mono — material de teste sem depender de arquivos.
std::string writeToneWav(const std::filesystem::path& dir, const char* name, double hz,
                         double seconds) {
  constexpr int kRate = 48000;
  const int frames = static_cast<int>(kRate * seconds);
  std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const double v = 0.30 * std::sin(2.0 * 3.14159265358979 * hz * i / kRate);
    pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(v * 32767.0);
  }

  const auto path = dir / name;
  std::ofstream out(path, std::ios::binary);
  const auto u32 = [&out](std::uint32_t v) {
    const char bytes[4] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                           static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff)};
    out.write(bytes, 4);
  };
  const auto u16 = [&out](std::uint16_t v) {
    const char bytes[2] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
    out.write(bytes, 2);
  };
  const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
  out.write("RIFF", 4);
  u32(36 + dataBytes);
  out.write("WAVEfmt ", 8);
  u32(16);
  u16(1);                                   // PCM
  u16(1);                                   // mono
  u32(kRate);
  u32(kRate * 2);                           // byte rate
  u16(2);                                   // block align
  u16(16);                                  // bits
  out.write("data", 4);
  u32(dataBytes);
  out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(dataBytes));
  return path.string();
}

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FALHA", what);
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  // Modo diagnóstico: `wamp-smoke <audio> <curva.eqf|.csv>` imprime o preset lido.
  if (argc >= 3) {
    wamp::EqPreset preset;
    std::string error;
    const std::string file = argv[2];
    const bool csv = file.size() > 4 && file.compare(file.size() - 4, 4, ".csv") == 0;
    const bool ok = csv ? wamp::loadEqPresetCsv(file, &preset, &error)
                        : wamp::loadEqPreset(file, &preset, &error);
    if (!ok) {
      std::fprintf(stderr, "falhou: %s\n", error.c_str());
      return 1;
    }
    std::printf("preset: %s\npreamp: %+.2f dB\nbandas:", preset.name.c_str(), preset.preampDb);
    for (double band : preset.bandsDb) std::printf(" %+.2f", band);
    std::printf("\n");
    return 0;
  }

  if (argc < 2) {
    std::fprintf(stderr, "uso: %s <arquivo-de-audio>\n", argv[0]);
    return 2;
  }

  wamp::PlayerEngine engine;
  engine.setCallbacks({});

  if (!engine.load(argv[1])) {
    std::fprintf(stderr, "não abriu %s: %s\n", argv[1], engine.lastError().c_str());
    return 1;
  }
  std::printf("carregado: %s (%lld ms)\n", engine.track().displayTitle().c_str(),
              static_cast<long long>(engine.durationMs()));

  engine.setVolume(0.0f);  // o teste não precisa ser audível
  engine.play();
  check(waitFor([](const auto& e) { return e.positionMs() > 400; }, engine, 5000ms),
        "reproduz a partir do zero");

  engine.pause();
  const auto pausedAt = engine.positionMs();
  std::this_thread::sleep_for(700ms);
  const auto afterPause = engine.positionMs();
  check(afterPause - pausedAt <= 60, "posição congela durante a pausa");

  engine.play();
  std::this_thread::sleep_for(700ms);
  const auto afterResume = engine.positionMs();
  check(afterResume - afterPause > 200, "posição volta a subir ao retomar");

  engine.stop();
  std::this_thread::sleep_for(200ms);
  check(engine.positionMs() == 0, "stop volta para o começo");

  engine.seek(engine.durationMs() / 2);
  const auto half = engine.durationMs() / 2;
  check(std::llabs(engine.positionMs() - half) < 150, "seek reposiciona a posição");

  // Playlist e fila: puro estado, sem áudio.
  {
    wamp::Playlist pl;
    pl.add("/tmp/a.mp3");
    pl.add("/tmp/b.mp3");
    pl.add("/tmp/c.mp3");
    check(pl.size() == 3, "playlist aceita itens");
    pl.setCurrent(2);
    check(pl.nextIndex() == 0, "próxima dá a volta no fim");
    check(pl.previousIndex() == 1, "anterior volta um");
    pl.setCurrent(0);
    check(pl.previousIndex() == 2, "anterior dá a volta no começo");
    pl.enqueue(1);
    pl.enqueue(2);
    check(pl.queuePosition(1) == 1, "fila numera a partir de 1");
    check(pl.queuePosition(2) == 2, "segundo enfileirado é posição 2");
    check(pl.queuePosition(0) == 0, "faixa fora da fila não tem posição");
    check(pl.dequeue() == "/tmp/b.mp3", "fila entrega a faixa enfileirada");
    check(pl.queuePosition(2) == 1, "posição anda quando a fila anda");

    check(pl.shuffledNextIndex(0.0) == 1, "sorteio nível 0 é sequencial");
    pl.setCurrent(1);
    bool shuffleOk = true;
    for (int i = 0; i < 20 && shuffleOk; ++i) {
      const std::size_t picked = pl.shuffledNextIndex(1.0);
      if (picked == pl.currentIndex() || picked >= pl.size()) shuffleOk = false;
      pl.setCurrent(picked);
    }
    check(shuffleOk, "sorteio nível 1 evita a faixa atual");

    wamp::Playlist queued;
    queued.addAll({"/tmp/x.mp3", "/tmp/y.mp3"});
    queued.enqueue(1);
    check(queued.removeFromQueue(1), "remove faixa da fila");
    check(queued.queueEmpty(), "fila fica vazia depois de remover");
    pl.dequeue();

    // Ordem da lista: inverter, embaralhar e tirar o que sumiu do disco.
    {
      std::error_code ignored;
      const std::filesystem::path dir =
          std::filesystem::temp_directory_path() / "wamp-order-test";
    std::filesystem::remove_all(dir, ignored);
      std::filesystem::create_directories(dir, ignored);
      const std::string one = writeToneWav(dir, "um.wav", 220.0, 0.2);
      const std::string two = writeToneWav(dir, "dois.wav", 330.0, 0.2);
      const std::string three = writeToneWav(dir, "tres.wav", 440.0, 0.2);

      wamp::Playlist order;
      order.addAll({one, two, three});
      order.setCurrent(1);
      order.reverseOrder();
      check(order.at(0) == three && order.at(2) == one, "inverter lista troca a ordem");
      check(order.currentIndex() == 1 && order.at(1) == two,
            "inverter mantém a faixa atual");

      order.setItems({one, two, three}, 2);
      order.shuffleOrder();
      check(order.size() == 3 && order.at(order.currentIndex()) == three,
            "embaralhar preserva a faixa atual");

      std::filesystem::remove(two, ignored);
      const std::size_t gone = order.removeMissingFiles();
      check(gone == 1, "remove arquivo que não existe mais");
      check(order.size() == 2, "lista encolhe ao remover inexistente");
      check(std::find(order.items().begin(), order.items().end(), two) == order.items().end(),
            "arquivo inexistente saiu da lista");
      std::filesystem::remove_all(dir, ignored);
    }

    // Formatos de playlist: .m3u (ida e volta), .pls, .b4s e .wpl.
    {
      std::string err;
      for (const char* ext : {".m3u", ".pls", ".b4s", ".wpl"}) {
        const std::string file = std::string("/tmp/amplia-smoke-fmt") + ext;
        check(pl.savePlaylist(file, &err), (std::string("salva ") + ext).c_str());
        wamp::Playlist loaded;
        check(loaded.loadPlaylist(file, &err), (std::string("carrega ") + ext).c_str());
        check(loaded.size() == 3, (std::string("três faixas em ") + ext).c_str());
        check(loaded.at(0) == "/tmp/a.mp3",
              (std::string("caminho preservado em ") + ext).c_str());
        std::remove(file.c_str());
      }

      // .pls com caminho relativo e .b4s com URL percent-encoded.
      {
        std::ofstream out("/tmp/amplia-smoke-rel.pls", std::ios::trunc);
        out << "[playlist]\nNumberOfEntries=2\nFile1=sub/a.mp3\nFile2=/tmp/abs.mp3\n";
      }
      wamp::Playlist rel;
      check(rel.loadPlaylist("/tmp/amplia-smoke-rel.pls", &err), "carrega .pls relativo");
      check(rel.at(0) == "/tmp/sub/a.mp3", ".pls resolve caminho relativo");
      check(rel.at(1) == "/tmp/abs.mp3", ".pls mantém caminho absoluto");
      std::remove("/tmp/amplia-smoke-rel.pls");

      {
        std::ofstream out("/tmp/amplia-smoke-url.b4s", std::ios::trunc);
        out << "<?xml version=\"1.0\"?><WinampXML><playlist num_entries=\"2\">"
            << "<entry playstring=\"file:///tmp/Com%20Espaco.mp3\" length=\"-1\"/>"
            << "<entry playstring=\"http://exemplo/stream.mp3\" length=\"-1\"/>"
            << "</playlist></WinampXML>";
      }
      wamp::Playlist url;
      check(url.loadPlaylist("/tmp/amplia-smoke-url.b4s", &err), "carrega .b4s com URL");
      check(url.at(0) == "/tmp/Com Espaco.mp3", ".b4s decodifica %20");
      check(url.at(1) == "http://exemplo/stream.mp3", ".b4s preserva URL de rede");
      std::remove("/tmp/amplia-smoke-url.b4s");

      check(!pl.savePlaylist("/tmp/amplia-smoke.xyz", &err), "recusa extensão desconhecida");
      std::remove("/tmp/amplia-smoke.xyz");
    }
  }

  // Metadados: leitura de cabeçalho sem decodificar, e a fila entregando o resultado.
  {
    wamp::TrackInfo direct;
    std::string err;
    check(wamp::probeTrack(argv[1], &direct, &err), "probeTrack abre o arquivo");
    check(direct.durationMs > 0, "duração vem do cabeçalho");
    check(direct.path == argv[1], "probeTrack preserva o caminho");

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    wamp::TrackInfo scanned;
    wamp::MetadataScanner scanner;
    scanner.setCallback([&](const std::string&, const wamp::TrackInfo& got, bool ok) {
      std::lock_guard<std::mutex> lock(mutex);
      done = ok;
      scanned = got;
      cv.notify_one();
    });
    scanner.enqueue(argv[1]);
    std::unique_lock<std::mutex> lock(mutex);
    check(cv.wait_for(lock, 5000ms, [&] { return done; }), "fila de metadados entrega a faixa");
    check(scanned.durationMs == direct.durationMs, "fila e leitura direta concordam");
  }

  // Gapless: a segunda faixa entra sem parar a saída (silencioso, volume 0).
  {
    const auto dir = std::filesystem::temp_directory_path();
    const std::string first = writeToneWav(dir, "wamp-gap-a.wav", 220.0, 1.0);
    const std::string second = writeToneWav(dir, "wamp-gap-b.wav", 440.0, 1.0);

    wamp::PlayerEngine gap;
    std::atomic<int> ended{0};
    std::atomic<int> changed{0};
    std::mutex changedMutex;
    std::string changedPath;
    gap.setCallbacks({
        [&ended] { ended.fetch_add(1); },
        [](const std::string&) {},
        [&](const wamp::TrackInfo& info) {
          std::lock_guard<std::mutex> lock(changedMutex);
          changedPath = info.path;
          changed.fetch_add(1);
        },
    });
    gap.setVolume(0.0f);

    const bool loaded = gap.load(first);
    check(loaded, "gapless: primeira faixa abre");
    if (loaded) {
      gap.setNextTrack(second);
      gap.play();

      const auto deadline = std::chrono::steady_clock::now() + 4000ms;
      while (std::chrono::steady_clock::now() < deadline && changed.load() == 0) {
        std::this_thread::sleep_for(10ms);
      }

      std::string switched;
      {
        std::lock_guard<std::mutex> lock(changedMutex);
        switched = changedPath;
      }
      check(changed.load() == 1, "gapless: uma transição anunciada");
      check(ended.load() == 0, "gapless: a saída não parou na virada");
      check(gap.state() == wamp::PlaybackState::Playing, "gapless: continua tocando depois da virada");
      check(switched == second, "gapless: toca a faixa seguinte");
      check(gap.positionMs() < 600, "gapless: posição recomeça na faixa nova");

      const auto endDeadline = std::chrono::steady_clock::now() + 4000ms;
      while (std::chrono::steady_clock::now() < endDeadline && ended.load() == 0) {
        std::this_thread::sleep_for(10ms);
      }
      check(ended.load() == 1, "gapless: avisa o fim só depois da segunda faixa");
      gap.stop();
    }

    std::error_code ignored;
    std::filesystem::remove(first, ignored);
    std::filesystem::remove(second, ignored);
  }

  // Regra de scrobble: >30 s e 50% ou 4 min, o que vier antes.
  {
    using wamp::qualifiesForScrobble;
    check(!qualifiesForScrobble(25'000, 25'000), "scrobble ignora faixa com menos de 30 s");
    check(!qualifiesForScrobble(200'000, 99'000), "scrobble ainda não vale antes da metade");
    check(qualifiesForScrobble(200'000, 100'000), "scrobble vale na metade da faixa");
    check(!qualifiesForScrobble(600'000, 239'000), "faixa longa espera 4 minutos");
    check(qualifiesForScrobble(600'000, 240'000), "faixa longa entra aos 4 minutos");
    check(!qualifiesForScrobble(0, 0), "sem duração não há scrobble");
  }

  // Cache de tags: guarda, devolve, e não devolve quando o arquivo muda.
  {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "wamp-cache-test";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir, ignored);

    const std::string track = writeToneWav(dir, "cache-track.wav", 330.0, 1.0);
    const std::string database = (dir / "library.db").string();

    wamp::TrackInfo info;
    std::string error;
    check(wamp::probeTrack(track, &info, &error), "probeTrack abre a faixa para o cache");
    check(info.modifiedMs > 0, "probeTrack anota a data do arquivo");

    {
      wamp::TagCache cache(database);
      check(cache.open(&error), "cache de tags abre");
      cache.store(info);
      cache.flush();
      check(cache.count() == 1, "cache guarda uma faixa");

      wamp::TrackInfo hit;
      check(cache.lookup(track, &hit), "cache devolve a faixa guardada");
      check(hit.durationMs == info.durationMs, "duração vem do cache");
      check(hit.fileSize == info.fileSize, "tamanho vem do cache");
    }

    // O arquivo muda (conteúdo e data): o registro antigo não vale mais.
    writeToneWav(dir, "cache-track.wav", 550.0, 1.0);
    std::filesystem::last_write_time(track,
                                     std::filesystem::file_time_type::clock::now() + 5s);
    wamp::TagCache cache(database);
    check(cache.open(&error), "cache reabre");
    wamp::TrackInfo stale;
    check(!cache.lookup(track, &stale), "arquivo alterado invalida o cache");

    std::filesystem::remove_all(dir, ignored);
  }

  // TagLib: lê, grava com gravação segura e confere numa CÓPIA (nunca no original).
  {
    std::error_code ignored;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wamp-tags-test";
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir, ignored);
    const std::filesystem::path copy = dir / "copia.mp3";
    std::filesystem::copy_file(argv[1], copy, std::filesystem::copy_options::overwrite_existing,
                               ignored);

    std::string error;
    wamp::TagFields fields;
    wamp::TagFormat format;
    std::vector<wamp::TagPicture> pictures;
    check(wamp::TagEditor::read(copy.string(), &fields, &format, &pictures, &error),
          "TagLib abre a cópia");
    check(!fields.title.empty() || !fields.artist.empty(), "TagLib lê título ou artista");
    check(format.durationMs > 0 && format.bitrate > 0, "TagLib lê a informação de áudio");

    const std::string originalTitle = fields.title;
    fields.title = "Título de teste Amplia";
    check(wamp::TagEditor::write(copy.string(), fields, true, &error), "gravação segura conclui");
    check(!std::filesystem::exists(copy.string() + ".wamp-tmp"),
          "gravação segura não deixa cópia temporária");
    wamp::TagFields reread;
    check(wamp::TagEditor::read(copy.string(), &reread, nullptr, nullptr, &error),
          "releitura depois da gravação");
    check(reread.title == "Título de teste Amplia", "título gravado voltou na releitura");

    fields.title = originalTitle;
    check(wamp::TagEditor::write(copy.string(), fields, true, &error), "título original restaurado");

    wamp::TagPicture art;
    art.data = std::string("\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR", 16);
    art.mime = "image/png";
    art.description = "capa de teste";
    art.type = "cover";
    check(wamp::TagEditor::replacePicture(copy.string(), 0, art, true, &error),
          "imagem embutida é gravada");
    std::vector<wamp::TagPicture> after;
    check(wamp::TagEditor::read(copy.string(), nullptr, nullptr, &after, &error) &&
              after.size() == 1,
          "imagem aparece na releitura");
    check(wamp::TagEditor::removePicture(copy.string(), 0, true, &error), "imagem é removida");
    std::vector<wamp::TagPicture> emptied;
    check(wamp::TagEditor::read(copy.string(), nullptr, nullptr, &emptied, &error) &&
              emptied.empty(),
          "arquivo fica sem imagem");

      // Capa da pasta: cover.jpg/folder.jpg, comparando sem caixa como as pastas do Windows.
      {
        const std::string song = writeToneWav(dir, "sem-capa.wav", 440.0, 1);
        check(wamp::TagEditor::folderCover(song).empty(), "sem cover na pasta devolve vazio");
        const auto coverPath = dir / "Cover.JPG";
        std::ofstream(coverPath, std::ios::binary) << "nao-e-imagem-de-verdade";
        const std::string found = wamp::TagEditor::folderCover(song);
        check(!found.empty(), "acha Cover.JPG com maiúsculas");
        check(found.find("Cover.JPG") != std::string::npos, "devolve o caminho da capa");
        std::error_code coverErr;
        std::filesystem::remove(coverPath, coverErr);
      }

      // Permissão de escrita: arquivo somente leitura e pasta sem escrita.
    {
      const std::string roPath = writeToneWav(dir, "somente-leitura.wav", 440.0, 1);
      std::error_code permErr;
      const auto readOnly = std::filesystem::perms::owner_read |
                            std::filesystem::perms::group_read |
                            std::filesystem::perms::others_read;
      std::filesystem::permissions(roPath, readOnly, std::filesystem::perm_options::replace,
                                   permErr);
      std::string motivo;
      const bool directOk = wamp::TagEditor::canSave(roPath, false, &motivo);
      check(!directOk,
            "arquivo somente leitura bloqueia gravação direta");
      check(motivo.find("permiss") != std::string::npos, "motivo cita a permissão do arquivo");
      std::string motivoSafe;
      const bool safeOk = wamp::TagEditor::canSave(roPath, true, &motivoSafe);
      check(safeOk, "gravação segura passa: a pasta aceita escrita");
      std::filesystem::permissions(roPath, std::filesystem::perms::owner_all,
                                   std::filesystem::perm_options::replace, permErr);
    }

    const std::string roDir = (dir / "pasta-somente-leitura").string();
    std::filesystem::create_directories(roDir, ignored);
    const std::string nested = writeToneWav(roDir, "dentro.wav", 440.0, 1);
    std::error_code dirErr;
    std::filesystem::permissions(roDir, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace, dirErr);
    std::string dirMotivo;
    check(!wamp::TagEditor::canSave(nested, true, &dirMotivo),
          "pasta sem escrita bloqueia a gravação segura");
    check(dirMotivo.find("pasta") != std::string::npos, "motivo cita a pasta");
    std::filesystem::permissions(roDir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, dirErr);

    // Montagem somente leitura: modo 0755 mas f_flag ro, que ::access() não vê.
    // O caminho vem do ambiente porque é específico de cada máquina.
    const char* roMountEnv = std::getenv("AMPLIA_TEST_RO_MOUNT");
    const std::string nas = roMountEnv != nullptr ? roMountEnv : "";
    if (!nas.empty() && std::filesystem::exists(nas)) {
      std::string nasMotivo;
      bool foundReadOnly = false;
      for (const auto& entry : std::filesystem::recursive_directory_iterator(
               nas, std::filesystem::directory_options::skip_permission_denied, ignored)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
          foundReadOnly = true;
          check(!wamp::TagEditor::canSave(entry.path().string(), true, &nasMotivo),
                "montagem somente leitura bloqueia a gravação segura");
          check(nasMotivo.find("somente leitura") != std::string::npos,
                "motivo cita o sistema de arquivos somente leitura");
          break;
        }
      }
      if (!foundReadOnly) std::printf("aviso  nenhum mp3 em %s para testar\n", nas.c_str());
    }

    std::filesystem::remove_all(dir, ignored);
  }

  // probeAudioOnly: aceita áudio puro e recusa o que não abre.
  {
    check(wamp::probeAudioOnly(argv[1]), "um MP3 é áudio puro");
    check(!wamp::probeAudioOnly("/tmp/nao-existe.mp3"), "arquivo inexistente não é áudio");
  }


  // Equalizador: +12 dB na banda de 1 kHz deve render ~4x de amplitude.
  {
    wamp::Equalizer eq;
    eq.setSampleRate(48000.0);
    std::array<double, wamp::Equalizer::kBands> gains{};
    gains[4] = 12.0;  // 1 kHz
    eq.setGains(gains);
    eq.setEnabled(true);
    eq.reset();

    constexpr int kFrames = 48000;
    const double amplitude = 0.1;
    std::vector<float> buffer(static_cast<std::size_t>(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
      const double t = static_cast<double>(i) / 48000.0;
      const auto sample = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 1000.0 * t));
      buffer[static_cast<std::size_t>(i) * 2] = sample;
      buffer[static_cast<std::size_t>(i) * 2 + 1] = sample;
    }
    eq.process(buffer.data(), kFrames, 2);

    double sum = 0.0;
    const int from = kFrames / 2;  // ignora o transiente do filtro
    for (int i = from; i < kFrames; ++i) {
      const double value = buffer[static_cast<std::size_t>(i) * 2];
      sum += value * value;
    }
    const double rms = std::sqrt(sum / (kFrames - from));
    const double reference = amplitude / std::sqrt(2.0);
    check(rms > reference * 3.0 && rms < reference * 5.0,
          "banda de 1 kHz com +12 dB cresce ~4x");

    // Desligado, o sinal passa intacto.
    std::vector<float> untouched(static_cast<std::size_t>(kFrames) * 2, 0.25f);
    eq.setEnabled(false);
    eq.process(untouched.data(), kFrames, 2);
    check(untouched.front() == 0.25f && untouched.back() == 0.25f,
          "EQ desligado não mexe no sinal");

    // Limitador do EQ10: +12 dB numa entrada já alta não estoura a saída.
    {
      wamp::Equalizer limited;
      limited.setSampleRate(48000.0);
      limited.setGains(gains);
      limited.setEnabled(true);
      std::vector<float> loud(static_cast<std::size_t>(kFrames) * 2);
      for (int i = 0; i < kFrames; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        const auto sample = static_cast<float>(0.9 * std::sin(2.0 * M_PI * 1000.0 * t));
        loud[static_cast<std::size_t>(i) * 2] = sample;
        loud[static_cast<std::size_t>(i) * 2 + 1] = sample;
      }
      limited.process(loud.data(), kFrames, 2);
      float peak = 0.0f;
      for (int i = kFrames / 2; i < kFrames; ++i) {
        peak = std::max(peak, std::abs(loud[static_cast<std::size_t>(i) * 2]));
      }
      check(peak > 0.85f && peak < 0.95f, "limitador segura o pico em -0,6 dB");
    }

    // ReplayGain: texto das tags -> fator linear, com teto pelo pico declarado.
    {
      double db = 0.0;
      check(wamp::parseGainDb("-6.50 dB", &db) && std::abs(db + 6.5) < 0.001,
            "ReplayGain lê ganho negativo");
      check(wamp::parseGainDb("+3.2 dB", &db) && std::abs(db - 3.2) < 0.001,
            "ReplayGain lê ganho positivo");
      check(wamp::parseGainDb("−4,5 dB", &db) && std::abs(db + 4.5) < 0.001,
            "ReplayGain lê vírgula e o menos tipográfico");
      check(!wamp::parseGainDb("sem número", &db), "ReplayGain recusa texto sem número");

      double peak = 0.0;
      double p2 = 0.0;
      check(wamp::parsePeak("0.900000", &p2) && std::abs(p2 - 0.9) < 1e-9,
            "pico decimal com ponto é lido (independente do locale)");
      check(wamp::parsePeak("0.98765", &peak) && std::abs(peak - 0.98765) < 1e-6,
            "pico em fração é lido direto");
      check(wamp::parsePeak("98765", &peak) && std::abs(peak - 0.98765) < 1e-6,
            "pico em inteiro vira fração");

      check(std::abs(wamp::replayGainFactor(-6.0) - 0.501187) < 0.0001,
            "-6 dB vira fator ~0,5");
      check(std::abs(wamp::replayGainFactor(6.0, 0.9) - 1.111111) < 0.0001,
            "ganho limitado pelo pico declarado");
      check(wamp::replayGainFactor("", "") == 1.0, "sem tag de ReplayGain o fator é 1");
      check(std::abs(wamp::replayGainFactor("+6.00 dB", "0.900000") - 1.111111) < 0.0001,
            "texto das tags respeita o teto do pico");

      wamp::PlayerEngine rgEngine;
      rgEngine.setReplayGainFactor(0.5);
      check(std::abs(rgEngine.replayGainFactor() - 0.5) < 1e-6, "motor guarda o fator de ReplayGain");
      rgEngine.setReplayGainFactor(1000.0);
      check(rgEngine.replayGainFactor() <= 16.0, "fator absurdo é limitado");
    }

    // Presets do equalizador: .eqf de texto, .eqf binário do Winamp e CSV nosso.
    {
      const auto dir = std::filesystem::temp_directory_path() / "wamp-eqpreset-test";
      std::error_code ignored;
      std::filesystem::remove_all(dir, ignored);
      std::filesystem::create_directories(dir);

      const auto textPath = dir / "texto.eqf";
      {
        std::ofstream out(textPath);
        out << "[Equalizer Preset]\nPresetName=Meu Rock\nPreamp=3\n";
        for (int band = 0; band < 10; ++band) out << "Band" << band << "=" << (band - 5) << "\n";
      }
      wamp::EqPreset fromText;
      std::string error;
      const bool textOk = wamp::loadEqPreset(textPath.string(), &fromText, &error);
      check(textOk, "eqf de texto abre");
      check(fromText.name == "Meu Rock" && fromText.preampDb == 3.0, "eqf de texto traz nome e pré-amp");
      check(fromText.bandsDb[0] == -5.0 && fromText.bandsDb[9] == 4.0, "eqf de texto traz as bandas");

      // Binário do Winamp: assinatura + nome (261 bytes) + 10 valores 0..63 + pré-amp.
      const auto binPath = dir / "binario.eqf";
      {
        std::string bytes("Winamp EQ library file v1.1\x1a!--");
        std::vector<char> record(261 + 10 + 4, 0);
        std::memcpy(record.data(), "Entry1", 6);
        record[261 + 0] = 0;   // +12 dB
        record[261 + 1] = 31;  // 0 dB
        record[261 + 9] = 63;  // -12 dB
        const int preamp = 31;
        std::memcpy(record.data() + 271, &preamp, sizeof(preamp));
        bytes += std::string(record.begin(), record.end());
        std::ofstream out(binPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      }
      wamp::EqPreset fromBin;
      const bool binOk = wamp::loadEqPreset(binPath.string(), &fromBin, &error);
      check(binOk, "eqf binário do Winamp abre");
      check(fromBin.name == "Entry1", "eqf binário traz o nome da curva");
      check(fromBin.bandsDb[0] == 12.0 && fromBin.bandsDb[1] == 0.0 && fromBin.bandsDb[9] == -12.0,
            "eqf binário converte slider 0..63 para dB");

      // Layout do Winamp 5 (o que ele grava de verdade): 257 de nome + 10 + 1 = 268.
      const auto realPath = dir / "winamp5.eqf";
      {
        std::string bytes("Winamp EQ library file v1.1\x1a!--");
        std::vector<char> record(257 + 10 + 1, 0);
        std::memcpy(record.data(), "Cassio", 6);
        for (int band = 0; band < 10; ++band) record[257 + band] = static_cast<char>(31);
        record[257 + 0] = 0;   // +12 dB
        record[257 + 9] = 63;  // -12 dB
        record[267] = 31;  // pré-amp em 0 dB
        bytes += std::string(record.begin(), record.end());
        std::ofstream out(realPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      }
      wamp::EqPreset fromReal;
      const bool realOk = wamp::loadEqPreset(realPath.string(), &fromReal, &error);
      check(realOk, "eqf do Winamp 5 (registro de 268 bytes) abre");
      check(fromReal.name == "Cassio" && fromReal.preampDb == 0.0, "eqf do Winamp 5 traz nome e pré-amp");
      check(std::abs(fromReal.bandsDb[0] - 12.0) < 0.01 && fromReal.bandsDb[5] == 0.0 &&
                std::abs(fromReal.bandsDb[9] + 12.0) < 0.01,
            "eqf do Winamp 5 converte os sliders certos");

      check(wamp::presetFileName("Rock Pesado!") == "rock-pesado", "nome de curva vira arquivo seguro");
      check(wamp::presetFileName("  ***  ").empty(), "nome sem letra não vira arquivo");

      {
        const auto library = dir / "presets";
        std::filesystem::create_directories(library);
        wamp::EqPreset one;
        one.name = "Primeira";
        one.bandsDb = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        wamp::saveEqPresetCsv((library / "primeira.csv").string(), one, &error);
        wamp::EqPreset two;
        two.name = "Segunda";
        wamp::saveEqPresetCsv((library / "segunda.csv").string(), two, &error);
        std::ofstream noise(library / "anotacoes.txt");
        noise << "não é curva";

        const auto found = wamp::listUserPresets(library.string());
        check(found.size() == 2, "lista de curvas ignora o que não é .csv");
        check(found.size() == 2 && found[0].find("primeira.csv") != std::string::npos,
              "lista de curvas vem em ordem alfabética");
        check(wamp::listUserPresets((dir / "nao-existe").string()).empty(),
              "diretório de curvas inexistente devolve lista vazia");
      }

      wamp::EqPreset exported;
      exported.name = "Compartilhada";
      exported.preampDb = -2.0;
      exported.bandsDb = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
      const auto csvPath = dir / "curva.csv";
      check(wamp::saveEqPresetCsv(csvPath.string(), exported, &error), "CSV de curva grava");
      wamp::EqPreset imported;
      check(wamp::loadEqPresetCsv(csvPath.string(), &imported, &error), "CSV de curva lê");
      check(imported.name == "Compartilhada" && imported.preampDb == -2.0,
            "CSV preserva nome e pré-amp");
      check(imported.bandsDb == exported.bandsDb, "CSV preserva as dez bandas");
      std::ifstream header(csvPath);
      std::string firstLine;
      std::getline(header, firstLine);
      check(firstLine == wamp::eqPresetCsvHeader(), "CSV começa com o cabeçalho das bandas");

      std::filesystem::remove_all(dir, ignored);
    }

    // Pré-amp de -6 dB corta pela metade.
    wamp::Equalizer preampEq;
    preampEq.setSampleRate(48000.0);
    preampEq.setPreamp(-6.0);
    preampEq.setEnabled(true);
    std::vector<float> quiet(static_cast<std::size_t>(kFrames) * 2, 0.5f);
    preampEq.process(quiet.data(), kFrames, 2);
    check(quiet.front() > 0.23f && quiet.front() < 0.27f, "pré-amp de -6 dB corta pela metade");
  }

  // Compensação de latência: o analisador tem que olhar o passado (o que já saiu), não o
  // último bloco decodificado. Um tom só no começo e silêncio depois prova os dois lados.
  {
    wamp::AudioAnalyzer analyzer;
    analyzer.setSampleRate(48000);
    analyzer.reset();
    std::vector<float> burst(1024);
    for (std::size_t i = 0; i < burst.size(); ++i) {
      burst[i] = 0.8f * std::sin(2.0 * 3.14159265358979 * 1000.0 * static_cast<double>(i) / 48000.0);
    }
    analyzer.push(burst.data(), 1024, 1);
    std::vector<float> silence(60000, 0.0f);
    analyzer.push(silence.data(), static_cast<int>(silence.size()), 1);

    const std::int64_t pushed = analyzer.pushedFrames();
    const auto recent = analyzer.analyze(0.066, 0);
    float recentMax = 0.0f;
    for (float v : recent.bands) recentMax = std::max(recentMax, v);

    const auto delayed = analyzer.analyze(0.066, pushed - 512);
    float delayedMax = 0.0f;
    for (float v : delayed.bands) delayedMax = std::max(delayedMax, v);

    // Instância limpa: a suavização guarda o quadro anterior e mascararia o silêncio.
    wamp::AudioAnalyzer fresh;
    fresh.setSampleRate(48000);
    fresh.reset();
    fresh.push(burst.data(), 1024, 1);
    fresh.push(silence.data(), static_cast<int>(silence.size()), 1);
    const auto past = fresh.analyze(0.066, fresh.pushedFrames());
    float pastMax = 0.0f;
    for (float v : past.bands) pastMax = std::max(pastMax, v);

    check(recentMax < 0.10f, "sem atraso, o analisador ve o silencio recente");
    check(delayedMax > 0.30f, "com atraso de 512, o analisador acha o tom que ja saiu");
    check(pastMax < 0.10f, "atraso maior que o historico cai no silencio");
  }

  // Analisador do visualizador: um tom de 1 kHz precisa dominar a banda vizinha a ele.
  {
    wamp::AudioAnalyzer analyzer;
    analyzer.setSampleRate(48000);
    analyzer.reset();
    std::vector<float> tone(4096);
    for (std::size_t i = 0; i < tone.size(); ++i) {
      tone[i] = 0.8f * std::sin(2.0 * 3.14159265358979 * 1000.0 * static_cast<double>(i) / 48000.0);
    }
    analyzer.push(tone.data(), static_cast<int>(tone.size()), 1);
    const auto frame = analyzer.analyze(0.066);
    std::size_t loudest = 0;
    for (std::size_t i = 1; i < frame.bands.size(); ++i) {
      if (frame.bands[i] > frame.bands[loudest]) loudest = i;
    }
    // ~1 kHz em 20 bandas log de 25 Hz a 16 kHz cai na casa do índice 13.
    check(loudest >= 11 && loudest <= 15, "analisador acha o tom de 1 kHz na banda certa");
    check(frame.levelL > 0.5f, "medidor do canal esquerdo responde ao tom");
    // Mono toca nos dois lados: os medidores ficam iguais, e é o esperado.
    check(std::abs(frame.levelL - frame.levelR) < 0.02f, "medidores empatam com entrada mono");

    // Estéreo com o canal direito mudo: só o esquerdo acende.
    std::vector<float> stereo(4096 * 2, 0.0f);
    for (std::size_t i = 0; i < 4096; ++i) {
      stereo[i * 2] = tone[i];
    }
    wamp::AudioAnalyzer two;
    two.setSampleRate(48000);
    two.reset();
    two.push(stereo.data(), 4096, 2);
    const auto pair = two.analyze(0.066);
    check(pair.levelL > 0.5f && pair.levelR < 0.1f, "estéreo separa os canais");
  }

  std::printf("\n%s\n", failures == 0 ? "tudo certo" : "houve falhas");
  return failures == 0 ? 0 : 1;
}
