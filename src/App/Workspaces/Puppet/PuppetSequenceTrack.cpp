#include "PuppetSequenceTrack.h"
#include "AppIcons.h"

#include <algorithm>
#include <cstdlib>
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int MARGIN = 6;
constexpr int RULER_HEIGHT = 14;
constexpr int BLOCK_HEIGHT = 64;
/// Narrowest a block is drawn, so a one-frame step can still be clicked and read.
constexpr int MIN_BLOCK_WIDTH = 28;
/// Widest a block is drawn: a step can hold tens of thousands of frames, and the widget
/// must not demand a multi-megapixel width for them.
constexpr int MAX_BLOCK_WIDTH = 4000;
constexpr int THUMBNAIL_SIZE = 36;
/// The row under the blocks holding each step's round drag handle.
constexpr int HANDLE_ROW_HEIGHT = 16;
constexpr int HANDLE_RADIUS = 5;
/// How far from a handle's middle a press still grabs it.
constexpr int HANDLE_GRAB_RADIUS = 8;
/// Opacity of a dragged step left in place and of the copy following the pointer.
constexpr qreal DRAGGED_SOURCE_OPACITY = 0.35;
constexpr qreal DRAGGED_GHOST_OPACITY = 0.8;
/// A ruler number every this many frames.
constexpr int RULER_STEP = 4;
}  // namespace

PuppetSequenceTrack::PuppetSequenceTrack(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // to show the grab cursor over the handles
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(sizeHint().height());
}

QSize PuppetSequenceTrack::sizeHint() const {
    const int width = m_blocks.isEmpty() ? 200 : blockLeft(m_blocks.size()) + MARGIN;
    return QSize(width, MARGIN + RULER_HEIGHT + BLOCK_HEIGHT + HANDLE_ROW_HEIGHT + MARGIN);
}

void PuppetSequenceTrack::showSteps(const QVector<Block>& blocks, int selected, const QStringList& poseNames) {
    // Selecting the dragged step makes the workspace show the steps again: the drag goes on.
    if (m_dragStep >= blocks.size()) cancelStepDrag();
    m_blocks = blocks;
    m_poseNames = poseNames;
    m_selected = selected >= 0 && selected < blocks.size() ? selected : -1;
    m_playhead = std::min(m_playhead, totalFrames() - 1);
    // As wide as the steps at least: the scroll area around it then scrolls to the last ones
    // instead of squeezing it into view.
    setMinimumWidth(sizeHint().width());
    updateGeometry();
    resize(std::max(sizeHint().width(), parentWidget() ? parentWidget()->width() : 0), sizeHint().height());
    update();
}

void PuppetSequenceTrack::setPlayhead(int frame) {
    if (frame == m_playhead) return;
    m_playhead = frame;
    update();
}

int PuppetSequenceTrack::totalFrames() const {
    int frames = 0;
    for (const Block& block : m_blocks) frames += std::max(1, block.in + block.hold);
    return frames;
}

int PuppetSequenceTrack::blockWidth(int step) const {
    const Block& block = m_blocks[step];
    return std::max(MIN_BLOCK_WIDTH,
                    std::min(MAX_BLOCK_WIDTH, std::max(1, block.in + block.hold) * FRAME_WIDTH));
}

int PuppetSequenceTrack::blockLeft(int step) const {
    int x = MARGIN;
    for (int i = 0; i < step && i < m_blocks.size(); ++i) x += blockWidth(i);
    return x;
}

int PuppetSequenceTrack::stepAtX(int x) const {
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (x >= blockLeft(i) && x < blockLeft(i) + blockWidth(i)) return i;
    }
    return -1;
}

QPoint PuppetSequenceTrack::handleCenter(int step) const {
    return QPoint(blockLeft(step) + (blockWidth(step) - 2) / 2,
                  MARGIN + RULER_HEIGHT + BLOCK_HEIGHT + HANDLE_ROW_HEIGHT / 2);
}

int PuppetSequenceTrack::handleAt(const QPoint& pos) const {
    for (int i = 0; i < m_blocks.size(); ++i) {
        const QPoint offset = pos - handleCenter(i);
        if (offset.x() * offset.x() + offset.y() * offset.y() <= HANDLE_GRAB_RADIUS * HANDLE_GRAB_RADIUS) return i;
    }
    return -1;
}

