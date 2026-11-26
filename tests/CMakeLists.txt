# Testes que não abrem áudio, janela nem rede — os únicos que podem rodar no CI.
add_executable(amplia-work-key-test work_key_test.cpp)
target_link_libraries(amplia-work-key-test PRIVATE wamp-library)
target_compile_options(amplia-work-key-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME work_key COMMAND amplia-work-key-test)
