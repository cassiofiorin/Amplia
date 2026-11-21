add_library(wamp-library STATIC
  src/tag_cache.cpp
  include/wamp/tag_cache.h
)

target_include_directories(wamp-library
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(wamp-library
  PUBLIC wamp-core
  PRIVATE PkgConfig::SQLITE3
)

target_compile_options(wamp-library PRIVATE -Wall -Wextra -Wpedantic)
