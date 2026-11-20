// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FavoriteTools.h"
#include <QDockWidget>
#include <QHash>
#include <QTimer>
#include <optional>

class QAction;
class QMainWindow;
class QToolButton;
class QVBoxLayout;

namespace propernotes {
class FavoritesBar final : public QDockWidget {
    Q_OBJECT
public:
    explicit FavoritesBar(QMainWindow* parent);
    void setFavorites(const QVector<FavoriteTool>& favorites);
    void setBarPreferences(const FavoritesBarPreferences& preferences);
    void setCurrentTool(const FavoriteTool& liveSnapshot);
    void setCurrentTool(InkCanvas::Tool tool, const ToolPreferences& tools, const QColor& color, qreal widthPt);
    void setActiveFavoriteId(const QUuid& id);
    void setWritable(bool writable);
    void setActivationEnabled(bool enabled);
    QAction* visibilityAction() const { return m_visibilityAction; }
    void prepareForHostClose();

signals:
    void activated(const propernotes::FavoriteTool& favorite);
    void addCurrentRequested();
    void favoritesEdited(const QVector<propernotes::FavoriteTool>& favorites);
    void barPreferencesEdited(const propernotes::FavoritesBarPreferences& preferences);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void rebuild();
    void showMenu(const QPoint& global, const QUuid& id = {});
    void editFavorite(const QUuid& id);
    void proposeLayout(FavoritesBarPreferences preferences);
    void recordLayout();
    FavoritesBarPreferences currentLayout() const;
    bool requestedVisible() const;
    void setUserVisible(bool visible);
    void syncVisibility();
    int indexOf(const QUuid& id) const;
    QMainWindow* m_mainWindow = nullptr;
    QWidget* m_contents = nullptr;
    QWidget* m_entries = nullptr;
    QVBoxLayout* m_entriesLayout = nullptr;
    QToolButton* m_current = nullptr;
    QToolButton* m_collapse = nullptr;
    QToolButton* m_menu = nullptr;
    QAction* m_visibilityAction = nullptr;
    QHash<QUuid, QToolButton*> m_buttons;
    QVector<FavoriteTool> m_favorites;
    FavoriteTool m_currentTool;
    FavoritesBarPreferences m_preferences;
    QUuid m_activeId;
    QString m_dragToken;
    bool m_applying = false;
    bool m_hostAvailable = false;
    bool m_hostClosing = false;
    bool m_preferencesApplied = false;
    bool m_writable = false;
    bool m_activationEnabled = true;
    std::optional<bool> m_visibilityOverride;
    QTimer m_layoutTimer;
};
} // namespace propernotes
