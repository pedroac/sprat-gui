#pragma once

#include "TimelineGenerationService.h"

class QWidget;
class QString;

class TimelineUi {
public:
    static TimelineGenerationService::ConflictResolution askTimelineConflictResolution(QWidget* parent, const QString& timelineName);
};
