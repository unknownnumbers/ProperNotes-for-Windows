// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindowState.h"
#include "DocumentSession.h"
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
    review.select->setToolTip(QStringLiteral("拖选这一页的 PDF 正文，可复制或添加审阅标记"));
    connect(review.select, &QAction::triggered, this, [this](bool enabled) {
        chooseTool(enabled ? InkCanvas::Tool::TextSelect : InkCanvas::Tool::Pan);
    });

    review.menuButton = new QToolButton(toolbar);
    review.menuButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    review.menuButton->setPopupMode(QToolButton::MenuButtonPopup);
    review.apply = new QAction(this);
    connect(review.apply, &QAction::triggered, this, [this] { applyTextReview(m_review->kind); });
    review.menuButton->setDefaultAction(review.apply);
    auto* menu = new QMenu(review.menuButton);
    for (auto kind : reviewKinds) {
        auto* action = menu->addAction(textMarkupKindName(kind));
        review.kinds[kindIndex(kind)] = action;
        connect(action, &QAction::triggered, this, [this, kind] { applyTextReview(kind); });
    }
    menu->addSeparator();
    review.style = menu->addAction(QStringLiteral("当前审阅颜色与不透明度…"));
    connect(review.style, &QAction::triggered, this, [this] {
        const auto kind = m_review->kind;
        const auto color = QColorDialog::getColor(reviewColor(kind), this,
            textMarkupKindName(kind), QColorDialog::ShowAlphaChannel);
        if (!color.isValid() || color.alpha() == 0) return;
        if (m_liveFavorite->active && m_liveFavorite->active->markupKind) clearFavoriteSelection();
        m_review->colors[kindIndex(kind)] = color;
        updateToolUi();
        updateTextReviewUi();
    });
    review.favorite = menu->addAction(QStringLiteral("收藏当前审阅样式…"));
    connect(review.favorite, &QAction::triggered, this, &MainWindow::favoriteTextReview);
    review.copy = menu->addAction(QStringLiteral("复制所选正文"));
    connect(review.copy, &QAction::triggered, this, &MainWindow::copyBodySelection);
    review.menuButton->setMenu(menu);
    toolbar->addWidget(review.menuButton);

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
    auto* layout = static_cast<QVBoxLayout*>(central->layout());
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
    connect(m_view, &DocumentView::textSelectionChanged, this, &MainWindow::updateTextReviewUi);
    updateTextReviewUi();
}

void MainWindow::updateTextReviewUi()
{
    if (!m_review) return;
    auto& review = *m_review;
    const bool readable = m_pageCount > 0 && !m_fileBusy && !m_closingAll && !m_thumbnailBrowser;
    const bool hasSelection = readable && !m_view->textSelection().text.isEmpty();
    const bool editable = readable && !m_readOnly;
    review.select->setEnabled(readable);
    review.select->setChecked(m_tool == InkCanvas::Tool::TextSelect);
    review.find->setEnabled(readable);
    review.copy->setEnabled(hasSelection);
    review.copyShortcut->setEnabled(hasSelection && !m_view->isTextEditing());
    review.clearShortcut->setEnabled(hasSelection && m_tool == InkCanvas::Tool::TextSelect && !m_view->isTextEditing());
    review.apply->setText(textMarkupKindName(review.kind));
    review.apply->setToolTip(QStringLiteral("对所选正文应用%1").arg(textMarkupKindName(review.kind)));
    review.apply->setEnabled(editable);
    review.menuButton->setEnabled(readable);
    for (auto* action : review.kinds) action->setEnabled(editable);
    review.style->setEnabled(editable);
    review.favorite->setEnabled(editable && m_preferencesWritable && !m_preferencesBusy && !m_backupBusy);
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

QColor MainWindow::reviewColor(TextMarkupKind kind) const
{
    const auto& active = m_liveFavorite->active;
    if (active && active->markupKind == kind) {
        QColor color = active->color;
        color.setAlphaF(float(active->tools.opacity));
        return color;
    }
    return m_review->colors.at(kindIndex(kind));
}

void MainWindow::applyTextReview(TextMarkupKind kind)
{
    if (m_fileBusy || m_readOnly || m_pageCount < 1 || m_closingAll || m_thumbnailBrowser) return;
    m_review->kind = kind;
    if (m_liveFavorite->active && m_liveFavorite->active->markupKind &&
        m_liveFavorite->active->markupKind != kind) clearFavoriteSelection();
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
    if (!selection.reviewSupported || selection.quads.isEmpty() || selection.quads.size() > MaxTextMarkupQuads) {
        statusBar()->showMessage(QStringLiteral("这个选区可复制，但无法添加审阅标记。请尝试较短的横排或直角旋转选区。"), 7000);
        return;
    }
    TextMarkup markup;
    markup.kind = kind;
    markup.color = reviewColor(kind);
    markup.quads = selection.quads;
    const auto before = m_pageAnnotations.value(page);
    auto after = before;
    after.textMarkups.append(markup);
    commitAnnotations(page, before, after, QStringLiteral("text-review"), markup.id);
    if (!m_lastCommitAccepted) return;
    m_view->updatePageAnnotations(page, m_pageAnnotations.value(page));
    m_view->clearTextSelection();
    m_view->setFocus();
}

void MainWindow::favoriteTextReview()
{
    if (!m_preferencesWritable || m_preferencesBusy || m_fileBusy || m_backupBusy || m_closingAll) return;
    const auto kind = m_review->kind;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("收藏审阅样式"), QStringLiteral("预设名称"),
        QLineEdit::Normal, textMarkupKindName(kind), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    auto values = m_preferences;
    values.favorites.append(captureReviewFavorite(kind, reviewColor(kind), name));
    persistPreferences(values);
}
} // namespace propernotes
