// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "MainWindow.h"
#include "WorkspaceStore.h"
#include "FavoriteTools.h"
#include <optional>
#include <array>
#include "PdfTextLayer.h"

class QCheckBox;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace propernotes {
struct MainWindow::WorkspaceState { WorkspaceValues values; };
struct MainWindow::FavoriteState {
    struct RuntimeTool {
        InkCanvas::Tool tool = InkCanvas::Tool::Pan;
        FavoriteTool snapshot;
        std::optional<FavoriteTool> favorite;
    };
    std::optional<FavoriteTool> active;
    std::optional<FavoritesBarPreferences> queuedBar;
    QVector<RuntimeTool> persistent;
    bool pendingDefaultChange = false;
};
struct MainWindow::ReviewState {
    QAction* select = nullptr;
    QAction* copy = nullptr;
    QAction* copyShortcut = nullptr;
    QAction* clearShortcut = nullptr;
    QAction* find = nullptr;
    QAction* apply = nullptr;
    QAction* style = nullptr;
    QAction* favorite = nullptr;
    std::array<QAction*, 4> kinds{};
    QToolButton* menuButton = nullptr;
    QWidget* selectionBar = nullptr;
    QLabel* selectionStatus = nullptr;
    QString reviewFeedback;
    QWidget* searchPanel = nullptr;
    QLineEdit* query = nullptr;
    QCheckBox* matchCase = nullptr;
    QPushButton* runSearch = nullptr;
    QPushButton* stopSearch = nullptr;
    QPushButton* previous = nullptr;
    QPushButton* next = nullptr;
    QLabel* progress = nullptr;
    QListWidget* results = nullptr;
    struct SearchHit {
        int page;
        QVector<TextMarkupQuad> quads;
    };
    QVector<SearchHit> hits;
    std::shared_ptr<std::atomic_bool> cancelSearch;
    QString runningQuery;
    qsizetype searchQuads = 0;
    int pagesVisited = 0;
    int incompletePages = 0;
    bool running = false;
    bool cancelled = false;
    bool capped = false;
    bool caseSensitive = false;
    TextMarkupKind kind = TextMarkupKind::Highlight;
    std::array<QColor, 4> colors{QColor(248, 203, 33, 112), QColor("#2564bc"),
                                  QColor("#ba3434"), QColor("#ac580f")};
};
}
