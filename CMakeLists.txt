cmake_minimum_required(VERSION 3.25)
project(Amplia VERSION 1.0.0 LANGUAGES CXX)

# Versão mostrada na barra de status: número do projeto + commit curto, quando houver git.
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE AMPLIA_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT AMPLIA_GIT_HASH)
  set(AMPLIA_GIT_HASH "sem-git")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libswresample libavutil)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/scrobble)
add_subdirectory(src/library)
add_subdirectory(src/tags)
add_subdirectory(src/ui)

add_subdirectory(tools)
