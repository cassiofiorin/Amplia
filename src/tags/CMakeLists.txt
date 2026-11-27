pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

add_library(wamp-tags STATIC
  src/tag_editor.cpp
  src/remux_writer.cpp
  include/wamp/tag_editor.h
  include/wamp/remux_writer.h
)

target_include_directories(wamp-tags PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(wamp-tags PUBLIC wamp-core PRIVATE PkgConfig::TAGLIB PkgConfig::FFMPEG)

target_compile_options(wamp-tags PRIVATE -Wall -Wextra -Wpedantic)
