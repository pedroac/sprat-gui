#pragma once
#include <QString>

/// Plain-text messages for dialogs and logs. A blank line separates paragraphs (one idea
/// each: what went wrong, why, what to do), and a paragraph whose lines are all indented by
/// two spaces is a block shown apart in a fixed-width font: what a tool printed, or a command
/// to run. Pure: MessageDialog turns the text into HTML with toHtml().
namespace MessageText {

/// @p output (what a tool such as spratlayout printed) as a block of its own: each line
/// indented, blank lines dropped, and cut to its first @p maxLines lines with a note of how
/// many were left out.
QString toolOutput(const QString& output, int maxLines = 12);

/// @p message, then @p output as a block of its own (see toolOutput()). With no output,
/// just @p message.
QString withToolOutput(const QString& message, const QString& output);

/// @p text as HTML for a dialog: each paragraph a <p>, line breaks kept, indented blocks in
/// a fixed-width font, and everything escaped, so text such as "<stdin>" shows as written.
/// Text that already starts with "<qt>" is returned as it is.
QString toHtml(const QString& text);

}  // namespace MessageText
