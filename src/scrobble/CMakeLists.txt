add_library(wamp-scrobble STATIC
  src/scrobbler.cpp
  include/wamp/scrobbler.h
  include/wamp/scrobble_types.h
)

target_include_directories(wamp-scrobble
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(wamp-scrobble
  PUBLIC
    Qt6::Core
    Qt6::Network
)

target_compile_options(wamp-scrobble PRIVATE -Wall -Wextra)
