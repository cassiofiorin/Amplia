add_executable(wamp
  click_slider.h
  main.cpp
  main_window.cpp
  main_window.h
  display_panel.cpp
  display_panel.h
  equalizer_dialog.cpp
  eq_curve_view.cpp
  check_style.cpp
  library_window.cpp
  confirm_dialog.cpp
  library_window.h
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
  help_dialog.cpp
  help_dialog.h
  about_dialog.cpp
  about_dialog.h
)

# Versão exibida na barra de status: número do projeto + commit. Header gerado evita
# a dança de aspas das definições de compilação (-DAMPLIA_VERSION=0.1.0 perdia as aspas).
# Canal da versão (o sufixo "rc2" de 0.9.0-rc2). Vazio quando o pacote for final.
set(AMPLIA_RELEASE "rc2")
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

# Ajuda: Markdown puro em help/, embutido nos recursos. Quem traduz edita um arquivo de
# texto — nenhum código é tocado, e o README linka o mesmo arquivo.
qt_add_resources(wamp "help"
  PREFIX "/help"
  BASE "${CMAKE_SOURCE_DIR}/help"
  FILES
    "${CMAKE_SOURCE_DIR}/help/ajuda-pt.md"
    "${CMAKE_SOURCE_DIR}/help/ajuda-en.md"
    "${CMAKE_SOURCE_DIR}/help/idiomas.txt"
)
