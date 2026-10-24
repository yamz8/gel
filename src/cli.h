#pragma once

#include <QStringList>

// Headless commands for scripts and agents: gel lanes, quant, export, check,
// and skill. None of them needs a display.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
