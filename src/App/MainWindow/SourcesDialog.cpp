#include "SourcesDialog.h"

#include <QApplication>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QStyle>
#include <QContextMenuEvent>

namespace {
QIcon iconForSourceType(SourceType t) {
    switch (t) {
    case SourceType::Folder:
        return QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon);
    case SourceType::SingleImage:
        return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    case SourceType::Archive:
        return QApplication::style()->standardIcon(QStyle::SP_DriveFDIcon);
    case SourceType::Url:
        return QApplication::style()->standardIcon(QStyle::SP_CommandLink);
    }
    return QIcon();
}

QString labelForSourceType(SourceType t) {
    switch (t) {
    case SourceType::Folder:      return QStringLiteral("Folder");
    case SourceType::SingleImage: return QStringLiteral("Image");
    case SourceType::Archive:     return QStringLiteral("Archive");
    case SourceType::Url:         return QStringLiteral("URL");
    }
    return QStringLiteral("Folder");
}
} // namespace

SourcesDialog::SourcesDialog(QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Project Sources"));
    setMinimumSize(560, 320);
    buildUi();
}

void SourcesDialog::buildUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Description
    auto* descLabel = new QLabel(
        tr("Each source provides images for the layout. "
           "Folder sources are watched for changes; archive and image sources are cached locally. "
           "Double-click a name to rename it."), this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Toolbar row
    auto* toolbar = new QHBoxLayout;
    auto* addBtn = new QToolButton(this);
    addBtn->setText(tr("Add ▼"));
    addBtn->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addBtn);
    addMenu->addAction(tr("Add Folder…"),  this, &SourcesDialog::addFolderRequested);
    addMenu->addAction(tr("Add File…"),    this, &SourcesDialog::addFileRequested);
    addMenu->addAction(tr("Add Archive…"), this, &SourcesDialog::addArchiveRequested);
    addMenu->addAction(tr("Add URL…"),     this, &SourcesDialog::addUrlRequested);
    addBtn->setMenu(addMenu);
    toolbar->addWidget(addBtn);
    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // Sources table
    m_sourcesTable = new QTableWidget(0, 5, this);
    m_sourcesTable->setHorizontalHeaderLabels({
        QString(), tr("Name"), tr("Type"), tr("Path / URL"), QString()
    });
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_sourcesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sourcesTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_sourcesTable->verticalHeader()->setVisible(false);
    connect(m_sourcesTable, &QTableWidget::itemChanged,
            this, &SourcesDialog::onItemChanged);
    mainLayout->addWidget(m_sourcesTable, 1);

    // Orphaned sprites section
    m_orphanLabel = new QLabel(tr("Orphaned sprites:"), this);
    mainLayout->addWidget(m_orphanLabel);

    m_orphanList = new QListWidget(this);
    m_orphanList->setMaximumHeight(100);
    m_orphanList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_orphanList, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* item = m_orphanList->itemAt(pos);
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction(tr("Restore"), this, [this, path]() {
            emit restoreOrphanRequested(path);
        });
        menu.addAction(tr("Discard"), this, [this, path]() {
            emit discardOrphanRequested(path);
        });
        menu.exec(m_orphanList->mapToGlobal(pos));
    });
    mainLayout->addWidget(m_orphanList);

    // Close button row
    auto* closeRow = new QHBoxLayout;
    closeRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    mainLayout->addLayout(closeRow);
}

void SourcesDialog::refresh(const QVector<ProjectSource>& sources,
                             const QStringList& orphaned) {
    m_programmaticUpdate = true;

    // Rebuild sources table
    m_sourcesTable->setRowCount(0);
    for (int i = 0; i < sources.size(); ++i) {
        const ProjectSource& src = sources.at(i);
        m_sourcesTable->insertRow(i);

        // Column 0: type icon
        auto* iconItem = new QTableWidgetItem(iconForSourceType(src.type), QString());
        iconItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        iconItem->setToolTip(labelForSourceType(src.type));
        m_sourcesTable->setItem(i, 0, iconItem);

        // Column 1: name (editable)
        auto* nameItem = new QTableWidgetItem(src.name);
        nameItem->setData(Qt::UserRole, i);
        m_sourcesTable->setItem(i, 1, nameItem);

        // Column 2: type label
        auto* typeItem = new QTableWidgetItem(labelForSourceType(src.type));
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_sourcesTable->setItem(i, 2, typeItem);

        // Column 3: path
        auto* pathItem = new QTableWidgetItem(src.originalPath);
        pathItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        pathItem->setToolTip(src.originalPath);
        m_sourcesTable->setItem(i, 3, pathItem);

        // Column 4: action buttons widget
        auto* actionsWidget = new QWidget;
        auto* actLayout = new QHBoxLayout(actionsWidget);
        actLayout->setContentsMargins(2, 0, 2, 0);
        actLayout->setSpacing(2);

        auto* syncBtn   = new QPushButton(tr("Sync"), actionsWidget);
        auto* removeBtn = new QPushButton(tr("Remove"), actionsWidget);
        syncBtn->setFixedHeight(22);
        removeBtn->setFixedHeight(22);

        const int capturedIndex = i;
        connect(syncBtn,   &QPushButton::clicked, this, [this, capturedIndex]() {
            emit syncSourceRequested(capturedIndex);
        });
        connect(removeBtn, &QPushButton::clicked, this, [this, capturedIndex]() {
            emit removeSourceRequested(capturedIndex);
        });

        actLayout->addWidget(syncBtn);
        actLayout->addWidget(removeBtn);
        m_sourcesTable->setCellWidget(i, 4, actionsWidget);
    }

    // Rebuild orphan list
    m_orphanList->clear();
    for (const QString& p : orphaned) {
        auto* item = new QListWidgetItem(p, m_orphanList);
        item->setData(Qt::UserRole, p);
    }
    m_orphanLabel->setVisible(!orphaned.isEmpty());
    m_orphanList->setVisible(!orphaned.isEmpty());

    m_programmaticUpdate = false;
}

void SourcesDialog::onAddClicked() {
    // Handled via the popup menu actions connected in buildUi()
}

void SourcesDialog::onItemChanged(QTableWidgetItem* item) {
    if (m_programmaticUpdate) return;
    // Only column 1 (name) is editable
    if (item->column() != 1) return;
    const int sourceIndex = item->data(Qt::UserRole).toInt();
    emit sourceRenamed(sourceIndex, item->text());
}
