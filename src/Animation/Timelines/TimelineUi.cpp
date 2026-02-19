#include "TimelineUi.h"

#include <QMessageBox>
#include <QPushButton>

TimelineGenerationService::ConflictResolution TimelineUi::askTimelineConflictResolution(QWidget* parent, const QString& timelineName) {
    QMessageBox msg(parent);
    msg.setWindowTitle("Timeline Already Exists");
    msg.setText(QString("A timeline named \"%1\" already exists. What should be done with the generated frames?").arg(timelineName));
    QPushButton* replaceBtn = msg.addButton("Replace", QMessageBox::DestructiveRole);
    QPushButton* mergeBtn = msg.addButton("Merge", QMessageBox::AcceptRole);
    msg.addButton("Ignore", QMessageBox::RejectRole);
    msg.exec();

    if (msg.clickedButton() == replaceBtn) {
        return TimelineGenerationService::ConflictResolution::Replace;
    }
    if (msg.clickedButton() == mergeBtn) {
        return TimelineGenerationService::ConflictResolution::Merge;
    }
    return TimelineGenerationService::ConflictResolution::Ignore;
}
