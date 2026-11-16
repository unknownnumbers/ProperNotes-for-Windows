// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "InkCanvas.h"
#include "ToolPreferences.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>

namespace propernotes {

// Only applicable tool fields are captured/serialized/applied. The remaining
// ToolPreferences fields are defaults, never input-routing or reading presets.
struct FavoriteTool {
    QUuid id = QUuid::createUuid();
    QString name;
    InkCanvas::Tool tool = InkCanvas::Tool::Pen;
    QColor color = QColor(QStringLiteral("#202833"));
    qreal widthPt = 2.5;
    ToolPreferences tools;
};

struct FavoritesBarPreferences {
    bool visible = true;
    bool collapsed = false;
    QString side = QStringLiteral("left");
    bool floating = false;
    QRect geometry = QRect(100, 100, 160, 480);
};

FavoriteTool captureFavorite(InkCanvas::Tool tool, const ToolPreferences& tools,
                             const QColor& color, qreal widthPt, const QString& name = {});
// Merge into a runtime snapshot. Calling this never mutates any other preset.
void applyFavorite(const FavoriteTool& favorite, ToolPreferences& tools, QColor& color, qreal& widthPt);
QString favoriteToolName(InkCanvas::Tool tool);
QString favoriteDescription(const FavoriteTool& favorite);
QVector<FavoriteTool> defaultFavoriteTools();
bool validFavoriteTools(const QVector<FavoriteTool>& favorites, QString* error = nullptr);
QJsonArray favoriteToolsJson(const QVector<FavoriteTool>& favorites);
bool readFavoriteTools(const QJsonArray& array, QVector<FavoriteTool>& favorites);
bool validFavoritesBarPreferences(const FavoritesBarPreferences& preferences);
QJsonObject favoritesBarJson(const FavoritesBarPreferences& preferences);
bool readFavoritesBar(const QJsonObject& object, FavoritesBarPreferences& preferences);

} // namespace propernotes
Q_DECLARE_METATYPE(propernotes::FavoriteTool)
Q_DECLARE_METATYPE(propernotes::FavoritesBarPreferences)
