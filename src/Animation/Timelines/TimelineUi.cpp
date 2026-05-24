#include "TimelineUi.h"
#include "MessageDialog.h"

#include <QCoreApplication>
#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <QPushButton>

namespace {
QString trTimelineUi(const char* text) {
    return QCoreApplication::translate("TimelineUi", text);
}
}

TimelineGenerationService::ConflictResolution TimelineUi::askTimelineConflictResolution(QWidget* parent, const QString& timelineName) {
    auto* style_ = QApplication::style();
    const int answer = MessageDialog::customQuestion(
        parent,
        trTimelineUi("Timeline Already Exists"),
        QString(trTimelineUi("A timeline named \"%1\" already exists. What should be done with the generated frames?")).arg(timelineName),
        { trTimelineUi("Replace"), trTimelineUi("Merge"), trTimelineUi("Ignore") },
        2, // Default to Ignore
        trTimelineUi("Timeline Already Exists"),
        {
            style_->standardIcon(QStyle::SP_DialogSaveButton),
            style_->standardIcon(QStyle::SP_DialogApplyButton),
            style_->standardIcon(QStyle::SP_DialogCancelButton)
        }
    );

    if (answer == 0) {
        return TimelineGenerationService::ConflictResolution::Replace;
    }
    if (answer == 1) {
        return TimelineGenerationService::ConflictResolution::Merge;
    }
    return TimelineGenerationService::ConflictResolution::Ignore;
}
