#pragma once
#include <QStringView>

/// Compares like a person numbering frames: digit runs by value ("walk2" < "walk10"), the rest
/// case-insensitively. Hand-written because QCollator's numeric mode depends on the Qt build
/// and the system locale (without ICU it is ignored), and the order must be the same everywhere.
/// Returns a negative number, zero or a positive number, as a < b, a == b or a > b.
inline int compareNaturally(QStringView a, QStringView b) {
    const auto isAsciiDigit = [](QChar c) { return c >= u'0' && c <= u'9'; };
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (isAsciiDigit(a[i]) && isAsciiDigit(b[j])) {
            const qsizetype runStartA = i;
            const qsizetype runStartB = j;
            while (i < a.size() && isAsciiDigit(a[i])) ++i;
            while (j < b.size() && isAsciiDigit(b[j])) ++j;
            QStringView digitsA = a.mid(runStartA, i - runStartA);
            QStringView digitsB = b.mid(runStartB, j - runStartB);
            while (digitsA.size() > 1 && digitsA.front() == u'0') digitsA = digitsA.mid(1);
            while (digitsB.size() > 1 && digitsB.front() == u'0') digitsB = digitsB.mid(1);
            // Without leading zeros, the longer run is the bigger number; equal lengths compare
            // digit by digit. No integer conversion, so long runs can't overflow.
            if (digitsA.size() != digitsB.size()) {
                return digitsA.size() < digitsB.size() ? -1 : 1;
            }
            if (const int order = digitsA.compare(digitsB); order != 0) {
                return order;
            }
            continue;
        }
        const QChar foldedA = a[i].toCaseFolded();
        const QChar foldedB = b[j].toCaseFolded();
        if (foldedA != foldedB) {
            return foldedA < foldedB ? -1 : 1;
        }
        ++i;
        ++j;
    }
    const qsizetype restA = a.size() - i;
    const qsizetype restB = b.size() - j;
    return restA == restB ? 0 : (restA < restB ? -1 : 1);
}
