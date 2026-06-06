#pragma once
#include <QLabel>
#include <QResizeEvent>

/**
 * A QLabel that elides its text with "…" when the available width is smaller
 * than the full text. Set text via setFullText() instead of setText().
 * The horizontal size policy is Ignored so the label can shrink freely.
 */
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    void setFullText(const QString& text) {
        m_fullText = text;
        updateElided();
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        updateElided();
    }

private:
    void updateElided() {
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, qMax(0, width())));
    }

    QString m_fullText;
};
