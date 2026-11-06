#pragma once
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>

/// @p text as a quoted string of sprat-cli's text formats: quotes and backslashes are
/// escaped, and line breaks, which can't be represented (each record is one line), become
/// spaces.
inline QString cliQuoted(QString text) {
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char('"'), QLatin1String("\\\""));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QLatin1Char('"') + text + QLatin1Char('"');
}
