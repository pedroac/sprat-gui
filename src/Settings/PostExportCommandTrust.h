#pragma once

#include "CliToolsConfig.h"

#include <QCryptographicHash>
#include <QSettings>
#include <QStringList>

/// Post-export commands this user has typed or approved on this machine.
///
/// The command is stored in the project file, so a project from someone else could run any
/// shell command on export. Commands are only run without asking once the user typed them
/// or confirmed them; approvals are kept as SHA-256 hashes in the app's settings file.
class PostExportCommandTrust {
public:
    static bool isTrusted(const QString& command) {
        return settingsList().contains(hash(command));
    }

    static void trust(const QString& command) {
        if (command.trimmed().isEmpty() || isTrusted(command))
            return;
        QSettings settings(CliToolsConfig::configPath(), QSettings::IniFormat);
        QStringList hashes = settings.value(kSettingsKey).toStringList();
        hashes.append(hash(command));
        settings.setValue(kSettingsKey, hashes);
    }

private:
    static constexpr const char* kSettingsKey = "export/trusted_post_export_commands";

    static QString hash(const QString& command) {
        return QString::fromLatin1(
            QCryptographicHash::hash(command.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex());
    }

    static QStringList settingsList() {
        QSettings settings(CliToolsConfig::configPath(), QSettings::IniFormat);
        return settings.value(kSettingsKey).toStringList();
    }
};
