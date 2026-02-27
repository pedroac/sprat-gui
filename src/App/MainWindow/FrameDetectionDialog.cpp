#include "FrameDetectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QPixmap>
#include <QRect>
#include <QVector>
#include <QPoint>
#include <QPen>
#include <QBrush>
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QColor>
#include <Qt>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QGraphicsLineItem>
#include <QDoubleSpinBox>
#include <QLabel>

FrameDetectionDialog::FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& detectedFrames, QWidget* parent)
    : QDialog(parent)
    , m_image(imagePath)
    , m_frames(detectedFrames)
    , m_selected(detectedFrames.size(), false)
    , m_hovered(detectedFrames.size(), false)
{
    setWindowTitle(tr("Frame Detection Review"));
    setModal(true);
    resize(800, 600);
    
    // Create widgets
    m_scene = new QGraphicsScene(this);
    m_imageView = new QGraphicsView(m_scene, this);
    m_imageView->setAlignment(Qt::AlignCenter);
    m_imageView->setMouseTracking(true);
    m_imageView->setRenderHint(QPainter::Antialiasing, false);
    m_imageView->setRenderHint(QPainter::SmoothPixmapTransform, false);
    m_imageView->setDragMode(QGraphicsView::NoDrag);
    m_imageView->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    m_imageView->setBackgroundBrush(QColor(90, 90, 90));
    
    // Set up panning
    setupPanning();
    
    // Top Bar
    QHBoxLayout* topBarLayout = new QHBoxLayout();
    topBarLayout->addStretch();
    topBarLayout->addWidget(new QLabel(tr("Zoom:")));
    m_zoomSpin = new QDoubleSpinBox(this);
    m_zoomSpin->setRange(0.1, 50.0);
    m_zoomSpin->setValue(1.0);
    m_zoomSpin->setSingleStep(0.1);
    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value){
        if (m_imageView && m_imageView->viewport()) {
            const QPoint viewPos = m_imageView->viewport()->mapFromGlobal(QCursor::pos());

            const QRectF sceneRect = m_imageView->sceneRect();
            const QSize viewportSize = m_imageView->viewport()->size();
            const bool fitsInView = (sceneRect.width() * value <= viewportSize.width()) && 
                                    (sceneRect.height() * value <= viewportSize.height());

            if (!fitsInView && m_imageView->viewport()->rect().contains(viewPos)) {
                m_imageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            } else {
                m_imageView->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            }
        }
        QTransform t;
        t.scale(value, value);
        m_imageView->setTransform(t);
    });
    topBarLayout->addWidget(m_zoomSpin);

    m_acceptBtn = new QPushButton(tr("Accept Frames"), this);
    m_rejectBtn = new QPushButton(tr("Use as Single Frame"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    
    connect(m_acceptBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onRejectClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onCancelClicked);
    
    // Layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_rejectBtn);
    buttonLayout->addWidget(m_acceptBtn);
    
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topBarLayout);
    mainLayout->addWidget(m_imageView);
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
    
    // Set up the scene with the image at original size
    if (!m_image.isNull()) {
        m_imageItem = m_scene->addPixmap(m_image);
        m_imageItem->setPos(0, 0);
        
        // Set scene rect to image size
        m_scene->setSceneRect(0, 0, m_image.width(), m_image.height());
        
        // Draw frame rectangles as QGraphicsRectItems
        drawFrameRectangles();
    }

    // Setup split line indicator
    m_splitLineItem = new QGraphicsLineItem();
    QPen splitPen(Qt::red, 2, Qt::DashLine);
    m_splitLineItem->setPen(splitPen);
    m_splitLineItem->setZValue(2.0); // Above frames
    m_splitLineItem->hide();
    m_scene->addItem(m_splitLineItem);
}

void FrameDetectionDialog::setupPanning() {
    // Enable panning by setting up event filters
    m_imageView->viewport()->installEventFilter(this);
}

