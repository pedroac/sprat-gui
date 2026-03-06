#include "TimelineUi.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace {
QString trTimelineUi(const char* text) {
    return QCoreApplication::translate("TimelineUi", text);
}
}

TimelineGenerationService::ConflictResolution TimelineUi::askTimelineConflictResolution(QWidget* parent, const QString& timelineName) {
    QMessageBox msg(parent);
    msg.setWindowTitle(trTimelineUi("Timeline Already Exists"));
    msg.setText(QString(trTimelineUi("A timeline named \"%1\" already exists. What should be done with the generated frames?")).arg(timelineName));
    QPushButton* replaceBtn = msg.addButton(trTimelineUi("Replace"), QMessageBox::DestructiveRole);
    QPushButton* mergeBtn = msg.addButton(trTimelineUi("Merge"), QMessageBox::AcceptRole);
    msg.addButton(trTimelineUi("Ignore"), QMessageBox::RejectRole);
    msg.exec();

    if (msg.clickedButton() == replaceBtn) {
        return TimelineGenerationService::ConflictResolution::Replace;
    }
    if (msg.clickedButton() == mergeBtn) {
        return TimelineGenerationService::ConflictResolution::Merge;
    }
    return TimelineGenerationService::ConflictResolution::Ignore;
}
