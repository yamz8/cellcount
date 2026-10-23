#pragma once

#include <QStringList>

// Commands for scripts and agents that need no display: cellcount check,
// summary, detect, export and skill. Follows Hype's cli.cpp.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
