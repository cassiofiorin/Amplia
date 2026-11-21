#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "theme.h"

/// Temas personalizados em disco: um CSV por tema, dentro de `presets/themes`.
/// O mesmo CSV serve para compartilhar: um arquivo pode trazer vários temas (até 30).
namespace wamp::ui {

/// Teto de temas personalizados: cinco embutidos + "Personalizado" não contam.
inline constexpr int kMaxSavedThemes = 30;

QString themesDir();
QString themeCsvHeader();
QString themeCsvSample();

/// Uma linha do CSV (sem cabeçalho) para um tema personalizado.
QString themeToCsvRow(const Theme& theme);

/// Aceita uma linha do CSV. `line` pode vir com comentário (`#`) ou vazia: devolve false
/// e não é erro — quem chama decide se avisa.
bool themeFromCsvRow(const QString& line, Theme* out, QString* error);

QVector<Theme> loadSavedThemes();
bool saveThemeFile(const Theme& theme, QString* error);
bool removeThemeFile(const QString& id, QString* error);

/// Importa um CSV (várias linhas). Devolve quantos entraram; `warnings` recebe o motivo de
/// cada linha recusada. Recusa o arquivo inteiro se estourar o teto.
int importThemesCsv(const QString& file, QStringList* warnings, QString* error);
bool exportThemesCsv(const QString& file, QString* error);

/// Nome de arquivo seguro a partir do nome do tema.
QString themeSlug(const QString& name);

}  // namespace wamp::ui
