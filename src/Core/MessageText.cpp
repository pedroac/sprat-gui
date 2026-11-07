#include "MessageText.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace {
const QString BLOCK_INDENT = QStringLiteral("  ");

bool isBlock(const QStringList& lines) {
    for (const QString& line : lines) {
        if (!line.startsWith(BLOCK_INDENT) && !line.startsWith(QLatin1Char('\t'))) return false;
    }
    return !lines.isEmpty();
}
}  // namespace

namespace MessageText {

QString toolOutput(const QString& output, int maxLines) {
    QStringList lines;
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        QString trimmed = line;
        trimmed.remove(QLatin1Char('\r'));
        if (!trimmed.trimmed().isEmpty()) lines.append(BLOCK_INDENT + trimmed.trimmed());
    }
    if (lines.size() > maxLines) {
        const int hidden = int(lines.size()) - maxLines;
        lines = lines.mid(0, maxLines);
        lines.append(BLOCK_INDENT + QCoreApplication::translate("MessageText", "… %n more line(s)", nullptr, hidden));
    }
    return lines.join(QLatin1Char('\n'));
}

QString withToolOutput(const QString& message, const QString& output) {
    const QString block = toolOutput(output);
    if (block.isEmpty()) return message;
    return message + QStringLiteral("\n\n") + block;
}

QString toHtml(const QString& text) {
    if (text.startsWith(QLatin1String("<qt>"))) return text;
    static const QRegularExpression paragraphBreak(QStringLiteral("\\n[ \\t]*\\n"));
    QString html = QStringLiteral("<qt>");
    for (const QString& paragraph : text.split(paragraphBreak, Qt::SkipEmptyParts)) {
        QStringList lines = paragraph.split(QLatin1Char('\n'));
        while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
        if (lines.isEmpty()) continue;
        const bool block = isBlock(lines);
        QStringList escaped;
        for (QString line : lines) {
            if (block) line = line.startsWith(QLatin1Char('\t')) ? line.mid(1) : line.mid(BLOCK_INDENT.size());
            escaped.append(line.toHtmlEscaped());
        }
        html += block ? QStringLiteral("<p style=\"font-family: monospace; white-space: pre-wrap;\">")
                      : QStringLiteral("<p>");
        html += escaped.join(QStringLiteral("<br>")) + QStringLiteral("</p>");
    }
    return html + QStringLiteral("</qt>");
}

}  // namespace MessageText
