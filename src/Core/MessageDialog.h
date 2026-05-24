#pragma once

#include <QMessageBox>
#include <QStyle>
#include <QIcon>
#include <QStringList>

class QWidget;
class QString;

class MessageDialog {
public:
    static void information(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle = QString());
    static void warning(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle = QString());
    static void critical(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle = QString());
    static QMessageBox::StandardButton question(
        QWidget* parent,
        const QString& title,
        const QString& text,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::StandardButton defaultButton = QMessageBox::Cancel,
        const QString& windowTitle = QString());

    static QMessageBox::StandardButton confirmWarning(
        QWidget* parent,
        const QString& title,
        const QString& text,
        QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
        QMessageBox::StandardButton defaultButton = QMessageBox::No,
        const QString& windowTitle = QString());

    static int customQuestion(
        QWidget* parent,
        const QString& title,
        const QString& text,
        const QStringList& buttons,
        int defaultButtonIndex = 0,
        const QString& windowTitle = QString(),
        const QList<QIcon>& icons = {});

private:
    static int execCustom(
        QWidget* parent,
        QStyle::StandardPixmap icon,
        const QString& title,
        const QString& text,
        const QString& windowTitle,
        const QStringList& buttons,
        int defaultButtonIndex,
        const QList<QIcon>& buttonIcons);

    static QMessageBox::StandardButton exec(
        QWidget* parent,
        QStyle::StandardPixmap icon,
        const QString& title,
        const QString& text,
        const QString& windowTitle,
        QMessageBox::StandardButtons buttons,
        QMessageBox::StandardButton defaultButton);
};
