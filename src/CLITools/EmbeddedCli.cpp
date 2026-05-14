#include "EmbeddedCli.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <QDebug>
#include "commands/entrypoints.h"

std::atomic<bool> EmbeddedCli::m_stopRequested{false};

void EmbeddedCli::requestStop() {
    m_stopRequested = true;
}

bool EmbeddedCli::isStopRequested() {
    return m_stopRequested.load();
}

void EmbeddedCli::resetStop() {
    m_stopRequested = false;
}

CliResult EmbeddedCli::run(const QString& command, const QStringList& args, const QByteArray& input) {
    resetStop();

    // Prepare arguments
    std::vector<std::string> stdArgs;
    stdArgs.reserve(args.size() + 1);
    stdArgs.push_back(command.toStdString());
    for (const QString& arg : args) {
        stdArgs.push_back(arg.toStdString());
    }

    std::vector<char*> argv;
    argv.reserve(stdArgs.size() + 1);
    for (auto& s : stdArgs) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    int argc = static_cast<int>(stdArgs.size());

    // Redirect stdout/stderr and stdin
    std::stringstream outStream, errStream, inStream;
    if (!input.isEmpty()) {
        inStream.write(input.constData(), input.size());
    }
    std::streambuf* oldOut = std::cout.rdbuf(outStream.rdbuf());
    std::streambuf* oldErr = std::cerr.rdbuf(errStream.rdbuf());
    std::streambuf* oldIn = std::cin.rdbuf(inStream.rdbuf());

    int exitCode = -1;
    try {
        if (command == "spratlayout") {
            exitCode = run_spratlayout(argc, argv.data());
        } else if (command == "spratpack") {
            exitCode = run_spratpack(argc, argv.data());
        } else if (command == "spratconvert") {
            exitCode = run_spratconvert(argc, argv.data());
        } else if (command == "spratframes") {
            exitCode = run_spratframes(argc, argv.data());
        } else if (command == "spratunpack") {
            exitCode = run_spratunpack(argc, argv.data());
        } else {
            errStream << "Unknown embedded command: " << command.toStdString() << "\n";
            exitCode = 1;
        }
    } catch (const std::exception& e) {
        errStream << "Exception in embedded CLI: " << e.what() << "\n";
        exitCode = 1;
    } catch (...) {
        errStream << "Unknown exception in embedded CLI\n";
        exitCode = 1;
    }

    // Restore stdout/stderr and stdin
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
    std::cin.rdbuf(oldIn);

    CliResult result;
    result.exitCode = exitCode;
    result.stdOut = QByteArray::fromStdString(outStream.str());
    result.stdErr = QByteArray::fromStdString(errStream.str());
    return result;
}