int PuppetSequenceTrack::dropIndexAt(int x) const {
    // The steps other than the dragged one whose middle is left of the pointer go before it.
    int index = 0;
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (i != m_dragStep && blockLeft(i) + blockWidth(i) / 2 < x) ++index;
    }
    return index;
}

int PuppetSequenceTrack::dropLineX(int to) const {
    QVector<int> others;
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (i != m_dragStep) others.append(i);
    }
    if (others.isEmpty()) return blockLeft(m_dragStep);
    if (to <= 0) return blockLeft(others.first()) - 1;
    const int before = others[std::min(to, int(others.size())) - 1];
    return blockLeft(before) + blockWidth(before) - 1;
}

void PuppetSequenceTrack::cancelStepDrag() {
    if (m_dragStep < 0) return;
    m_dragStep = -1;
    m_dragMoved = false;
    unsetCursor();
    update();
}

int PuppetSequenceTrack::frameAtX(int x) const {
    // Within a block, frames share its width evenly (a short step's block is widened).
    int frame = 0;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const int frames = std::max(1, m_blocks[i].in + m_blocks[i].hold);
        const int left = blockLeft(i);
        if (x < left + blockWidth(i)) {
            const int within = std::clamp((x - left) * frames / blockWidth(i), 0, frames - 1);
            return frame + within;
        }
        frame += frames;
    }
    return std::max(0, frame - 1);
}

