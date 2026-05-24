#include "MessageDialog.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

void MessageDialog::information(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle) {
    exec(parent, QStyle::SP_MessageBoxInformation, title, text, windowTitle, QMessageBox::Ok, QMessageBox::Ok);
}

void MessageDialog::warning(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle) {
    exec(parent, QStyle::SP_MessageBoxWarning, title, text, windowTitle, QMessageBox::Ok, QMessageBox::Ok);
}

void MessageDialog::critical(QWidget* parent, const QString& title, const QString& text, const QString& windowTitle) {
    exec(parent, QStyle::SP_MessageBoxCritical, title, text, windowTitle, QMessageBox::Ok, QMessageBox::Ok);
}

QMessageBox::StandardButton MessageDialog::question(
    QWidget* parent,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    const QString& windowTitle)
{
    return exec(parent, QStyle::SP_MessageBoxQuestion, title, text, windowTitle, buttons, defaultButton);
}

QMessageBox::StandardButton MessageDialog::confirmWarning(
    QWidget* parent,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    const QString& windowTitle)
{
    return exec(parent, QStyle::SP_MessageBoxWarning, title, text, windowTitle, buttons, defaultButton);
}

int MessageDialog::customQuestion(
    QWidget* parent,
    const QString& title,
    const QString& text,
    const QStringList& buttons,
    int defaultButtonIndex,
    const QString& windowTitle,
    const QList<QIcon>& icons)
{
    return execCustom(parent, QStyle::SP_MessageBoxQuestion, title, text, windowTitle, buttons, defaultButtonIndex, icons);
}

int MessageDialog::execCustom(
    QWidget* parent,
    QStyle::StandardPixmap iconType,
    const QString& title,
    const QString& text,
    const QString& windowTitle,
    const QStringList& buttons,
    int defaultButtonIndex,
    const QList<QIcon>& buttonIcons)
{
    QDialog dlg(parent);

    QString finalWindowTitle = windowTitle;
    if (finalWindowTitle.isEmpty()) {
        finalWindowTitle = QApplication::translate("MessageDialog", "Question");
    }

    dlg.setWindowTitle(finalWindowTitle);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumWidth(400);

    auto* iconLabel = new QLabel(&dlg);
    iconLabel->setPixmap(QApplication::style()->standardIcon(iconType).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* titleLabel = new QLabel(title, &dlg);
    titleLabel->setWordWrap(true);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont boldFont = titleLabel->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 2);
    titleLabel->setFont(boldFont);

    auto* textLabel = new QLabel(text, &dlg);
    textLabel->setWordWrap(true);
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(8);
    textLayout->addWidget(titleLabel);
    if (!text.isEmpty()) {
        textLayout->addWidget(textLabel);
    }
    textLayout->addStretch();

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(20);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(iconLabel);
    contentLayout->addLayout(textLayout, 1);

    auto* buttonBox = new QDialogButtonBox(&dlg);
    int result = -1;

    for (int i = 0; i < buttons.size(); ++i) {
        QPushButton* btn = buttonBox->addButton(buttons[i], QDialogButtonBox::ActionRole);
        if (i < buttonIcons.size() && !buttonIcons[i].isNull()) {
            btn->setIcon(buttonIcons[i]);
        }
        if (i == defaultButtonIndex) {
            btn->setDefault(true);
            btn->setFocus();
        }
        QObject::connect(btn, &QPushButton::clicked, &dlg, [&result, i, &dlg]() {
            result = i;
            dlg.accept();
        });
    }

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(24, 24, 24, 16);
    mainLayout->setSpacing(24);
    mainLayout->addLayout(contentLayout);
    mainLayout->addWidget(buttonBox);

    dlg.exec();
    return result;
}

QMessageBox::StandardButton MessageDialog::exec(
    QWidget* parent,
    QStyle::StandardPixmap iconType,
    const QString& title,
    const QString& text,
    const QString& windowTitle,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton)
{
    QDialog dlg(parent);

    QString finalWindowTitle = windowTitle;
    if (finalWindowTitle.isEmpty()) {
        switch (iconType) {
            case QStyle::SP_MessageBoxInformation: finalWindowTitle = QApplication::translate("MessageDialog", "Information"); break;
            case QStyle::SP_MessageBoxWarning: finalWindowTitle = QApplication::translate("MessageDialog", "Warning"); break;
            case QStyle::SP_MessageBoxCritical: finalWindowTitle = QApplication::translate("MessageDialog", "Error"); break;
            case QStyle::SP_MessageBoxQuestion: finalWindowTitle = QApplication::translate("MessageDialog", "Question"); break;
            default: finalWindowTitle = QApplication::translate("MessageDialog", "Message"); break;
        }
    }

    dlg.setWindowTitle(finalWindowTitle);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumWidth(400);

    auto* iconLabel = new QLabel(&dlg);
    iconLabel->setPixmap(QApplication::style()->standardIcon(iconType).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* titleLabel = new QLabel(title, &dlg);
    titleLabel->setWordWrap(true);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont boldFont = titleLabel->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 2);
    titleLabel->setFont(boldFont);

    auto* textLabel = new QLabel(text, &dlg);
    textLabel->setWordWrap(true);
    textLabel->setTextFormat(Qt::RichText); // Allow basic formatting in text if needed
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(8);
    textLayout->addWidget(titleLabel);
    if (!text.isEmpty()) {
        textLayout->addWidget(textLabel);
    }
    textLayout->addStretch();

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(20);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(iconLabel);
    contentLayout->addLayout(textLayout, 1);

    auto* buttonBox = new QDialogButtonBox(
        static_cast<QDialogButtonBox::StandardButtons>(static_cast<int>(buttons)), &dlg);

    QMessageBox::StandardButton result = defaultButton;
    for (QAbstractButton* btn : buttonBox->buttons()) {
        const auto stdBtn = buttonBox->standardButton(btn);
        QObject::connect(btn, &QAbstractButton::clicked, &dlg, [&result, stdBtn, &dlg]() {
            result = static_cast<QMessageBox::StandardButton>(static_cast<int>(stdBtn));
            dlg.accept();
        });
    }

    if (defaultButton != QMessageBox::NoButton) {
        auto* defBtn = buttonBox->button(
            static_cast<QDialogButtonBox::StandardButton>(static_cast<int>(defaultButton)));
        if (defBtn) {
            defBtn->setDefault(true);
            defBtn->setFocus();
        }
    }

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(24, 24, 24, 16);
    mainLayout->setSpacing(24);
    mainLayout->addLayout(contentLayout);
    mainLayout->addWidget(buttonBox);

    dlg.exec();
    return result;
}
