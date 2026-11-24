// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QList>
#include <QSet>
#include <QTimer>

class QAction;
class QFrame;
class QMenu;
class QToolButton;
class QWidget;

namespace propernotes {
// Child overlays, never separate windows. Observe input without consuming it.
class ReadingControls final : public QObject {
    Q_OBJECT
public:
    ReadingControls(QWidget* viewport, const QList<QAction*>& history,
                    QAction* zoomOut, QAction* zoomIn, QMenu* viewMenu, QObject* parent);
    void setHideDelaySeconds(int seconds);
    void setZoom(qreal zoom);
    void revealHistory();
    void revealZoom();
    void refreshAppearance();
    // Reserve both full panels and their reveal buttons even while hidden.
    // Showing a transient HUD must not move another overlay out of its way.
    QList<QWidget*> reservedOverlays() const;
signals:
    void reservedGeometryChanged();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void place();
    void reveal(int side);
    void schedule(int side);
    bool held(int side) const;
    int sideOf(QWidget* widget) const;
    QWidget* m_viewport = nullptr;
    QFrame* m_panels[2]{};
    QToolButton* m_reveal[2]{};
    QToolButton* m_zoom = nullptr;
    QMenu* m_viewMenu = nullptr;
    QTimer m_timers[2];
    QTimer m_placeTimer;
    QSet<int> m_touchHeld;
    int m_delaySeconds = 3;
    bool m_placing = false;
};
}