void PuppetSequenceTrack::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPalette& colors = palette();
    const int top = MARGIN + RULER_HEIGHT;

    if (m_blocks.isEmpty()) {
        painter.setPen(colors.color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(MARGIN, top, -MARGIN, -MARGIN), Qt::AlignCenter | Qt::TextWordWrap,
                         tr("No poses yet: right-click here to add one."));
    }

    // The ruler: a tick per frame, numbered every few.
    painter.setPen(colors.color(QPalette::Mid));
    int frame = 0;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const int frames = std::max(1, m_blocks[i].in + m_blocks[i].hold);
        const int width = blockWidth(i);
        // Bound the tick count by the pixels drawn: a step can hold tens of thousands
        // of frames, and one drawLine per frame would freeze the workspace.
        const int tickStep = std::max(1, frames / std::max(1, width / 2));
        for (int f = 0; f < frames; f += tickStep) {
            const int x = blockLeft(i) + f * width / frames;
            const bool numbered = (frame + f) % RULER_STEP == 0;
            painter.drawLine(x, top - (numbered ? 6 : 3), x, top);
            if (numbered) painter.drawText(QPoint(x + 2, top - 5), QString::number(frame + f + 1));
        }
        frame += frames;
    }

    const bool dragging = m_dragStep >= 0 && m_dragMoved;
    const auto drawBlock = [&](int i, int left) {
        const Block& block = m_blocks[i];
        const QRect box(left, top, blockWidth(i) - 2, BLOCK_HEIGHT);
        const bool selected = i == m_selected;
        painter.setPen(QPen(selected ? colors.color(QPalette::Highlight) : colors.color(QPalette::Mid), selected ? 2 : 1,
                            block.shared ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(colors.color(QPalette::Base));
        painter.drawRoundedRect(box, 3, 3);
        // The ramp: the pose being reached over `in` frames (a step for ease:step), then held.
        const int frames = std::max(1, block.in + block.hold);
        const int rampWidth = box.width() * std::max(0, block.in) / frames;
        if (rampWidth > 0) {
            // Linear rises over `in`; Step stays low, then jumps up as the pose arrives.
            QPainterPath ramp;
            ramp.moveTo(box.left(), box.bottom());
            if (block.snap) {
                ramp.lineTo(box.left(), box.bottom() - 3);
                ramp.lineTo(box.left() + rampWidth - 2, box.bottom() - 3);
                ramp.lineTo(box.left() + rampWidth - 2, box.bottom() - 10);
                ramp.lineTo(box.left() + rampWidth, box.bottom() - 10);
            } else {
                ramp.lineTo(box.left() + rampWidth, box.bottom() - 10);
            }
            ramp.lineTo(box.left() + rampWidth, box.bottom());
            ramp.closeSubpath();
            QColor fill = colors.color(QPalette::Highlight);
            fill.setAlpha(70);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(ramp);
        }
        if (!block.thumbnail.isNull()) {
            const QPixmap thumbnail = block.thumbnail.scaled(std::min(THUMBNAIL_SIZE, box.width() - 4), THUMBNAIL_SIZE,
                                                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawPixmap(box.left() + (box.width() - thumbnail.width()) / 2, box.top() + 3, thumbnail);
        }
        painter.setPen(block.missing ? QColor(0xd9, 0x53, 0x4f) : colors.color(QPalette::Text));
        const QString name = block.missing ? QStringLiteral("⚠ %1").arg(block.pose) : block.pose;
        painter.drawText(box.adjusted(2, 0, -2, -3), Qt::AlignHCenter | Qt::AlignBottom,
                         painter.fontMetrics().elidedText(name, Qt::ElideRight, box.width() - 4));
    };
    for (int i = 0; i < m_blocks.size(); ++i) {
        painter.setOpacity(dragging && i == m_dragStep ? DRAGGED_SOURCE_OPACITY : 1.0);
        drawBlock(i, blockLeft(i));
        // The round handle under the step, to drag it elsewhere.
        const bool active = i == m_dragStep || i == m_hoveredHandle;
        painter.setPen(QPen(colors.color(active ? QPalette::Highlight : QPalette::Mid), 1.5));
        painter.setBrush(active ? colors.color(QPalette::Highlight) : colors.color(QPalette::Button));
        painter.drawEllipse(handleCenter(i), HANDLE_RADIUS, HANDLE_RADIUS);
    }
    painter.setOpacity(1.0);

    if (m_playhead >= 0 && !m_blocks.isEmpty()) {
        // The middle of the playhead's frame, in its block.
        int x = MARGIN;
        int remaining = m_playhead;
        for (int i = 0; i < m_blocks.size(); ++i) {
            const int frames = std::max(1, m_blocks[i].in + m_blocks[i].hold);
            if (remaining < frames || i == m_blocks.size() - 1) {
                x = blockLeft(i) + (std::min(remaining, frames - 1) * 2 + 1) * blockWidth(i) / (2 * frames);
                break;
            }
            remaining -= frames;
        }
        painter.setPen(QPen(QColor(0xd9, 0x53, 0x4f), 2));
        painter.drawLine(x, MARGIN, x, top + BLOCK_HEIGHT);
    }

    if (dragging) {
        // Where the step will land, then the step itself following the pointer.
        const int lineX = dropLineX(dropIndexAt(m_dragX));
        painter.setPen(QPen(colors.color(QPalette::Highlight), 3));
        painter.drawLine(lineX, top - 2, lineX, top + BLOCK_HEIGHT + 2);
        painter.setOpacity(DRAGGED_GHOST_OPACITY);
        drawBlock(m_dragStep, m_dragX - m_dragGrabX);
        painter.setOpacity(1.0);
    }
}

void PuppetSequenceTrack::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() != Qt::LeftButton || m_blocks.isEmpty()) return;
    const int x = event->position().toPoint().x();
    if (const int handle = handleAt(event->position().toPoint()); handle >= 0) {
        // The handle selects its step and, once dragged, moves it.
        m_dragStep = handle;
        m_dragX = x;
        m_dragGrabX = x - blockLeft(handle);
        m_dragMoved = false;
        setCursor(Qt::ClosedHandCursor);
        if (handle != m_selected) {
            m_selected = handle;
            emit stepSelected(handle);
        }
        update();
        return;
    }
    const int step = stepAtX(x);
    m_scrubbing = true;
    if (step >= 0) {
        // The workspace puts the playhead where the step's pose arrives; dragging scrubs.
        m_selected = step;
        emit stepSelected(step);
    } else {
        m_playhead = frameAtX(x);
        emit playheadMoved(m_playhead);
    }
    update();
}

void PuppetSequenceTrack::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (m_dragStep >= 0) {
        if (!(event->buttons() & Qt::LeftButton)) {
            cancelStepDrag();
            return;
        }
        if (!m_dragMoved && std::abs(pos.x() - m_dragX) < QApplication::startDragDistance()) return;
        m_dragMoved = true;
        m_dragX = pos.x();
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        const int hovered = handleAt(pos);
        if (hovered != m_hoveredHandle) {
            m_hoveredHandle = hovered;
            if (hovered >= 0) setCursor(Qt::OpenHandCursor);
            else unsetCursor();
            update();
        }
    }
    if (!m_scrubbing || !(event->buttons() & Qt::LeftButton)) {
        m_scrubbing = false;
        return;
    }
    const int frame = frameAtX(event->position().toPoint().x());
    if (frame == m_playhead) return;
    m_playhead = frame;
    emit playheadMoved(frame);
    update();
}

