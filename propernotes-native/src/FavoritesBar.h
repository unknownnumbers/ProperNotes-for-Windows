// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FavoriteTools.h"
#include <QHash>
#include <QTimer>
#include <QWidget>
#include <optional>

class QAction;
class QCloseEvent;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace propernotes {
// Child overlay of the reading viewport, never an independent desktop window.
class FavoritesBar final : public QWidget {
    Q_OBJECT
public:
    explicit FavoritesBar(QWidget* parent);
    void setFavorites(const QVector<FavoriteTool>& favorites);
    void setBarPreferences(const FavoritesBarPreferences& preferences);
    void setCurrentTool(const FavoriteTool& liveSnapshot);
    void setCurrentTool(InkCanvas::Tool tool, const ToolPreferences& tools, const QColor& color, qreal widthPt);
    void setActiveFavoriteId(const QUuid& id);
    void setWritable(bool writable);
    void setActivationEnabled(bool enabled);
    void setPinned(bool pinned);
    bool isPinned() const { return m_preferences.pinned; }
    FavoritesBarPreferences snapshotPreferences() const { return currentLayout(); }
    QAction* visibilityAction() const { return m_visibilityAction; }
    void prepareForHostClose();

signals:
    void activated(const propernotes::FavoriteTool& favorite);
    void fixedToolRequested(propernotes::InkCanvas::Tool tool);
    void selectionOptionsRequested();
    void addCurrentRequested();
    void favoritesEdited(const QVector<propernotes::FavoriteTool>& favorites);
    void barPreferencesEdited(const propernotes::FavoritesBarPreferences& preferences);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void rebuild();
    void showMenu(const QPoint& global, const QUuid& id = {});
    void editFavorite(const QUuid& id);
    void openLibrary();
    void recordLayout();
    FavoritesBarPreferences currentLayout() const;
    void syncPlacementControls();
    void constrainGeometry();
    int minimumPaletteHeight() const;
    void handleGesture(int kind, int phase, const QPointF& global);
    void keyboardGeometry(int kind, int key, bool accelerated);
    bool requestedVisible() const;
    void setUserVisible(bool visible);
    void syncVisibility();
    int indexOf(const QUuid& id) const;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_entries = nullptr;
    QVBoxLayout* m_entriesLayout = nullptr;
    QToolButton* m_pan = nullptr;
    QToolButton* m_select = nullptr;
    QToolButton* m_selectMore = nullptr;
    QToolButton* m_add = nullptr;
    QToolButton* m_move = nullptr;
    QToolButton* m_topResize = nullptr;
    QToolButton* m_bottomResize = nullptr;
    QAction* m_visibilityAction = nullptr;
    QHash<QUuid, QToolButton*> m_buttons;
    QHash<QUuid, QToolButton*> m_moreButtons;
    QVector<FavoriteTool> m_favorites;
    FavoriteTool m_currentTool;
    FavoritesBarPreferences m_preferences;
    QUuid m_activeId;
    QUuid m_revealId;
    bool m_applying = false;
    bool m_hostClosing = false;
    bool m_preferencesApplied = false;
    bool m_writable = false;
    bool m_activationEnabled = true;
    std::optional<bool> m_visibilityOverride;
    struct Gesture { int kind; QPointF origin; QRect geometry; };
    std::optional<Gesture> m_gesture;
    QTimer m_layoutTimer;
};
} // namespace propernotes
