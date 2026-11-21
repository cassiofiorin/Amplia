add_library(wamp-core STATIC
  src/decoder_ffmpeg.cpp
  src/audio_analyzer.cpp
  src/equalizer.cpp
  src/eq_preset.cpp
  src/file_info.cpp
  src/pipewire_output.cpp
  src/metadata.cpp
  src/metadata_scanner.cpp
  src/player_engine.cpp
  src/playlist.cpp
  src/playlist_file.cpp
  src/replay_gain.cpp
)

target_include_directories(wamp-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(wamp-core
  PUBLIC
    PkgConfig::FFMPEG
    PkgConfig::PIPEWIRE
    Threads::Threads
)

target_compile_options(wamp-core PRIVATE -Wall -Wextra -Wpedantic)
