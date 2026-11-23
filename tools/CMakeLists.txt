# Teste de fumaça do pipeline (sem GUI). Instalado no app para poder rodar dentro do
# sandbox: flatpak run --command=wamp-smoke io.github.cassiofiorin.Amplia <arquivo>
add_executable(wamp-smoke smoke.cpp)

target_link_libraries(wamp-smoke PRIVATE wamp-core wamp-scrobble wamp-library wamp-tags)

target_compile_options(wamp-smoke PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(wamp-smoke PROPERTIES OUTPUT_NAME amplia-smoke)

install(TARGETS wamp-smoke RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Renderiza a lista da playlist num PNG (sem janela), para conferir o desenho das
# colunas de número e de fila: flatpak run --command=wamp-ui-preview ... /tmp/x.png
add_executable(wamp-ui-preview
  preview.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/display_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/display_panel.h
  ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.h
  ${CMAKE_SOURCE_DIR}/src/ui/theme_store.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme_store.h
  ${CMAKE_SOURCE_DIR}/src/ui/equalizer_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/eq_curve_view.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/translator.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme_editor.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/icons.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/playlist_delegate.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/file_info_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/check_style.cpp
)

target_include_directories(wamp-ui-preview PRIVATE ${CMAKE_SOURCE_DIR}/src/ui)
target_link_libraries(wamp-ui-preview PRIVATE wamp-core wamp-scrobble wamp-tags Qt6::Widgets)
target_compile_options(wamp-ui-preview PRIVATE -Wall -Wextra)

set_target_properties(wamp-ui-preview PROPERTIES OUTPUT_NAME amplia-ui-preview)

# Os alvos de teste desenham as mesmas telas, então precisam do mesmo .qm embutido.
qt_add_translations(wamp-ui-preview TS_FILES ${CMAKE_SOURCE_DIR}/i18n/amplia_en.ts RESOURCE_PREFIX "/i18n")

install(TARGETS wamp-ui-preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Testes dos widgets de interação (slider de posição). Também sem janela visível:
# flatpak run --env=QT_QPA_PLATFORM=offscreen --command=wamp-ui-smoke io.github.cassiofiorin.Amplia
add_executable(wamp-ui-smoke
  ui_smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/display_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/display_panel.h
  ${CMAKE_SOURCE_DIR}/src/ui/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/translator.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.h
  ${CMAKE_SOURCE_DIR}/src/ui/theme_store.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme_store.h
  ${CMAKE_SOURCE_DIR}/src/ui/equalizer_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/eq_curve_view.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/icons.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/eq_curve_view.h
  ${CMAKE_SOURCE_DIR}/src/ui/jump_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/jump_dialog.h
)

target_include_directories(wamp-ui-smoke PRIVATE ${CMAKE_SOURCE_DIR}/src/ui
                                                  ${CMAKE_SOURCE_DIR}/src/library/include)
target_link_libraries(wamp-ui-smoke PRIVATE wamp-core wamp-library wamp-scrobble Qt6::Widgets)
target_compile_options(wamp-ui-smoke PRIVATE -Wall -Wextra)

set_target_properties(wamp-ui-smoke PROPERTIES OUTPUT_NAME amplia-ui-smoke)

qt_add_translations(wamp-ui-smoke TS_FILES ${CMAKE_SOURCE_DIR}/i18n/amplia_en.ts RESOURCE_PREFIX "/i18n")

install(TARGETS wamp-ui-smoke RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Sonda de bandeja (não é instalada no app; serve para decidir se vale o agente).
add_executable(wamp-tray-probe tray_probe.cpp)
target_link_libraries(wamp-tray-probe PRIVATE Qt6::Widgets)
set_target_properties(wamp-tray-probe PROPERTIES AUTOMOC OFF)
set_target_properties(wamp-tray-probe PROPERTIES OUTPUT_NAME amplia-tray-probe)

install(TARGETS wamp-tray-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
