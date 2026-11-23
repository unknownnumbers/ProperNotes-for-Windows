// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FavoriteTools.h"
#include <QIcon>
#include <QPalette>

class QToolButton;

namespace propernotes {
inline constexpr int ReaderGlyphSize=24;
inline constexpr int ReaderButtonSize=44;
// SVG geometry is rendered at the requested size and screen pixel ratio.
QIcon uiIcon(const QString& name, const QPalette& palette, const QColor& color = {});
QIcon favoriteIcon(const FavoriteTool& favorite, const QPalette& palette);
void configureReaderIconButton(QToolButton* button);
void configureFavoriteButton(QToolButton* button,const FavoriteTool& favorite);
}
