/// @file MainWindow.SmokeTest.cpp
/// Import flow driven by --smoke-test: opens a ZIP, answers the folder picker like a user
/// would, and waits for the layout to reach the canvas.

#include "MainWindow.h"
#include "LayoutOrchestrator.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <memory>

namespace {
constexpr int SMOKE_TEST_POLL_INTERVAL_MS = 100;
const QString FOLDER_PICKER_OBJECT_NAME = QStringLiteral("frameFolderPickerDialog");

/// Ticks the requested folders (or the top-level ones) and presses OK.
/// @return The folders ticked, or an error message in error
QStringList answerFolderPicker(QDialog& dialog, const QStringList& folders, QString& error) {
    auto* tree = dialog.findChild<QTreeWidget*>();
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QPushButton* okButton = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
    if (!tree || !okButton) {
        error = QObject::tr("the folder picker has no folder tree or OK button");
        return {};
    }

    QHash<QString, QTreeWidgetItem*> itemsByPath;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        itemsByPath.insert((*it)->data(0, Qt::UserRole).toString(), *it);
    }

    QStringList toPick = folders;
    if (toPick.isEmpty()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            toPick << tree->topLevelItem(i)->data(0, Qt::UserRole).toString();
        }
    }
    for (const QString& folder : toPick) {
        QTreeWidgetItem* item = itemsByPath.value(folder);
        if (!item) {
            QStringList available = itemsByPath.keys();
            available.sort();
            error = QObject::tr("folder '%1' is not in the ZIP (folders: %2)")
                        .arg(folder, available.join(QStringLiteral(", ")));
            return {};
        }
        item->setCheckState(0, Qt::Checked);
    }
    if (!okButton->isEnabled()) {
        error = QObject::tr("the folder picker's OK button stayed disabled");
        return {};
    }
    okButton->click();
    return toPick;
}

/// Window title plus label text, so an unexpected dialog can be reported.
QString describeDialog(const QWidget& dialog) {
    QStringList parts{dialog.windowTitle()};
    for (const QLabel* label : dialog.findChildren<QLabel*>()) {
        if (!label->text().trimmed().isEmpty()) parts << label->text().trimmed();
    }
    return parts.join(QStringLiteral(": "));
}
}

void MainWindow::runSmokeTestImport(const QString& zipPath, const QString& profile, const QStringList& folders,
                                    std::function<void(const SmokeTestImportResult&)> done) {
    auto finished = std::make_shared<bool>(false);
    auto pickedFolders = std::make_shared<QStringList>();
    auto connections = std::make_shared<QVector<QMetaObject::Connection>>();

    auto finish = [this, finished, pickedFolders, connections, done](SmokeTestImportResult result) {
        if (*finished) return;
        *finished = true;
        for (const QMetaObject::Connection& connection : *connections) {
            disconnect(connection);
        }
        result.profile = m_currentProfile;
        result.pickedFolders = *pickedFolders;
        done(result);
    };
    auto fail = [finish](const QString& error) {
        SmokeTestImportResult result;
        result.error = error;
        finish(result);
    };

    if (!QFileInfo(zipPath).isFile()) {
        fail(tr("ZIP not found: %1").arg(zipPath));
        return;
    }

    // Modal dialogs run their own event loop, so this timer keeps firing while one is open.
    auto watchDialogs = [finished, pickedFolders, folders, fail]() {
        QWidget* modal = QApplication::activeModalWidget();
        if (*finished || !modal || !modal->isVisible()) return;
        auto* picker = qobject_cast<QDialog*>(modal);
        if (picker && picker->objectName() == FOLDER_PICKER_OBJECT_NAME && pickedFolders->isEmpty()) {
            QString error;
            *pickedFolders = answerFolderPicker(*picker, folders, error);
            if (!error.isEmpty()) fail(error);
            return;
        }
        fail(tr("unexpected dialog: %1").arg(describeDialog(*modal)));
    };

    auto startImport = [this, zipPath, profile, connections, finish, fail]() {
        auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
        const int profileIndex = profileCombo ? profileCombo->findData(profile) : -1;
        if (profileIndex < 0) {
            QStringList available;
            for (int i = 0; profileCombo && i < profileCombo->count(); ++i) {
                available << profileCombo->itemData(i).toString();
            }
            fail(tr("profile '%1' not found (available: %2)").arg(profile, available.join(", ")));
            return;
        }
        {
            // Select the profile the way a profile fallback does, without an undo entry.
            const QSignalBlocker blocker(profileCombo);
            profileCombo->setCurrentIndex(profileIndex);
        }
        m_currentProfile = profile;
        m_layoutOrchestrator->clearProfilesTried();
        m_layoutOrchestrator->setCurrentProfile(profile);

        connections->append(connect(m_layoutOrchestrator, &LayoutOrchestrator::layoutAppliedToCanvas,
                                    this, [this, profile, finish]() {
            SmokeTestImportResult result;
            for (const LayoutModel& model : m_session->activeAtlas().layoutModels) {
                result.spriteCount += model.sprites.size();
                result.atlasSizes << QStringLiteral("%1 x %2").arg(model.atlasWidth).arg(model.atlasHeight);
            }
            // The CLI-ready rebuild of the empty project lands here too; keep waiting for the ZIP.
            if (result.spriteCount == 0) return;
            if (m_currentProfile != profile) {
                result.error = tr("layout fell back from profile '%1' to '%2'").arg(profile, m_currentProfile);
            }
            finish(result);
        }));
        connections->append(connect(m_layoutOrchestrator, &LayoutOrchestrator::layoutFailed,
                                    this, [fail](const QString& details) {
            fail(tr("layout failed: %1").arg(details));
        }));

        if (!loadImagesFromZip(zipPath, DropAction::Replace)) {
            fail(tr("the ZIP import did not start"));
        }
    };

    // The CLI check finishes after startup, and loadImagesFromZip() refuses to run before it.
    auto importStarted = std::make_shared<bool>(false);
    auto* poll = new QTimer(this);
    poll->setInterval(SMOKE_TEST_POLL_INTERVAL_MS);
    connect(poll, &QTimer::timeout, this,
            [this, poll, finished, importStarted, startImport, watchDialogs]() {
        if (*finished) {
            poll->stop();
            poll->deleteLater();
            return;
        }
        if (*importStarted) {
            watchDialogs();
            return;
        }
        if (!m_cliReady || m_isLoading) return;
        *importStarted = true;
        startImport();
    });
    poll->start();
}