void PuppetSequenceTrack::mouseReleaseEvent(QMouseEvent* event) {
    m_scrubbing = false;
    if (event->button() != Qt::LeftButton || m_dragStep < 0) return;
    const int from = m_dragStep;
    const int to = dropIndexAt(event->position().toPoint().x());
    const bool moved = m_dragMoved;
    cancelStepDrag();
    m_hoveredHandle = handleAt(event->position().toPoint());
    if (m_hoveredHandle >= 0) setCursor(Qt::OpenHandCursor);
    if (moved && to != from) emit moveRequested(from, to);
}

void PuppetSequenceTrack::leaveEvent(QEvent* event) {
    if (m_hoveredHandle >= 0 && m_dragStep < 0) {
        m_hoveredHandle = -1;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}

void PuppetSequenceTrack::mouseDoubleClickEvent(QMouseEvent* event) {
    const int step = stepAtX(event->position().toPoint().x());
    if (step >= 0) emit stepActivated(step);
}

void PuppetSequenceTrack::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && m_dragStep >= 0) {
        cancelStepDrag();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selected >= 0) {
        emit removeRequested(m_selected);
        event->accept();
        return;
    }
    const int delta = event->key() == Qt::Key_Left ? -1 : event->key() == Qt::Key_Right ? 1 : 0;
    if (delta != 0 && !m_blocks.isEmpty()) {
        m_selected = std::clamp(m_selected + delta, 0, int(m_blocks.size()) - 1);
        emit stepSelected(m_selected);
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

QMenu* PuppetSequenceTrack::posesMenu(const QString& title, int stepIndex, QWidget* parent) {
    auto* menu = new QMenu(title, parent);
    menu->setIcon(AppIcons::icon(QStringLiteral("add")));
    for (const QString& pose : m_poseNames)
        menu->addAction(pose, this, [this, stepIndex, pose]() { emit addRequested(stepIndex, pose); });
    menu->setEnabled(!m_poseNames.isEmpty());
    return menu;
}

QMenu* PuppetSequenceTrack::createContextMenu(int step) {
    auto* menu = new QMenu(this);
    const QIcon add = AppIcons::icon(QStringLiteral("add"));
    if (step < 0) {
        const int last = int(m_blocks.size()) - 1;
        menu->addAction(add, tr("New Frame"), this, [this, last]() { emit newFrameRequested(last + 1, last); });
        menu->addMenu(posesMenu(tr("Add Pose"), m_blocks.size(), menu));
        return menu;
    }
    menu->addAction(add, tr("New Frame Before"), this, [this, step]() { emit newFrameRequested(step, step); });
    menu->addAction(add, tr("New Frame After"), this, [this, step]() { emit newFrameRequested(step + 1, step); });
    menu->addMenu(posesMenu(tr("Add Pose Before"), step, menu));
    menu->addMenu(posesMenu(tr("Add Pose After"), step + 1, menu));
    menu->addSeparator();
    QAction* save = menu->addAction(tr("Save as Pose..."), this, [this, step]() { emit savePoseRequested(step); });
    save->setEnabled(!m_blocks[step].missing);
    menu->addSeparator();
    QAction* left = menu->addAction(tr("Move to the left"), this, [this, step]() { emit moveRequested(step, step - 1); });
    left->setEnabled(step > 0);
    QAction* right = menu->addAction(tr("Move to the right"), this, [this, step]() { emit moveRequested(step, step + 1); });
    right->setEnabled(step < m_blocks.size() - 1);
    menu->addAction(tr("Duplicate"), this, [this, step]() { emit duplicateRequested(step); });
    menu->addSeparator();
    menu->addAction(tr("Delete"), this, [this, step]() { emit removeRequested(step); });
    return menu;
}

void PuppetSequenceTrack::contextMenuEvent(QContextMenuEvent* event) {
    const int step = stepAtX(event->pos().x());
    if (step >= 0 && step != m_selected) {
        m_selected = step;
        emit stepSelected(step);
        update();
    }
    QMenu* menu = createContextMenu(step);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
    event->accept();
}
