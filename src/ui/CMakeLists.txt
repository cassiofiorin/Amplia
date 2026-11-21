add_executable(wamp
  click_slider.h
  main.cpp
  main_window.cpp
  main_window.h
  display_panel.cpp
  display_panel.h
  equalizer_dialog.cpp
  eq_curve_view.cpp
  icons.cpp
  round_button.cpp
  eq_curve_view.h
  file_info_dialog.cpp
  equalizer_dialog.h
  file_info_dialog.h
  jump_dialog.cpp
  playlist_delegate.cpp
  playlist_delegate.h
  preferences_dialog.cpp
  preferences_dialog.h
  theme.cpp
  theme_store.cpp
  theme_editor.cpp
  theme_editor.h
  theme.h
  translator.cpp
  translator.h
)

# Versão exibida na barra de status: número do projeto + commit. Header gerado evita
# a dança de aspas das definições de compilação (-DAMPLIA_VERSION=0.1.0 perdia as aspas).
configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

set_target_properties(wamp PROPERTIES OUTPUT_NAME amplia)

target_link_libraries(wamp PRIVATE wamp-core wamp-platform wamp-library wamp-tags wamp-scrobble Qt6::Widgets)
target_compile_options(wamp PRIVATE -Wall -Wextra)
target_include_directories(wamp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS wamp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
  "${CMAKE_SOURCE_DIR}/packaging/io.github.cassiofiorin.Amplia.desktop"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES
  "${CMAKE_SOURCE_DIR}/packaging/io.github.cassiofiorin.Amplia.svg"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# Traduções: o .ts vive em i18n/ e o lrelease embute o .qm no binário. O caminho avulso
# (AppDataLocation/i18n) é tentado antes, para quem traduz sem recompilar.
set(AMPLIA_TS_FILES
  "${CMAKE_SOURCE_DIR}/i18n/amplia_en.ts"
)
qt_add_translations(wamp TS_FILES ${AMPLIA_TS_FILES} RESOURCE_PREFIX "/i18n")
