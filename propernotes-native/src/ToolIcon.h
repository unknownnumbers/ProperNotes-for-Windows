// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FavoriteTools.h"
#include <QIcon>
#include <QPalette>

namespace propernotes {
// Original vector drawings, rendered at the device-independent icon size.
QIcon uiIcon(const QString& name, const QPalette& palette, const QColor& color = {});
QIcon favoriteIcon(const FavoriteTool& favorite, const QPalette& palette);
}
