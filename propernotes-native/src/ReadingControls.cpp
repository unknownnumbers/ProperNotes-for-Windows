// SPDX-License-Identifier: GPL-3.0-only
#include "ReadingControls.h"
#include "ToolIcon.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>
#include <algorithm>

namespace propernotes {
ReadingControls::ReadingControls(QWidget* viewport, const QList<QAction*>& history,
                                 QAction* zoomOut, QAction* zoomIn, QMenu* viewMenu, QObject* parent)
    : QObject(parent), m_viewport(viewport), m_viewMenu(viewMenu)
{
    for (int side = 0; side < 2; ++side) {
        m_panels[side] = new QFrame(viewport);
        m_panels[side]->setObjectName(QStringLiteral("readingHud"));
        m_panels[side]->setAutoFillBackground(true);
        m_panels[side]->setAttribute(Qt::WA_AcceptTouchEvents);
        auto* row = new QHBoxLayout(m_panels[side]);
        row->setContentsMargins(5, 5, 5, 5); row->setSpacing(2);
        m_reveal[side] = new QToolButton(viewport);
        configureReaderIconButton(m_reveal[side]);
        m_reveal[side]->setToolTip(side == 0 ? QStringLiteral("视角历史与撤销重做") : QStringLiteral("缩放与视图"));
        m_reveal[side]->setAccessibleName(m_reveal[side]->toolTip());
        connect(m_reveal[side], &QToolButton::clicked, this, [this, side] {
            const bool keyboardFocus = m_reveal[side]->hasFocus();
            reveal(side);
            if (keyboardFocus) for (auto* button : m_panels[side]->findChildren<QToolButton*>()) {
                if (button->isEnabled()) {button->setFocus(Qt::TabFocusReason);break;}
            }
        });
        m_timers[side].setSingleShot(true);
        connect(&m_timers[side], &QTimer::timeout, this, [this, side] {
            if (held(side)) { schedule(side); return; }
            m_panels[side]->hide(); m_reveal[side]->show();
        });
        m_panels[side]->hide();
    }
    auto add = [](QFrame* panel, QAction* action) {
        auto* button = new QToolButton(panel);
        button->setDefaultAction(action);configureReaderIconButton(button);
        button->setAccessibleName(action->text());
        panel->layout()->addWidget(button);
    };
    for (auto* action : history) add(m_panels[0], action);
    add(m_panels[1], zoomOut);
    m_zoom = new QToolButton(m_panels[1]);
    m_zoom->setMinimumSize(72, 44); m_zoom->setPopupMode(QToolButton::InstantPopup);
    m_zoom->setStyleSheet(QStringLiteral("QToolButton{padding:0 8px;border:0;border-radius:7px;}QToolButton::menu-indicator{width:0;height:0;}"));
    m_zoom->setMenu(viewMenu); m_zoom->setAccessibleName(QStringLiteral("缩放倍率与页面视图"));
    m_panels[1]->layout()->addWidget(m_zoom); add(m_panels[1], zoomIn);
    connect(viewMenu, &QMenu::aboutToShow, this, [this] { reveal(1); });
    connect(viewMenu, &QMenu::aboutToHide, this, [this] { schedule(1); });
    qApp->installEventFilter(this);
    connect(viewport, &QObject::destroyed, this, [this] {
        qApp->removeEventFilter(this);m_timers[0].stop();m_timers[1].stop();m_viewport=nullptr;
    });
    refreshAppearance(); place();
}
void ReadingControls::setHideDelaySeconds(int seconds)
{
    const int delay = std::clamp(seconds, 1, 30);
    if (delay == m_delaySeconds) return;
    m_delaySeconds = delay;
    for (int side = 0; side < 2; ++side) if (m_panels[side]->isVisible()) schedule(side);
}
void ReadingControls::setZoom(qreal zoom) { m_zoom->setText(QString::number(zoom * 100, 'f', zoom < .1 ? 2 : 0) + QLatin1Char('%')); }
void ReadingControls::revealHistory() { reveal(0); }
void ReadingControls::revealZoom() { reveal(1); }
void ReadingControls::refreshAppearance()
{
    for (auto* button : m_reveal) button->setIcon(uiIcon(QStringLiteral("chevron-up"), m_viewport->palette()));
    // QPalette follows the native theme, including high contrast.
    for (auto* panel : m_panels) panel->setStyleSheet(QStringLiteral("QFrame#readingHud {border:1px solid palette(mid);border-radius:12px;background:palette(window);}"));
}
void ReadingControls::place()
{
    const bool narrow = m_viewport->width() < 420;
    for (int side = 0; side < 2; ++side) {
        auto* panel = m_panels[side]; panel->adjustSize();
        const int y = std::max(0, m_viewport->height() - panel->height() - (side == 0 && narrow ? 78 : 16));
        const int x = side == 0 ? 12 : std::max(0, m_viewport->width() - panel->width() - 12);
        panel->move(x, y); panel->raise();
        m_reveal[side]->move(side == 0 ? 12 : std::max(0, m_viewport->width() - 56), y + (panel->height() - 44) / 2);
        m_reveal[side]->raise();
    }
}
void ReadingControls::reveal(int side)
{
    m_reveal[side]->hide(); m_panels[side]->show(); place(); schedule(side);
}
void ReadingControls::schedule(int side) { m_timers[side].start(m_delaySeconds * 1000); }
bool ReadingControls::held(int side) const
{
    const auto* focus = QApplication::focusWidget();
    return m_panels[side]->underMouse() || (focus && (focus == m_panels[side] || m_panels[side]->isAncestorOf(focus)))
        || m_touchHeld.contains(side) || QApplication::mouseButtons() != Qt::NoButton
        || (side == 1 && m_viewMenu->isVisible());
}
int ReadingControls::sideOf(QWidget* widget) const
{
    if (!widget) return -1;
    for (int side = 0; side < 2; ++side)
        if (widget == m_panels[side] || m_panels[side]->isAncestorOf(widget) || widget == m_reveal[side]) return side;
    return -1;
}
bool ReadingControls::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_viewport) return false;
    // Ignored touch sequences may become synthesized mouse input. Release any
    // retained contact on either form of completion, even outside the panel.
    if (event->type()==QEvent::TouchEnd || event->type()==QEvent::TouchCancel || event->type()==QEvent::MouseButtonRelease
        || event->type()==QEvent::TabletRelease || event->type()==QEvent::WindowDeactivate) {
        m_touchHeld.clear();
        for(int i=0;i<2;++i) if(m_panels[i]->isVisible()) schedule(i);
    }
    if (watched == m_viewport && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) place();
    auto* widget = qobject_cast<QWidget*>(watched);
    const int side = sideOf(widget);
    if (side >= 0) {
        switch (event->type()) {
        case QEvent::Enter: case QEvent::Wheel:
            reveal(side); break;
        case QEvent::FocusIn:
            if (widget != m_reveal[side]) reveal(side);
            break;
        case QEvent::TouchBegin: m_touchHeld.insert(side); reveal(side); break;
        case QEvent::TouchEnd: case QEvent::TouchCancel: m_touchHeld.remove(side); schedule(side); break;
        case QEvent::Leave: case QEvent::FocusOut: case QEvent::MouseButtonRelease: schedule(side); break;
        default: break;
        }
    }
    return QObject::eventFilter(watched, event);
}
}
