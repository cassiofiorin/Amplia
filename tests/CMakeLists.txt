# Testes que não abrem áudio, janela nem rede — os únicos que podem rodar no CI.
add_executable(amplia-work-key-test work_key_test.cpp)
target_link_libraries(amplia-work-key-test PRIVATE wamp-library)
target_compile_options(amplia-work-key-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME work_key COMMAND amplia-work-key-test)

add_executable(amplia-shuffle-history-test shuffle_history_test.cpp)
target_link_libraries(amplia-shuffle-history-test PRIVATE wamp-core)
target_compile_options(amplia-shuffle-history-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shuffle_history COMMAND amplia-shuffle-history-test)
