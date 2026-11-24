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
    QString orientation = QStringLiteral("vertical");
    QString side = QStringLiteral("right");
    bool floating = true;
    // Legacy position fallback and desired floating size. Docking and a small
    // viewport never replace the desired height with a clipped layout height.
    QRect geometry = QRect(100000, 72, 80, 420);
    // Floating placement is independent of docking side. Before migration the
    // viewport resolves geometry's old pixel position into an edge and ratio.
    QString floatingAnchor = QStringLiteral("right");
    int floatingEdgeOffset = 8;
    qreal floatingVerticalRatio = 0.0;
    bool floatingPositionInitialized = false;
    // Horizontal placement has its own desired width and top/bottom anchor.
    // Switching orientation must not overwrite the other orientation's memory.
    QRect horizontalGeometry = QRect(72, 84, 420, 80);
    QString horizontalAnchor = QStringLiteral("top");
    int horizontalEdgeOffset = 84;
    qreal horizontalRatio = 0.5;
    bool horizontalPositionInitialized = true;
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
