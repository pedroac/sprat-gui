#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QMutex>
#include <atomic>

struct CliResult {
    int exitCode;
    QByteArray stdOut;
    QByteArray stdErr;
};

class EmbeddedCli {
public:
    static CliResult run(const QString& command, const QStringList& args, const QByteArray& input = QByteArray());
    
    // Allows other threads to request cancellation of the current CLI operation
    // Note: The CLI tools must be updated to check this flag if they are long-running
    static void requestStop();
    static bool isStopRequested();
    static void resetStop();

private:
    static std::atomic<bool> m_stopRequested;
    static QMutex m_mutex;
};
