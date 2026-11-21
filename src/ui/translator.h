#pragma once

#include <QString>
#include <QStringList>

namespace wamp::ui {

/// Idioma pedido pelo usuário: "sistema", "pt_BR" (o idioma-fonte, sem tradução) ou
/// um código como "en". Lido da INI antes de a janela existir.
QString languageFromSettings();

/// Mesmo caminho usado pela janela; exposto para o escape de reinício em testes.
QString settingsFileForTests();

/// Instala o tradutor do idioma pedido. Chamar **antes** de construir qualquer widget:
/// as strings em `tr()` são resolvidas no momento em que o objeto nasce, não no show.
void installTranslators(const QString& requested);

/// Idiomas oferecidos no seletor das preferências. O primeiro é o padrão.
QStringList availableLanguages();

/// Nome legível do idioma ("Português (Brasil)", "English", "Sistema").
QString languageLabel(const QString& id);

}  // namespace wamp::ui
