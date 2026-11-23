// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "InkCanvas.h"
#include "ToolPreferences.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <array>
#include <optional>

namespace propernotes {

inline constexpr auto ShapeTools = std::array{InkCanvas::Tool::Line, InkCanvas::Tool::Arrow,
                                              InkCanvas::Tool::Rectangle, InkCanvas::Tool::Ellipse};

// Only applicable tool fields are captured/serialized/applied. The remaining
// ToolPreferences fields are defaults, never input-routing or reading presets.
struct FavoriteTool {
    QUuid id = QUuid::createUuid();
    QString name;
    InkCanvas::Tool tool = InkCanvas::Tool::Pen;
    QColor color = QColor(QStringLiteral("#202833"));
    qreal widthPt = 2.5;
    ToolPreferences tools;
    std::optional<TextMarkupKind> markupKind;
    bool reviewSingleUse = false;
};

struct FavoritesBarPreferences {
    bool visible = true;
    bool collapsed = false;
    QString side = QStringLiteral("right");
    bool floating = true;
    // Last floating geometry, viewport-relative since v10. Docking never
    // overwrites it with the sidebar's full-height layout rectangle.
    QRect geometry = QRect(100000, 72, 80, 420);
    bool pinned = true;
};

FavoriteTool captureFavorite(InkCanvas::Tool tool, const ToolPreferences& tools,
                             const QColor& color, qreal widthPt, const QString& name = {});
FavoriteTool captureReviewFavorite(TextMarkupKind kind, const QColor& color,
                                   const QString& name = {}, bool singleUse = false);
QString textMarkupKindName(TextMarkupKind kind);
// Merge into a runtime snapshot. Calling this never mutates any other preset.
void applyFavorite(const FavoriteTool& favorite, ToolPreferences& tools, QColor& color, qreal& widthPt);
QString favoriteToolName(InkCanvas::Tool tool);
QString favoriteDescription(const FavoriteTool& favorite);
QVector<FavoriteTool> defaultFavoriteTools();
bool validFavoriteTools(const QVector<FavoriteTool>& favorites, QString* error = nullptr);
QJsonArray favoriteToolsJson(const QVector<FavoriteTool>& favorites);
bool readFavoriteTools(const QJsonArray& array, QVector<FavoriteTool>& favorites, int settingsVersion);
bool validFavoritesBarPreferences(const FavoritesBarPreferences& preferences);
QJsonObject favoritesBarJson(const FavoritesBarPreferences& preferences);
bool readFavoritesBar(const QJsonObject& object, FavoritesBarPreferences& preferences, int settingsVersion);

} // namespace propernotes
Q_DECLARE_METATYPE(propernotes::FavoriteTool)
Q_DECLARE_METATYPE(propernotes::FavoritesBarPreferences)
