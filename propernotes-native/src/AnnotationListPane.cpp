// SPDX-License-Identifier: GPL-3.0-only
#include "AnnotationListPane.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace propernotes {

AnnotationListPane::AnnotationListPane(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* filters = new QFormLayout;
    filters->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_scope = new QComboBox;
    m_scope->addItem(QStringLiteral("整份 PDF"));
    m_scope->addItem(QStringLiteral("当前页"));
    m_scope->setAccessibleName(QStringLiteral("批注页面范围"));
    filters->addRow(QStringLiteral("范围"), m_scope);
    m_kind = new QComboBox;
    m_kind->addItem(QStringLiteral("全部类型"), -1);
    for (const Kind kind : {Kind::Pen, Kind::Highlighter, Kind::TextBox, Kind::Highlight,
                           Kind::Underline, Kind::Squiggly, Kind::StrikeOut, Kind::Line,
                           Kind::Arrow, Kind::Rectangle, Kind::Ellipse})
        m_kind->addItem(kindName(kind), int(kind));
    m_kind->setAccessibleName(QStringLiteral("批注类型"));
    filters->addRow(QStringLiteral("类型"), m_kind);
    layout->addLayout(filters);
    m_count = new QLabel;
    m_count->setTextFormat(Qt::PlainText);
    m_count->setWordWrap(true);
    layout->addWidget(m_count);
    m_list = new QListWidget;
    m_list->setAccessibleName(QStringLiteral("可定位的批注"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    layout->addWidget(m_list, 1);
    m_empty = new QLabel;
    m_empty->setTextFormat(Qt::PlainText);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);
    auto* paging = new QHBoxLayout;
    m_previous = new QPushButton(QStringLiteral("上一组"));
    m_next = new QPushButton(QStringLiteral("下一组"));
    paging->addWidget(m_previous);
    paging->addWidget(m_next);
    layout->addLayout(paging);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (isVisible()) rebuildRows(true);
    });
    const auto filterChanged = [this] {
        m_offset = 0;
        rebuildRows(false);
    };
    connect(m_scope, &QComboBox::currentIndexChanged, this, filterChanged);
    connect(m_kind, &QComboBox::currentIndexChanged, this, filterChanged);
    connect(m_previous, &QPushButton::clicked, this, [this] {
        m_offset = std::max<qsizetype>(0, m_offset - EntriesPerGroup);
        rebuildRows(false);
    });
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_offset + EntriesPerGroup < m_filtered.size()) m_offset += EntriesPerGroup;
        rebuildRows(false);
    });
    connect(m_list, &QListWidget::itemClicked, this,
        [this](QListWidgetItem* item) { activateRow(m_list->row(item)); });
    connect(m_list, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item) { activateRow(m_list->row(item)); });
    clearDocument();
}

QString AnnotationListPane::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Pen: return QStringLiteral("笔迹");
    case Kind::Highlighter: return QStringLiteral("自由荧光笔");
    case Kind::TextBox: return QStringLiteral("文字框");
    case Kind::Highlight: return QStringLiteral("正文高亮");
    case Kind::Underline: return QStringLiteral("下划线");
    case Kind::Squiggly: return QStringLiteral("波浪线");
    case Kind::StrikeOut: return QStringLiteral("删除线");
    case Kind::Line: return QStringLiteral("直线");
    case Kind::Arrow: return QStringLiteral("箭头");
    case Kind::Rectangle: return QStringLiteral("矩形");
    case Kind::Ellipse: return QStringLiteral("椭圆");
    }
    return {};
}

QVector<AnnotationListPane::Entry> AnnotationListPane::pageEntries(int page, const PageAnnotations& annotations)
{
    QVector<Entry> entries;
    entries.reserve(annotations.strokes.size() + annotations.textBoxes.size()
        + annotations.textMarkups.size() + annotations.shapes.size());
    for (const auto& markup : annotations.textMarkups) {
        Kind kind = Kind::Highlight;
        switch (markup.kind) {
        case TextMarkupKind::Highlight: kind = Kind::Highlight; break;
        case TextMarkupKind::Underline: kind = Kind::Underline; break;
        case TextMarkupKind::Squiggly: kind = Kind::Squiggly; break;
        case TextMarkupKind::StrikeOut: kind = Kind::StrikeOut; break;
        }
        entries.append({page, markup.id, kind, {}});
    }
    for (const auto& stroke : annotations.strokes)
        entries.append({page, stroke.id, stroke.kind == InkStrokeKind::Highlighter ? Kind::Highlighter : Kind::Pen, {}});
    for (const auto& shape : annotations.shapes) {
        Kind kind = Kind::Line;
        switch (shape.kind) {
        case ShapeKind::Line: kind = Kind::Line; break;
        case ShapeKind::Arrow: kind = Kind::Arrow; break;
        case ShapeKind::Rectangle: kind = Kind::Rectangle; break;
        case ShapeKind::Ellipse: kind = Kind::Ellipse; break;
        }
        entries.append({page, shape.id, kind, {}});
    }
    for (const auto& box : annotations.textBoxes) {
        QString snippet = box.text.left(120).simplified();
        if (!snippet.isEmpty() && snippet.at(snippet.size() - 1).isHighSurrogate()) snippet.chop(1);
        if (box.text.size() > 120) snippet.append(QChar(0x2026));
        entries.append({page, box.id, Kind::TextBox, std::move(snippet)});
    }
    return entries;
}