bool FrameDetectionDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_imageView->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton || (mouseEvent->button() == Qt::LeftButton && m_spacePressed)) {
                m_isPanning = true;
                m_lastMousePos = mouseEvent->pos();
                setCursor(Qt::ClosedHandCursor);
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                return handleMousePress(mouseEvent);
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_isPanning) {
                QPoint delta = mouseEvent->pos() - m_lastMousePos;
                m_lastMousePos = mouseEvent->pos();
                // Use QGraphicsView's built-in panning methods
                m_imageView->horizontalScrollBar()->setValue(m_imageView->horizontalScrollBar()->value() - delta.x());
                m_imageView->verticalScrollBar()->setValue(m_imageView->verticalScrollBar()->value() - delta.y());
                return true;
            }
            return handleMouseMove(mouseEvent);
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton || (mouseEvent->button() == Qt::LeftButton && m_spacePressed)) {
                m_isPanning = false;
                setCursor(Qt::ArrowCursor);
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                return handleMouseRelease(mouseEvent);
            }
        } else if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->modifiers() & Qt::ControlModifier) {
                double current = m_zoomSpin->value();
                const double scaleFactor = 1.15;
                if (wheelEvent->angleDelta().y() > 0) {
                    current *= scaleFactor;
                } else {
                    current /= scaleFactor;
                }
                m_zoomSpin->setValue(current);
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
            showContextMenu(contextEvent->globalPos());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

FrameDetectionDialog::~FrameDetectionDialog() = default;

QVector<QRect> FrameDetectionDialog::getSelectedFrames() const {
    return m_frames;
}

bool FrameDetectionDialog::userAccepted() const {
    return m_userAccepted;
}

void FrameDetectionDialog::drawFrameRectangles() {
    // Clear existing frame items
    for (auto* item : m_frameItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_frameItems.clear();
    
    // Create new frame rectangles as QGraphicsRectItems
    for (int i = 0; i < m_frames.size(); ++i) {
        const QRect& rect = m_frames[i];
        QGraphicsRectItem* frameItem = new QGraphicsRectItem(rect);
        
        // Set pen and brush
        QPen pen;
        pen.setWidth(2);
        pen.setColor(m_selected[i] ? Qt::green : Qt::cyan);
        if (m_hovered[i]) {
            pen.setStyle(Qt::DashLine);
        }
        frameItem->setPen(pen);
        frameItem->setBrush(Qt::NoBrush);
        
        // Set Z value to be above the image
        frameItem->setZValue(1.0);
        
        // Store the index in the item's data for easy lookup
        frameItem->setData(0, i);
        
        m_scene->addItem(frameItem);
        m_frameItems.append(frameItem);
    }
}

bool FrameDetectionDialog::handleMousePress(QMouseEvent* event) {
    QPoint pos = event->pos();
    QPointF scenePos = m_imageView->mapToScene(pos);

    // Check for split action
    if (m_splitLineItem->isVisible() && m_splitFrameIndex != -1) {
        splitFrame(m_splitFrameIndex, m_splitOrientation, m_splitPos);
        m_splitLineItem->hide();
        m_splitFrameIndex = -1;
        return true;
    }

    // Check resize handles of selected frames
    for (int i = 0; i < m_frames.size(); ++i) {
        if (m_selected[i]) {
            ResizeHandle handle = getResizeHandle(pos, m_frames[i]);
            if (handle != NoHandle) {
                m_dragging = true;
                m_isResizing = true;
                m_resizeHandle = handle;
                m_draggedFrameIndex = i;
                m_dragOriginalRect = m_frames[i];
                m_dragStartPos = pos;
                updateResizeCursor(handle);
                return true;
            }
        }
    }

    QGraphicsItem* item = m_scene->itemAt(scenePos, m_imageView->transform());
    
    if (auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item)) {
        int frameIndex = rectItem->data(0).toInt();
        if (frameIndex >= 0 && frameIndex < m_frames.size()) {
            if (event->modifiers() & Qt::ControlModifier) {
                m_selected[frameIndex] = !m_selected[frameIndex];
            } else {
                std::fill(m_selected.begin(), m_selected.end(), false);
                m_selected[frameIndex] = true;
            }
            updateFrameVisuals();

            m_draggedFrameIndex = frameIndex;
            m_dragOriginalRect = m_frames[frameIndex];
            m_dragStartScenePos = scenePos;
            m_dragStartPos = pos;
            m_dragging = true;
            m_isResizing = false;
            m_imageView->viewport()->setCursor(Qt::SizeAllCursor);
            return true;
        }
    }

    // Clicked on background
    createFrame(scenePos);
    return true;
}

bool FrameDetectionDialog::handleMouseMove(QMouseEvent* event) {
    QPoint pos = event->pos();
    QPointF scenePos = m_imageView->mapToScene(pos);

    if (m_dragging && m_draggedFrameIndex >= 0) {
        if (m_isResizing) {
            resizeFrame(m_draggedFrameIndex, m_resizeHandle, scenePos);
        } else {
            QPointF sceneDelta = scenePos - m_dragStartScenePos;
            moveFrame(m_draggedFrameIndex, sceneDelta);
        }
        return true;
    }
    
    // Check for split mode (Shift + Hover over edge, or Split Mode enabled)
    if (m_splitMode || (event->modifiers() & Qt::ShiftModifier)) {
        bool foundSplit = false;
        for (int i = 0; i < m_frames.size(); ++i) {
            ResizeHandle handle = getResizeHandle(pos, m_frames[i]);
            // Only allow splitting on edges, not corners
            if (handle == Left || handle == Right || handle == Top || handle == Bottom) {
                m_splitFrameIndex = i;
                const QRect& rect = m_frames[i];
                QPointF mouseScenePos = m_imageView->mapToScene(pos);
                
                if (handle == Left || handle == Right) {
                    m_splitOrientation = Qt::Horizontal;
                    m_splitPos = qRound(mouseScenePos.y());
                    m_splitLineItem->setLine(rect.left(), m_splitPos, rect.right(), m_splitPos);
                } else {
                    m_splitOrientation = Qt::Vertical;
                    m_splitPos = qRound(mouseScenePos.x());
                    m_splitLineItem->setLine(m_splitPos, rect.top(), m_splitPos, rect.bottom());
                }
                m_splitLineItem->show();
                m_imageView->viewport()->setCursor(Qt::CrossCursor);
                return true;
            }
        }
        m_splitLineItem->hide();
        m_splitFrameIndex = -1;
    } else {
        m_splitLineItem->hide();
        m_splitFrameIndex = -1;
    }

    // Check resize handles
    for (int i = 0; i < m_frames.size(); ++i) {
        if (m_selected[i]) {
            ResizeHandle handle = getResizeHandle(pos, m_frames[i]);
            if (handle != NoHandle) {
                updateResizeCursor(handle);
                return true;
            }
        }
    }

    QGraphicsItem* item = m_scene->itemAt(scenePos, m_imageView->transform());
    
    bool foundHover = false;
    for (int i = 0; i < m_frameItems.size(); ++i) {
        bool wasHovered = m_hovered[i];
        bool isHovered = (item == m_frameItems[i]);
        m_hovered[i] = isHovered;
        
        if (wasHovered != isHovered) {
            foundHover = true;
        }
    }
    
    if (foundHover) {
        updateFrameVisuals();
    }
    updateCursor(scenePos.toPoint());
    return true;
}

bool FrameDetectionDialog::handleMouseRelease(QMouseEvent* event) {
    Q_UNUSED(event);
    if (m_dragging) {
        m_dragging = false;
        m_draggedFrameIndex = -1;
        m_imageView->viewport()->setCursor(Qt::ArrowCursor);
        return true;
    }
    return false;
}

void FrameDetectionDialog::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double current = m_zoomSpin->value();
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            current *= scaleFactor;
        } else {
            current /= scaleFactor;
        }
        m_zoomSpin->setValue(current);
        event->accept();
    } else {
        // Pass the event to the base class
        QDialog::wheelEvent(event);
    }
}

void FrameDetectionDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedFrames();
    }
    QDialog::keyPressEvent(event);
}

void FrameDetectionDialog::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        setCursor(Qt::ArrowCursor);
    }
    QDialog::keyReleaseEvent(event);
}

void FrameDetectionDialog::updateFrameVisuals() {
    for (int i = 0; i < m_frameItems.size(); ++i) {
        QGraphicsRectItem* frameItem = m_frameItems[i];
        QPen pen;
        pen.setWidth(2);
        pen.setColor(m_selected[i] ? Qt::green : Qt::cyan);
        if (m_hovered[i]) {
            pen.setStyle(Qt::DashLine);
        } else {
            pen.setStyle(Qt::SolidLine);
        }
        frameItem->setPen(pen);
    }
}

void FrameDetectionDialog::onAcceptClicked() {
    m_userAccepted = true;
    accept();
}

void FrameDetectionDialog::onRejectClicked() {
    m_userAccepted = false;
    accept();
}

void FrameDetectionDialog::onCancelClicked() {
    reject();
}

void FrameDetectionDialog::drawFrame(QPainter& painter, const QRect& rect, bool isSelected, bool isHovered) {
    QPen pen;
    pen.setWidth(2);
    
    if (isSelected) {
        pen.setColor(Qt::green);
    } else {
        pen.setColor(Qt::cyan);
    }
    
    if (isHovered) {
        pen.setStyle(Qt::DashLine);
    }
    
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
    
    // Draw selection indicator
    if (isSelected) {
        painter.setPen(Qt::white);
        painter.setBrush(Qt::green);
        painter.drawRect(rect.x(), rect.y(), 10, 10);
    }
}

