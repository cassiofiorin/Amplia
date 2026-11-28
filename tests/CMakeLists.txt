# Testes que não abrem áudio, janela nem rede — os únicos que podem rodar no CI.
add_executable(amplia-work-key-test work_key_test.cpp)
target_link_libraries(amplia-work-key-test PRIVATE wamp-library)
target_compile_options(amplia-work-key-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME work_key COMMAND amplia-work-key-test)

add_executable(amplia-shuffle-history-test shuffle_history_test.cpp)
target_link_libraries(amplia-shuffle-history-test PRIVATE wamp-core)
target_compile_options(amplia-shuffle-history-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shuffle_history COMMAND amplia-shuffle-history-test)

add_executable(amplia-autotag-test autotag_test.cpp)
target_link_libraries(amplia-autotag-test PRIVATE wamp-library)
target_compile_options(amplia-autotag-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME autotag COMMAND amplia-autotag-test)

# O analisador de resposta do MusicBrainz não abre socket: recebe JSON gravado. Precisa do
# QtCore/QtNetwork por causa dos tipos, mas nenhuma janela.
add_executable(amplia-musicbrainz-parse-test
  musicbrainz_parse_test.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/musicbrainz.cpp)
target_include_directories(amplia-musicbrainz-parse-test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/ui
  ${CMAKE_SOURCE_DIR}/src/library/include)
target_link_libraries(amplia-musicbrainz-parse-test PRIVATE Qt6::Core Qt6::Network wamp-library)
target_compile_options(amplia-musicbrainz-parse-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME musicbrainz_parse COMMAND amplia-musicbrainz-parse-test)

# O diário do autotag é JSON e disco, sem janela: precisa do QtCore e de um diretório temporário.
add_executable(amplia-tag-journal-test
  tag_journal_test.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/tag_journal.cpp)
target_include_directories(amplia-tag-journal-test PRIVATE ${CMAKE_SOURCE_DIR}/src/ui)
target_link_libraries(amplia-tag-journal-test PRIVATE Qt6::Core)
target_compile_options(amplia-tag-journal-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tag_journal COMMAND amplia-tag-journal-test)

# O carimbo de "tag corrigida pelo app" (D-153) precisa de banco de verdade: abre um temporário.
add_executable(amplia-tag-fixed-test tag_fixed_at_test.cpp)
target_link_libraries(amplia-tag-fixed-test PRIVATE wamp-library)
add_test(NAME tag_fixed_at COMMAND amplia-tag-fixed-test)