void AnnotationListPane::setDocument(const QMap<int, PageAnnotations>& annotations, int currentPage)
{
    m_refreshTimer.stop();
    m_entriesByPage.clear();
    for (auto it = annotations.cbegin(); it != annotations.cend(); ++it)
        m_entriesByPage.insert(it.key(), pageEntries(it.key(), it.value()));
    m_currentPage = currentPage;
    m_selectedPage = -1;
    m_selectedId = {};
    m_offset = 0;
    m_hasDocument = true;
    m_filtered.clear();
    {
        const QSignalBlocker blocked(m_list);
        m_list->clear();
    }
    m_count->clear();
    m_empty->setText(QStringLiteral("正在整理批注…"));
    m_empty->show();
    m_list->hide();
    m_previous->setEnabled(false);
    m_next->setEnabled(false);
    scheduleRefresh();
}

void AnnotationListPane::updatePageAnnotations(int page, const PageAnnotations& annotations)
{
    if (!m_hasDocument || page < 0) return;
    m_entriesByPage.insert(page, pageEntries(page, annotations));
    if (m_scope->currentIndex() == 0 || page == m_currentPage) scheduleRefresh();
}

void AnnotationListPane::setCurrentPage(int page)
{
    if (m_currentPage == page) return;
    m_currentPage = page;
    if (m_scope->currentIndex() == 1) {
        m_offset = 0;
        scheduleRefresh();
    }
}

void AnnotationListPane::clearDocument()
{
    m_refreshTimer.stop();
    m_entriesByPage.clear();
    m_filtered.clear();
    m_currentPage = m_selectedPage = -1;
    m_selectedId = {};
    m_offset = 0;
    m_hasDocument = false;
    rebuildRows(false);
}

void AnnotationListPane::scheduleRefresh()
{
    m_dirty = true;
    if (isVisible()) m_refreshTimer.start();
}

void AnnotationListPane::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty) rebuildRows(true);
}

void AnnotationListPane::rebuildRows(bool locateSelection)
{
    m_refreshTimer.stop();
    m_dirty = false;
    m_filtered.clear();
    const int kind = m_kind->currentData().toInt();
    auto firstPage = m_entriesByPage.cbegin();
    auto endPage = m_entriesByPage.cend();
    if (m_scope->currentIndex() == 1) {
        firstPage = m_entriesByPage.constFind(m_currentPage);
        endPage = firstPage;
        if (endPage != m_entriesByPage.cend()) ++endPage;
    }
    for (auto it = firstPage; it != endPage; ++it) {
        for (const auto& entry : it.value()) {
            if (kind < 0 || kind == int(entry.kind)) m_filtered.append(entry);
        }
    }
    qsizetype selected = -1;
    for (qsizetype index = 0; index < m_filtered.size(); ++index) {
        const auto& entry = m_filtered[index];
        if (entry.page == m_selectedPage && entry.id == m_selectedId) {
            selected = index;
            break;
        }
    }
    if (locateSelection && selected >= 0) m_offset = selected / EntriesPerGroup * EntriesPerGroup;
    if (selected < 0) {
        m_selectedId = {};
        m_selectedPage = -1;
    }
    const qsizetype lastOffset = m_filtered.isEmpty() ? 0 : (m_filtered.size() - 1) / EntriesPerGroup * EntriesPerGroup;
    m_offset = std::clamp(m_offset, qsizetype(0), lastOffset);
    const qsizetype end = std::min(m_offset + EntriesPerGroup, m_filtered.size());
    const QSignalBlocker blocked(m_list);
    m_list->clear();
    for (qsizetype index = m_offset; index < end; ++index) {
        const auto& entry = m_filtered[index];
        QString label = QStringLiteral("第 %1 页 · %2").arg(entry.page + 1).arg(kindName(entry.kind));
        if (!entry.snippet.isEmpty()) {
            label.append(QLatin1Char('\n'));
            label.append(entry.snippet);
        }
        auto* item = new QListWidgetItem(label, m_list);
        item->setToolTip(label);
        if (index == selected) m_list->setCurrentItem(item);
    }
    m_scope->setEnabled(m_hasDocument);
    m_kind->setEnabled(m_hasDocument);
    m_previous->setEnabled(m_offset > 0);
    m_next->setEnabled(end < m_filtered.size());
    m_count->setText(m_filtered.isEmpty() ? QStringLiteral("0 条批注")
        : QStringLiteral("%1–%2 / %3 条批注").arg(m_offset + 1).arg(end).arg(m_filtered.size()));
    m_empty->setText(!m_hasDocument ? QStringLiteral("打开 PDF 后可查看批注。")
        : m_scope->currentIndex() == 1 ? QStringLiteral("当前页没有符合条件的批注。")
                                     : QStringLiteral("此 PDF 没有符合条件的批注。"));
    m_empty->setVisible(m_filtered.isEmpty());
    m_list->setVisible(!m_filtered.isEmpty());
}

void AnnotationListPane::activateRow(int row)
{
    const qsizetype index = m_offset + row;
    if (row < 0 || index < 0 || index >= m_filtered.size()) return;
    const auto entry = m_filtered[index];
    m_selectedPage = entry.page;
    m_selectedId = entry.id;
    Q_EMIT annotationActivated(entry.page, entry.id);
}

} // namespace propernotes
