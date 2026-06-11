#include "TimelineEditorPanel.h"
#include "TimelineTreeWidget.h"
#include "TimelineListWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TimelineEditorPanel::TimelineEditorPanel(QWidget* parent)
    : QWidget(parent)
{
    const int groupMargin      = 4;
    const int groupTopPadding  = 12;
    const int groupBottomMargin = 0;

    // ── Top section: timeline list ────────────────────────────────────────────
    m_listArea = new QWidget(this);
    m_listArea->setStyleSheet("font-weight: normal;");
    auto* listLayout = new QVBoxLayout(m_listArea);
    listLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    auto* addLayout = new QHBoxLayout();
    addLayout->addWidget(new QLabel(tr("Name:"), m_listArea));
    m_timelineCreateEdit = new QLineEdit(m_listArea);
    m_timelineCreateEdit->setPlaceholderText(tr("Timeline name (optional)"));
    addLayout->addWidget(m_timelineCreateEdit);

    m_addBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder), "", m_listArea);
    m_addBtn->setToolTip(tr("Add timeline"));
    // Connections (addClicked, returnPressed) are wired by MainWindow after construction
    addLayout->addWidget(m_addBtn);
    listLayout->addLayout(addLayout);

    m_timelineList = new TimelineTreeWidget(m_listArea);
    m_timelineList->setHeaderHidden(true);
    m_timelineList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_timelineList->setIconSize(QSize(32, 32));
    m_timelineList->setDragEnabled(true);
    m_timelineList->setVisible(false);
    m_timelineList->setContextMenuPolicy(Qt::CustomContextMenu);
    listLayout->addWidget(m_timelineList, 1);

    // ── Bottom section: selected-timeline editor ─────────────────────────────
    m_timelineEditorContainer = new QWidget(this);
    m_timelineEditorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* editorContainerLayout = new QVBoxLayout(m_timelineEditorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    m_timelineEditorContainer->setVisible(false);

    m_selectedTimelineGroup = new QWidget(m_timelineEditorContainer);
    auto* groupLayout = new QVBoxLayout(m_selectedTimelineGroup);
    editorContainerLayout->addWidget(m_selectedTimelineGroup);

    // Name / FPS / Remove row
    auto* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Name:"), m_selectedTimelineGroup));
    m_timelineNameEdit = new QLineEdit(m_selectedTimelineGroup);
    m_timelineNameEdit->setEnabled(false);
    nameLayout->addWidget(m_timelineNameEdit);
    nameLayout->addWidget(new QLabel(tr("FPS:"), m_selectedTimelineGroup));
    m_timelineFpsSpin = new QSpinBox(m_selectedTimelineGroup);
    m_timelineFpsSpin->setRange(1, 60);
    m_timelineFpsSpin->setValue(8);
    m_timelineFpsSpin->setEnabled(false);
    m_timelineFpsSpin->setToolTip(tr("Frames per second for animation playback"));
    m_timelineFpsSpin->setAccessibleName(tr("Timeline FPS"));
    nameLayout->addWidget(m_timelineFpsSpin);
    m_removeBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton), "",
        m_selectedTimelineGroup);
    m_removeBtn->setToolTip(tr("Remove timeline"));
    // removeClicked is connected by MainWindow after construction
    nameLayout->addWidget(m_removeBtn);
    groupLayout->addLayout(nameLayout);

    // Alias label
    m_timelineAliasLabel = new QLabel(m_selectedTimelineGroup);
    m_timelineAliasLabel->setVisible(false);
    m_timelineAliasLabel->setToolTip(
        tr("An alias timeline references another timeline's frames but can have its own flip and transform settings."));
    groupLayout->addWidget(m_timelineAliasLabel);

    // Flip row
    auto* flipRow = new QHBoxLayout();
    m_timelineFlipLabel = new QLabel(tr("Flip:"), m_selectedTimelineGroup);
    m_timelineFlipLabel->setVisible(false);
    flipRow->addWidget(m_timelineFlipLabel);
    m_timelineFlipCombo = new QComboBox(m_selectedTimelineGroup);
    m_timelineFlipCombo->addItem(tr("None"),       0);
    m_timelineFlipCombo->addItem(tr("Horizontal"), 1);
    m_timelineFlipCombo->addItem(tr("Vertical"),   2);
    m_timelineFlipCombo->addItem(tr("Both"),       3);
    m_timelineFlipCombo->setVisible(false);
    flipRow->addWidget(m_timelineFlipCombo);
    flipRow->addStretch();
    groupLayout->addLayout(flipRow);

    // Timeline drop area (frames list)
    m_timelineDropArea = new QWidget(m_timelineEditorContainer);
    auto* dropLayout   = new QVBoxLayout(m_timelineDropArea);
    dropLayout->setContentsMargins(0, 4, 0, 0);
    m_timelineDragHintLabel = new QLabel(tr("Drag frames from layout canvas here"),
                                         m_timelineDropArea);
    dropLayout->addWidget(m_timelineDragHintLabel);
    m_timelineFramesList = new TimelineListWidget(m_timelineDropArea);
    m_timelineFramesList->setViewMode(QListWidget::IconMode);
    m_timelineFramesList->setFlow(QListWidget::LeftToRight);
    m_timelineFramesList->setWrapping(false);
    m_timelineFramesList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineFramesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_timelineFramesList->setResizeMode(QListWidget::Adjust);
    m_timelineFramesList->setIconSize(QSize(48, 48));
    m_timelineFramesList->setFixedHeight(96);
    dropLayout->addWidget(m_timelineFramesList);
    editorContainerLayout->addWidget(m_timelineDropArea);

}
