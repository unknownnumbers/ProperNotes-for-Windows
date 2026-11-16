// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "MainWindow.h"
#include "WorkspaceStore.h"
#include "FavoriteTools.h"
#include <optional>

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
}
