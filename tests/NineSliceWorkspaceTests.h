#pragma once

#include <QObject>

class NineSliceWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void testInsetsThatDontFitAreFittedOnLoad();
    void testInsetsThatFitAreKeptOnLoad();
    void testOnlyNineSlicedSpritesAreListed();
    void testEmptyStateWithoutNineSlicedSprites();
    void testAddingSpritesGivesEachOneThirds();
    void testRemovingKeepsInsetsAndShowsAnotherSprite();
    void testRemovingTheLastSpriteShowsTheEmptyState();
    void testPickerListsOnlyFilteredSprites();
};
