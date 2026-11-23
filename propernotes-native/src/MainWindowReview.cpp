// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindowState.h"
#include "DocumentSession.h"
#include "SettingsDialog.h"
#include "ThumbnailBrowser.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <utility>

namespace propernotes {
namespace {
constexpr std::array reviewKinds{TextMarkupKind::Highlight, TextMarkupKind::Underline,
                                 TextMarkupKind::StrikeOut, TextMarkupKind::Squiggly};
constexpr int maxHits = 1000;
constexpr qsizetype maxSearchQuads = 32000;

size_t kindIndex(TextMarkupKind kind)
{
    return size_t(std::find(reviewKinds.begin(), reviewKinds.end(), kind) - reviewKinds.begin());
}

PdfTextPage readPage(PdfDocument* document, int page, const std::atomic_bool* cancelled = nullptr)
{
    try {
        if (document) return document->extractTextPage(page, cancelled);
    } catch (const std::exception&) {
    } catch (...) {
    }
    PdfTextPage result;
    result.error = QStringLiteral("无法读取此页的 PDF 文字层。");
    return result;
}

QString resultSnippet(const QString& text, const PdfTextMatch& match)
{
    const qsizetype begin = std::max<qsizetype>(0, qsizetype(match.offset) - 24);
    const qsizetype end = std::min(text.size(), qsizetype(match.offset) + match.length + 32);
    QString result = text.mid(begin, end - begin).simplified();
    if (begin > 0) result.prepend(QChar(0x2026));
    if (end < text.size()) result.append(QChar(0x2026));
    return result;
}
}

void MainWindow::buildTextReviewUi(QToolBar* toolbar, QWidget* central)
{
    m_review = std::make_unique<ReviewState>();
    auto& review = *m_review;
    review.select = toolbar->addAction(QStringLiteral("选文字"));
    review.select->setCheckable(true);
    review.select->setToolTip(QStringLiteral("拖选本页 PDF 正文，可复制或标记。使用鼠标或单指拖选前，先开启“鼠标与手跟随工具”。"));
    connect(review.select, &QAction::triggered, this, [this](bool enabled) {
        chooseTool(enabled ? InkCanvas::Tool::TextSelect : InkCanvas::Tool::Pan);
    });

    review.menuButton = new QToolButton(toolbar);
    review.menuButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    review.menuButton->setPopupMode(QToolButton::InstantPopup);
    review.menuButton->setText(QStringLiteral("正文审阅"));
    auto* menu = new QMenu(review.menuButton);
    auto* toolMenu = menu->addMenu(QStringLiteral("选用审阅工具"));
    for (auto kind : reviewKinds) {
        auto* action = toolMenu->addAction(textMarkupKindName(kind));
        action->setCheckable(true);
        review.tools[kindIndex(kind)] = action;
        connect(action, &QAction::triggered, this, [this, kind] { chooseReviewTool(kind); });
    }
    menu->addSeparator();
    for (auto kind : reviewKinds) {
        auto* action = menu->addAction(textMarkupKindName(kind));
        review.kinds[kindIndex(kind)] = action;
        connect(action, &QAction::triggered, this, [this, kind] { applyTextReview(kind); });
    }
    menu->addSeparator();
    review.style = menu->addAction(QStringLiteral("当前审阅颜色与不透明度…"));
    connect(review.style, &QAction::triggered, this, [this] {
        const auto kind = m_review->kind;
        const auto serial = m_toolSerial;
        const auto color = QColorDialog::getColor(reviewColor(kind), this,
            textMarkupKindName(kind), QColorDialog::ShowAlphaChannel);
        if (!color.isValid() || color.alpha() == 0 || m_toolSerial != serial || color == reviewColor(kind)) return;
        m_review->colors[kindIndex(kind)] = color;
        if (const auto* tool = activeReviewTool()) {
            auto edited = captureReviewFavorite(kind, color, textMarkupKindName(kind), tool->reviewSingleUse);
            edited.id = {};
            m_liveFavorite->active = std::move(edited);
            rememberPersistentTool();
        }
        updateToolUi();
        updateTextReviewUi();
    });
    review.favorite = menu->addAction(QStringLiteral("收藏当前审阅工具…"));
    connect(review.favorite, &QAction::triggered, this, &MainWindow::favoriteTextReview);
    review.copy = menu->addAction(QStringLiteral("复制所选正文"));
    connect(review.copy, &QAction::triggered, this, &MainWindow::copyBodySelection);
    review.menuButton->setMenu(menu);
    toolbar->addWidget(review.menuButton);

    // Keep direct review actions available even when a narrow window moves
    // the reading toolbar's buttons into its overflow menu.
    review.selectionBar = new QWidget(central);
    auto* controls = new QHBoxLayout(review.selectionBar);
    controls->setContentsMargins(8, 2, 8, 2);
    controls->setSpacing(4);
    for (auto* action : review.kinds) {
        auto* button = new QToolButton;
        const auto label = action->text();
        action->setToolTip(QStringLiteral("对选区应用%1").arg(label));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setDefaultAction(action);
        controls->addWidget(button);
    }
    review.selectionStatus = new QLabel;
    review.selectionStatus->setTextFormat(Qt::PlainText);
    review.selectionStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    controls->addWidget(review.selectionStatus, 1);
    auto* layout = static_cast<QVBoxLayout*>(central->layout());
    layout->insertWidget(layout->indexOf(m_splitter), review.selectionBar);
    review.selectionBar->hide();

    review.copyShortcut = new QAction(m_view);
    review.copyShortcut->setShortcut(QKeySequence::Copy);
    review.copyShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(review.copyShortcut, &QAction::triggered, this, &MainWindow::copyBodySelection);
    m_view->addAction(review.copyShortcut);
    review.clearShortcut = new QAction(m_view);
    review.clearShortcut->setShortcut(QKeySequence(Qt::Key_Escape));
    review.clearShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(review.clearShortcut, &QAction::triggered, this, [this] {
        m_view->cancelGesture();
        m_view->clearTextSelection();
    });
    m_view->addAction(review.clearShortcut);

    review.searchPanel = new QWidget(central);
    auto* panelLayout = new QVBoxLayout(review.searchPanel);
    panelLayout->setContentsMargins(8, 4, 8, 4);
    panelLayout->setSpacing(4);
    auto* row = new QHBoxLayout;
    review.query = new QLineEdit;
    review.query->setMaxLength(512);
    review.query->setPlaceholderText(QStringLiteral("搜索 PDF 正文"));
    review.query->setAccessibleName(QStringLiteral("搜索 PDF 正文"));
    row->addWidget(review.query, 1);
    review.matchCase = new QCheckBox(QStringLiteral("区分大小写"));
    row->addWidget(review.matchCase);
    review.runSearch = new QPushButton(QStringLiteral("查找"));
    review.stopSearch = new QPushButton(QStringLiteral("停止"));
    review.previous = new QPushButton(QStringLiteral("上一处"));
    review.next = new QPushButton(QStringLiteral("下一处"));
    auto* close = new QPushButton(QStringLiteral("关闭搜索"));
    for (auto* button : {review.runSearch, review.stopSearch, review.previous, review.next, close})
        row->addWidget(button);
    panelLayout->addLayout(row);
    review.progress = new QLabel;
    review.progress->setTextFormat(Qt::PlainText);
    panelLayout->addWidget(review.progress);
    review.results = new QListWidget;
    review.results->setMaximumHeight(128);
    review.results->setUniformItemSizes(true);
    review.results->setAccessibleName(QStringLiteral("正文搜索结果"));
    panelLayout->addWidget(review.results);
    layout->insertWidget(layout->indexOf(m_splitter), review.searchPanel);
    review.searchPanel->hide();

    review.find = toolbar->addAction(QStringLiteral("搜索"));
    review.find->setShortcut(QKeySequence::Find);
    connect(review.find, &QAction::triggered, this, [this] {
        m_view->finishGesture();
        m_review->searchPanel->show();
        m_review->query->setFocus();
        m_review->query->selectAll();
    });
    connect(review.query, &QLineEdit::returnPressed, this, &MainWindow::startTextSearch);
    connect(review.runSearch, &QPushButton::clicked, this, &MainWindow::startTextSearch);
    connect(review.stopSearch, &QPushButton::clicked, this, [this] {
        if (m_review->cancelSearch) m_review->cancelSearch->store(true);
        m_review->running = false;
        m_review->cancelled = true;
        updateSearchProgress();
        updateTextReviewUi();
    });
    connect(close, &QPushButton::clicked, this, [this] {
        resetTextSearch(true);
        m_view->setFocus();
    });
    connect(review.query, &QLineEdit::textEdited, this, [this] { resetTextSearch(false); });
    connect(review.matchCase, &QCheckBox::toggled, this, [this] { resetTextSearch(false); });
    connect(review.results, &QListWidget::currentRowChanged, this, [this](int index) {
        if (m_fileBusy || m_closingAll || index < 0 || index >= m_review->hits.size()) return;
        const auto& hit = m_review->hits.at(index);
        m_view->showSearchResult(hit.page, hit.quads);
    });
    auto moveResult = [this](int direction) {
        const int count = m_review->results->count();
        if (!count) return;
        const int current = m_review->results->currentRow();
        m_review->results->setCurrentRow(current < 0 ? (direction > 0 ? 0 : count - 1)
                                                   : (current + direction + count) % count);
    };
    connect(review.previous, &QPushButton::clicked, this, [moveResult] { moveResult(-1); });
    connect(review.next, &QPushButton::clicked, this, [moveResult] { moveResult(1); });
    connect(m_view, &DocumentView::textPageRequested, this, &MainWindow::requestTextPage);
    connect(m_view, &DocumentView::textSelectionChanged, this, [this] {
        m_review->reviewFeedback.clear();
        updateTextReviewUi();
    });
    connect(m_view, &DocumentView::textSelectionCompleted, this, [this] {
        if (const auto* tool = activeReviewTool()) applyTextReview(*tool->markupKind);
    });
    updateTextReviewUi();
}

void MainWindow::updateTextReviewUi()
{
    if (!m_review) return;
    auto& review = *m_review;
    const bool readable = m_pageCount > 0 && !m_fileBusy && !m_closingAll && !m_thumbnailBrowser;
    const auto selection = m_view->textSelection();
    const bool hasSelection = readable && !selection.text.isEmpty();
    const bool editable = readable && !m_readOnly;
    const auto* tool = activeReviewTool();
    review.select->setEnabled(readable);
    review.select->setChecked(m_tool == InkCanvas::Tool::TextSelect && !tool);
    review.find->setEnabled(readable);
    review.copy->setEnabled(hasSelection);
    review.copyShortcut->setEnabled(hasSelection && !m_view->isTextEditing());
    review.clearShortcut->setEnabled(readable && m_tool == InkCanvas::Tool::TextSelect && !m_view->isTextEditing());
    review.menuButton->setEnabled(readable);
    for (auto* action : review.kinds) action->setEnabled(editable);
    for (auto kind : reviewKinds) {
        auto* action = review.tools[kindIndex(kind)];
        action->setEnabled(editable && !m_preferencesBusy && !m_backupBusy && !m_settingsDialog);
        action->setChecked(tool && tool->markupKind == kind);
    }
    review.style->setEnabled(editable);
    review.favorite->setEnabled(editable && m_preferencesWritable && !m_preferencesBusy && !m_backupBusy);
    // Visibility follows the tool, not individual input updates. In particular,
    // clearing an old selection must not resize the viewport midway through a drag.
    review.selectionBar->setVisible(m_pageCount > 0 && m_tool == InkCanvas::Tool::TextSelect);
    const QString dragText = m_preferences.tools.inputFollowsTool
        ? QStringLiteral("拖选本页正文") : QStringLiteral("用笔拖选本页正文");
    QString hint;
    if (!readable) hint = QStringLiteral("正在处理文档…");
    else if (!review.reviewFeedback.isEmpty()) hint = review.reviewFeedback;
    else if (!editable) hint = QStringLiteral("此 PDF 只读，可选字和复制。");
    else if (!hasSelection && tool)
        hint = QStringLiteral("%1工具 · %2 · %3，松开即可标记。")
                   .arg(textMarkupKindName(*tool->markupKind), tool->reviewSingleUse ? QStringLiteral("单次") : QStringLiteral("持续"), dragText);
    else if (!hasSelection) hint = QStringLiteral("%1，再点一种标记。").arg(dragText);
    else if (!selection.reviewSupported) hint = selection.reviewIssue.isEmpty()
        ? QStringLiteral("此选区可复制，几何不适合标记。") : selection.reviewIssue;
    else if (selection.skippedReviewGlyphs > 0)
        hint = QStringLiteral("可标记部分正文，将跳过 %1 个字形。按 Ctrl+C 可复制整个选区。")
                   .arg(selection.skippedReviewGlyphs);
    else hint = QStringLiteral("已选中正文。点标记，或按 Ctrl+C 复制。");
    review.selectionStatus->setText(hint);
    review.selectionStatus->setToolTip(hint.toHtmlEscaped());
    review.runSearch->setEnabled(readable && !review.query->text().trimmed().isEmpty());
    review.stopSearch->setEnabled(review.running);
    review.previous->setEnabled(readable && !review.hits.isEmpty());
    review.next->setEnabled(readable && !review.hits.isEmpty());
    review.results->setEnabled(readable);
}

void MainWindow::requestTextPage(int page)
{
    if (page < 0 || page >= m_pageCount || !m_workerContext) return;
    const auto session = m_session;
    const quint64 generation = m_documentGeneration;
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [session, generation, window, page] {
        if (!window) return;
        auto text = std::make_shared<const PdfTextPage>(readPage(session->document.get(), page));
        if (!window) return;
        QMetaObject::invokeMethod(window.data(), [window, generation, page, text] {
            if (window && window->m_documentGeneration == generation) window->m_view->deliverTextPage(page, text);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::resetTextSearch(bool hidePanel)
{
    if (!m_review) return;
    auto& review = *m_review;
    if (review.cancelSearch) review.cancelSearch->store(true);
    review.cancelSearch.reset();
    review.running = review.cancelled = review.capped = false;
    review.hits.clear();
    review.searchQuads = review.pagesVisited = review.incompletePages = 0;
    review.runningQuery.clear();
    review.results->clear();
    review.progress->clear();
    m_view->clearSearchResult();
    if (hidePanel) {
        review.searchPanel->hide();
        review.query->clear();
        review.reviewFeedback.clear();
    }
    updateTextReviewUi();
}

void MainWindow::startTextSearch()
{
    if (m_fileBusy || m_pageCount < 1 || m_closingAll || m_thumbnailBrowser) return;
    const QString query = m_review->query->text();
    if (query.trimmed().isEmpty()) return;
    m_view->finishGesture();
    resetTextSearch(false);
    m_review->runningQuery = query;
    m_review->caseSensitive = m_review->matchCase->isChecked();
    m_review->cancelSearch = std::make_shared<std::atomic_bool>(false);
    m_review->running = true;
    updateSearchProgress();
    updateTextReviewUi();
    continueTextSearch(0);
}

void MainWindow::continueTextSearch(int page)
{
    if (!m_review->running) return;
    if (page >= m_pageCount || m_review->hits.size() >= maxHits) {
        m_review->capped = m_review->capped || page < m_pageCount;
        m_review->running = false;
        updateSearchProgress();
        updateTextReviewUi();
        return;
    }
    const auto session = m_session;
    const auto token = m_review->cancelSearch;
    const auto query = m_review->runningQuery;
    const bool caseSensitive = m_review->caseSensitive;
    const int remaining = maxHits - int(m_review->hits.size());
    const QPointer<MainWindow> window(this);
    struct MatchRow { QVector<TextMarkupQuad> quads; QString excerpt; };
    QMetaObject::invokeMethod(m_workerContext,
        [window, session, token, query, caseSensitive, remaining, page] {
            if (token->load()) return;
            auto text = readPage(session->document.get(), page, token.get());
            if (!window || token->load()) return;
            bool incomplete = !text.error.isEmpty() || text.truncated;
            bool capped = false;
            QVector<MatchRow> rows;
            try {
                if (text.error.isEmpty()) {
                    auto matches = text.search(query, caseSensitive, remaining + 1);
                    capped = matches.size() > remaining;
                    if (capped) matches.resize(remaining);
                    rows.reserve(matches.size());
                    for (auto& match : matches) {
                        if (match.quads.size() > 128) match.quads.clear();
                        rows.append({std::move(match.quads), resultSnippet(text.text, match)});
                    }
                }
            } catch (...) {
                incomplete = true;
            }
            if (!window || token->load()) return;
            QMetaObject::invokeMethod(window.data(),
                [window, token, page, incomplete, capped, rows = std::move(rows)]() mutable {
                    if (!window || token->load() || window->m_review->cancelSearch != token) return;
                    auto& review = *window->m_review;
                    if (incomplete) ++review.incompletePages;
                    ++review.pagesVisited;
                    review.capped = review.capped || capped;
                    for (auto& row : rows) {
                        if (review.searchQuads + row.quads.size() > maxSearchQuads) row.quads.clear();
                        review.searchQuads += row.quads.size();
                        review.hits.append({page, std::move(row.quads)});
                        review.results->addItem(QStringLiteral("第 %1 页   %2").arg(page + 1).arg(row.excerpt));
                    }
                    window->updateSearchProgress();
                    window->updateTextReviewUi();
                    // Queue one page at a time so rendering and file work can proceed between pages.
                    window->continueTextSearch(page + 1);
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void MainWindow::updateSearchProgress()
{
    const auto& review = *m_review;
    QString status;
    if (review.running) status = QStringLiteral("正在搜索");
    else if (review.cancelled) status = QStringLiteral("已停止，保留已找到的结果");
    else if (review.capped) status = QStringLiteral("已达到结果上限");
    else status = QStringLiteral("搜索完成");
    status += QStringLiteral(" · %1/%2 页 · %3 处").arg(review.pagesVisited).arg(m_pageCount).arg(review.hits.size());
    if (review.incompletePages) status += QStringLiteral(" · %1 页无法完整读取").arg(review.incompletePages);
    if (!review.running && !review.cancelled && review.hits.isEmpty())
        status += QStringLiteral(" · 仅搜索 PDF 已有文字层");
    review.progress->setText(status);
}

void MainWindow::copyBodySelection()
{
    if (m_fileBusy || m_closingAll || m_view->isTextEditing()) return;
    const auto selection = m_view->textSelection();
    if (!selection.text.isEmpty()) QApplication::clipboard()->setText(selection.text);
}

const FavoriteTool* MainWindow::activeReviewTool() const
{
    if (m_tool != InkCanvas::Tool::TextSelect || !m_liveFavorite || !m_liveFavorite->active ||
        !m_liveFavorite->active->markupKind)
        return nullptr;
    return &*m_liveFavorite->active;
}

void MainWindow::chooseReviewTool(TextMarkupKind kind)
{
    auto tool = captureReviewFavorite(kind, m_review->colors.at(kindIndex(kind)),
        textMarkupKindName(kind), m_review->singleUse.at(kindIndex(kind)));
    tool.id = {};
    applyFavorite(tool);
}

void MainWindow::setActiveReviewSingleUse(bool singleUse)
{
    if (!activeReviewTool() || m_pageCount < 1 || m_readOnly || m_fileBusy || m_preferencesBusy || m_backupBusy ||
        m_closingAll || m_thumbnailBrowser || m_settingsDialog) return;
    auto& tool = *m_liveFavorite->active;
    ++m_toolSerial;
    tool.reviewSingleUse = singleUse;
    if (tool.id.isNull()) m_review->singleUse.at(kindIndex(*tool.markupKind)) = singleUse;
    if (!singleUse) rememberPersistentTool();
    m_review->reviewFeedback.clear();
    updateToolUi();
    updateTextReviewUi();
}

QColor MainWindow::reviewColor(TextMarkupKind kind) const
{
    const auto* tool = activeReviewTool();
    if (tool && tool->markupKind == kind) {
        QColor color = tool->color;
        color.setAlphaF(float(tool->tools.opacity));
        return color;
    }
    return m_review->colors.at(kindIndex(kind));
}

void MainWindow::applyTextReview(TextMarkupKind kind)
{
    if (m_fileBusy || m_readOnly || m_pageCount < 1 || m_closingAll || m_thumbnailBrowser) return;
    const auto* tool = activeReviewTool();
    const bool completeActiveTool = tool && tool->markupKind == kind;
    const bool returnAfterMark = completeActiveTool && tool->reviewSingleUse;
    if (!tool) m_review->kind = kind;
    if (m_tool != InkCanvas::Tool::TextSelect) chooseTool(InkCanvas::Tool::TextSelect);
    updateToolUi();
    updateTextReviewUi();
    const int page = m_view->textSelectionPage();
    const auto selection = m_view->textSelection();
    if (page < 0 || page >= m_pageCount || selection.text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("先拖选一页上的正文，再点“%1”。").arg(textMarkupKindName(kind)), 6000);
        m_view->setFocus();
        return;
    }
    if (!selection.reviewSupported || selection.reviewQuads.isEmpty() || selection.reviewQuads.size() > MaxTextMarkupQuads) {
        m_review->reviewFeedback = selection.reviewIssue.isEmpty()
            ? QStringLiteral("此选区没有可写入的完整坐标。可复制正文，或尝试缩小选区。")
            : selection.reviewIssue;
        updateTextReviewUi();
        return;
    }
    TextMarkup markup;
    markup.kind = kind;
    markup.color = reviewColor(kind);
    markup.quads = selection.reviewQuads;
    const auto before = m_pageAnnotations.value(page);
    auto after = before;
    after.textMarkups.append(markup);
    commitAnnotations(page, before, after, QStringLiteral("text-review"), markup.id);
    if (!m_lastCommitAccepted) {
        m_review->reviewFeedback = QStringLiteral("标记未应用，选区仍保留。");
        updateTextReviewUi();
        return;
    }
    if (completeActiveTool) toolCompleted(InkCanvas::Tool::TextSelect, QStringLiteral("text-review"));
    else m_lastCommitAccepted = false;
    m_view->updatePageAnnotations(page, m_pageAnnotations.value(page));
    m_view->clearTextSelection();
    m_review->reviewFeedback = selection.skippedReviewGlyphs > 0
        ? QStringLiteral("已对可用正文添加%1，跳过 %2 个字形。按 Ctrl+S 保存。")
              .arg(textMarkupKindName(kind)).arg(selection.skippedReviewGlyphs)
        : QStringLiteral("已添加%1。按 Ctrl+S 保存到 PDF。").arg(textMarkupKindName(kind));
    if (returnAfterMark) statusBar()->showMessage(m_review->reviewFeedback, 4500);
    updateTextReviewUi();
    m_view->setFocus();
}

void MainWindow::favoriteTextReview()
{
    if (!m_preferencesWritable || m_preferencesBusy || m_fileBusy || m_backupBusy || m_closingAll) return;
    const auto kind = m_review->kind;
    bool accepted = false;
    const auto color = reviewColor(kind);
    const auto* active = activeReviewTool();
    const bool singleUse = active ? active->reviewSingleUse : m_review->singleUse.at(kindIndex(kind));
    const QString name = QInputDialog::getText(this, QStringLiteral("收藏审阅工具"), QStringLiteral("预设名称"),
        QLineEdit::Normal, textMarkupKindName(kind), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    auto values = m_preferences;
    values.favorites.append(captureReviewFavorite(kind, color, name, singleUse));
    persistPreferences(values);
}
} // namespace propernotes
