#pragma once
#include <QString>
#include <QVector>
#include "SpratProfilesConfig.h"

/**
 * @interface ILayoutContext
 * @brief Context interface injected into LayoutOrchestrator.
 *
 * Replaces the std::function<> callback fields that were in LayoutOrchestrator::Config,
 * providing compile-time enforcement and easier mocking for unit tests.
 */
class ILayoutContext {
public:
    virtual ~ILayoutContext() = default;

    virtual bool    activeFramesAreInSourceFolder() const = 0;
    virtual void    copyActiveFramesToSourceFolder(bool overwriteDuplicates) = 0;
    virtual bool    selectedProfileDefinition(SpratProfile& out) const = 0;
    virtual QString layoutParserFolder() const = 0;
    virtual bool    sourceFolderMatchesActiveFrames() const = 0;
    virtual QVector<SpratProfile> configuredProfiles() = 0;
};
