// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#pragma once

#include <QString>

class QWidget;

namespace wamp::ui {

/// Três confirmações em sequência para uma ação destrutiva (o ML do Winamp pede uma;
/// aqui pedimos três, de tamanhos diferentes, para que o botão perigoso não caia
/// duas vezes sob o cursor).
///
/// - a 1ª usa o tamanho padrão e tem **Cancelar** como botão padrão;
/// - a 2ª é 1,5× maior e **não tem botão padrão**: Enter não faz nada, é preciso
///   tabular até o botão;
/// - a 3ª é 1,25× a primeira, também sem padrão, e o botão de prosseguir fica
///   desabilitado por 700 ms — o que quebra a sequência de cliques/Enter repetidos.
///
/// Retorna true só quando as três forem aceitas; Escape cancela em qualquer uma.
bool confirmDestructive(QWidget* parent, const QString& title, const QString& question,
                        const QString& permanence, const QString& actionLabel);

}  // namespace wamp::ui
