#pragma once
class QWidget;

class IAtlasViewport {
public:
    virtual ~IAtlasViewport() = default;
    virtual QWidget* widget() = 0;
};
