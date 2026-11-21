# Integração com o sistema (hoje: MPRIS2 pelo barramento de sessão).
add_library(wamp-platform STATIC
  src/mpris_server.cpp
  include/wamp/mpris_server.h
)

target_include_directories(wamp-platform
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

target_link_libraries(wamp-platform
  PUBLIC
    wamp-core
    Qt6::Core
    PkgConfig::SYSTEMD
)

target_compile_options(wamp-platform PRIVATE -Wall -Wextra)