int FrameDetectionDialog::findFrameAt(const QPoint& pos) {
    for (int i = 0; i < m_frames.size(); ++i) {
        if (m_frames[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void FrameDetectionDialog::updateCursor(const QPoint& pos) {
    int frameIndex = findFrameAt(pos);
    if (frameIndex >= 0) {
        m_imageView->viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        m_imageView->viewport()->setCursor(Qt::ArrowCursor);
    }
}

FrameDetectionDialog::ResizeHandle FrameDetectionDialog::getResizeHandle(const QPoint& pos, const QRect& rect) const {
    const int handleSize = 8;
    const int margin = 4;
    
    // Convert screen position to scene position
    QPointF scenePos = m_imageView->mapToScene(pos);
    
    // Adjust handle size based on zoom level
    double scale = m_imageView->transform().m11();
    double scaledHandleSize = handleSize / scale;
    double scaledMargin = margin / scale;
    
    // Check corners
    if (qAbs(scenePos.x() - rect.left()) <= scaledHandleSize && qAbs(scenePos.y() - rect.top()) <= scaledHandleSize) {
        return TopLeft;
    }
    if (qAbs(scenePos.x() - rect.right()) <= scaledHandleSize && qAbs(scenePos.y() - rect.top()) <= scaledHandleSize) {
        return TopRight;
    }
    if (qAbs(scenePos.x() - rect.left()) <= scaledHandleSize && qAbs(scenePos.y() - rect.bottom()) <= scaledHandleSize) {
        return BottomLeft;
    }
    if (qAbs(scenePos.x() - rect.right()) <= scaledHandleSize && qAbs(scenePos.y() - rect.bottom()) <= scaledHandleSize) {
        return BottomRight;
    }
    
    // Check edges
    if (qAbs(scenePos.x() - rect.left()) <= scaledMargin && scenePos.y() >= rect.top() && scenePos.y() <= rect.bottom()) {
        return Left;
    }
    if (qAbs(scenePos.x() - rect.right()) <= scaledMargin && scenePos.y() >= rect.top() && scenePos.y() <= rect.bottom()) {
        return Right;
    }
    if (qAbs(scenePos.y() - rect.top()) <= scaledMargin && scenePos.x() >= rect.left() && scenePos.x() <= rect.right()) {
        return Top;
    }
    if (qAbs(scenePos.y() - rect.bottom()) <= scaledMargin && scenePos.x() >= rect.left() && scenePos.x() <= rect.right()) {
        return Bottom;
    }
    
    return NoHandle;
}

void FrameDetectionDialog::updateResizeCursor(ResizeHandle handle) {
    switch (handle) {
        case TopLeft:
        case BottomRight:
            m_imageView->viewport()->setCursor(Qt::SizeFDiagCursor);
            break;
        case TopRight:
        case BottomLeft:
            m_imageView->viewport()->setCursor(Qt::SizeBDiagCursor);
            break;
        case Top:
        case Bottom:
            m_imageView->viewport()->setCursor(Qt::SizeVerCursor);
            break;
        case Left:
        case Right:
            m_imageView->viewport()->setCursor(Qt::SizeHorCursor);
            break;
        default:
            m_imageView->viewport()->setCursor(Qt::ArrowCursor);
            break;
    }
}

void FrameDetectionDialog::resizeFrame(int frameIndex, ResizeHandle handle, const QPointF& scenePos) {
    if (frameIndex < 0 || frameIndex >= m_frames.size()) {
        return;
    }
    
    QRect& rect = m_frames[frameIndex];
    rect = m_dragOriginalRect;
    
    switch (handle) {
        case TopLeft:
            rect.setTopLeft(scenePos.toPoint());
            break;
        case Top:
            rect.setTop(scenePos.y());
            break;
        case TopRight:
            rect.setTopRight(scenePos.toPoint());
            break;
        case Right:
            rect.setRight(scenePos.x());
            break;
        case BottomRight:
            rect.setBottomRight(scenePos.toPoint());
            break;
        case Bottom:
            rect.setBottom(scenePos.y());
            break;
        case BottomLeft:
            rect.setBottomLeft(scenePos.toPoint());
            break;
        case Left:
            rect.setLeft(scenePos.x());
            break;
        default:
            break;
    }
    
    rect = rect.normalized();
    
    // Update the graphics item
    if (frameIndex < m_frameItems.size()) {
        m_frameItems[frameIndex]->setRect(rect);
    }
}

void FrameDetectionDialog::moveFrame(int frameIndex, const QPointF& sceneDelta) {
    if (frameIndex < 0 || frameIndex >= m_frames.size()) {
        return;
    }
    
    QRect& rect = m_frames[frameIndex];
    rect = m_dragOriginalRect.translated(sceneDelta.toPoint());
    
    // Ensure rectangle stays within image bounds
    if (rect.left() < 0) rect.moveLeft(0);
    if (rect.top() < 0) rect.moveTop(0);
    if (rect.right() > m_image.width()) rect.moveRight(m_image.width());
    if (rect.bottom() > m_image.height()) rect.moveBottom(m_image.height());
    
    // Update the graphics item
    if (frameIndex < m_frameItems.size()) {
        m_frameItems[frameIndex]->setRect(rect);
    }
}

void FrameDetectionDialog::splitFrame(int frameIndex, Qt::Orientation orientation, int pos) {
    if (frameIndex < 0 || frameIndex >= m_frames.size()) {
        return;
    }

    QRect original = m_frames[frameIndex];
    QRect first, second;

    if (orientation == Qt::Horizontal) {
        // Split horizontally (y-axis cut)
        // Top part
        first = original;
        first.setBottom(pos - 1);
        // Bottom part
        second = original;
        second.setTop(pos);
    } else {
        // Split vertically (x-axis cut)
        // Left part
        first = original;
        first.setRight(pos - 1);
        // Right part
        second = original;
        second.setLeft(pos);
    }

    // Validate new rects
    if (!first.isValid() || !second.isValid() || first.isEmpty() || second.isEmpty()) {
        return;
    }

    // Remove old frame
    m_frames.removeAt(frameIndex);
    m_selected.removeAt(frameIndex);
    m_hovered.removeAt(frameIndex);
    if (frameIndex < m_frameItems.size()) {
        m_scene->removeItem(m_frameItems[frameIndex]);
        delete m_frameItems[frameIndex];
        m_frameItems.removeAt(frameIndex);
    }

    // Add new frames
    // We can use createFrame logic but we need to insert them properly.
    // Simpler to just append them and redraw or insert.
    // Let's append them.
    m_frames.append(first);
    m_selected.append(true);
    m_hovered.append(false);
    
    m_frames.append(second);
    m_selected.append(true);
    m_hovered.append(false);

    // Rebuild visuals to keep it simple and correct
    drawFrameRectangles();
}

void FrameDetectionDialog::createFrame(const QPointF& scenePos) {
    std::fill(m_selected.begin(), m_selected.end(), false);
    
    QRect newRect(scenePos.toPoint(), QSize(0, 0));
    m_frames.append(newRect);
    m_selected.append(true);
    m_hovered.append(false);
    
    // Add visual item
    QGraphicsRectItem* frameItem = new QGraphicsRectItem(newRect);
    QPen pen;
    pen.setWidth(2);
    pen.setColor(Qt::green);
    frameItem->setPen(pen);
    frameItem->setBrush(Qt::NoBrush);
    frameItem->setZValue(1.0);
    frameItem->setData(0, m_frames.size() - 1);
    m_scene->addItem(frameItem);
    m_frameItems.append(frameItem);
    
    // Start dragging bottom-right corner
    m_draggedFrameIndex = m_frames.size() - 1;
    m_dragOriginalRect = newRect;
    m_dragStartPos = m_imageView->mapFromScene(scenePos);
    m_dragging = true;
    m_isResizing = true;
    m_resizeHandle = BottomRight;
    m_imageView->viewport()->setCursor(Qt::SizeFDiagCursor);
}

void FrameDetectionDialog::createDefaultFrame(const QPointF& center) {
    std::fill(m_selected.begin(), m_selected.end(), false);
    
    QRect newRect(0, 0, 32, 32);
    newRect.moveCenter(center.toPoint());
    newRect = newRect.intersected(QRect(0, 0, m_image.width(), m_image.height()));
    
    if (newRect.isEmpty()) return;

    m_frames.append(newRect);
    m_selected.append(true);
    m_hovered.append(false);
    
    QGraphicsRectItem* frameItem = new QGraphicsRectItem(newRect);
    QPen pen;
    pen.setWidth(2);
    pen.setColor(Qt::green);
    frameItem->setPen(pen);
    frameItem->setBrush(Qt::NoBrush);
    frameItem->setZValue(1.0);
    frameItem->setData(0, m_frames.size() - 1);
    m_scene->addItem(frameItem);
    m_frameItems.append(frameItem);
    
    updateFrameVisuals();
}

void FrameDetectionDialog::deleteSelectedFrames() {
    m_dragging = false;
    m_draggedFrameIndex = -1;

    // Remove selected frames from the end to beginning to avoid index issues
    for (int i = m_frames.size() - 1; i >= 0; --i) {
        if (m_selected[i]) {
            m_frames.removeAt(i);
            m_selected.removeAt(i);
            m_hovered.removeAt(i);
            
            // Remove the corresponding graphics item
            if (i < m_frameItems.size()) {
                m_scene->removeItem(m_frameItems[i]);
                delete m_frameItems[i];
                m_frameItems.removeAt(i);
            }
        }
    }
    
    // Re-index remaining items
    for (int i = 0; i < m_frameItems.size(); ++i) {
        m_frameItems[i]->setData(0, i);
    }

    // Update visuals
    updateFrameVisuals();
}

void FrameDetectionDialog::showContextMenu(const QPoint& globalPos) {
    QPoint viewPos = m_imageView->viewport()->mapFromGlobal(globalPos);
    QPointF scenePos = m_imageView->mapToScene(viewPos);

    QGraphicsItem* item = m_scene->itemAt(scenePos, m_imageView->transform());
    int frameIndex = -1;
    if (auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item)) {
        frameIndex = rectItem->data(0).toInt();
    }

    if (frameIndex >= 0 && frameIndex < m_frames.size()) {
        if (!m_selected[frameIndex]) {
            std::fill(m_selected.begin(), m_selected.end(), false);
            m_selected[frameIndex] = true;
            updateFrameVisuals();
        }
    }

    bool hasSelection = false;
    for (bool s : m_selected) {
        if (s) {
            hasSelection = true;
            break;
        }
    }

    QMenu menu(this);
    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(hasSelection);
    connect(deleteAction, &QAction::triggered, this, &FrameDetectionDialog::deleteSelectedFrames);

    menu.addSeparator();
    QAction* splitAction = menu.addAction(tr("Split Mode"));
    splitAction->setCheckable(true);
    splitAction->setChecked(m_splitMode);
    connect(splitAction, &QAction::triggered, this, [this](bool checked){
        m_splitMode = checked;
        if (!m_splitMode) {
             m_splitLineItem->hide();
             m_splitFrameIndex = -1;
             m_imageView->viewport()->setCursor(Qt::ArrowCursor);
        }
    });

    QAction* createAction = menu.addAction(tr("Create New Frame"));
    connect(createAction, &QAction::triggered, this, [this, scenePos](){
        createDefaultFrame(scenePos);
    });

    menu.exec(globalPos);
}
