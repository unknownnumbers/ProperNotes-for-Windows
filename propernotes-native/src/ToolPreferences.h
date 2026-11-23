// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "InkData.h"
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <array>
#include <cmath>

namespace propernotes {
inline constexpr qreal MinHighlighterWidthPt = 2;
inline constexpr qreal MaxHighlighterWidthPt = 48;
inline constexpr int MinHighlighterOpacity = 1;
inline constexpr int MaxHighlighterOpacity = 100;
inline constexpr qreal MinShapeToolWidthPt = 0.5;
inline constexpr qreal MaxShapeToolWidthPt = 24;
inline constexpr int MinShapeToolOpacityPercent = 1;
inline constexpr int DefaultShapeFillOpacityPercent = 25;
inline constexpr auto ShapeToolKinds = std::array{ShapeKind::Line, ShapeKind::Arrow, ShapeKind::Rectangle, ShapeKind::Ellipse};

struct ShapeToolPreference {
    ShapeStyle style;
    bool singleUse = false;
};

inline QString shapeToolPreferenceToken(ShapeKind kind)
{
    switch (kind) {
    case ShapeKind::Line: return QStringLiteral("line");
    case ShapeKind::Arrow: return QStringLiteral("arrow");
    case ShapeKind::Rectangle: return QStringLiteral("rectangle");
    case ShapeKind::Ellipse: return QStringLiteral("ellipse");
    }
    return {};
}

inline bool validShapeToolPreference(ShapeKind kind, const ShapeToolPreference& preference)
{
    const auto& style = preference.style;
    return style.strokeColor.isValid() && style.strokeColor.alpha() > 0 && style.fillColor.isValid()
        && (isAreaShape(kind) || style.fillColor.alpha() == 0) && std::isfinite(style.widthPt)
        && style.widthPt >= MinShapeToolWidthPt && style.widthPt <= MaxShapeToolWidthPt;
}

inline QJsonObject shapeToolPreferenceJson(const ShapeToolPreference& preference)
{
    return {{"stroke", preference.style.strokeColor.name(QColor::HexArgb)},
            {"widthPt", preference.style.widthPt}, {"fill", preference.style.fillColor.name(QColor::HexArgb)},
            {"singleUse", preference.singleUse}};
}

inline bool readShapeToolPreference(const QJsonObject& object, ShapeKind kind, ShapeToolPreference& result)
{
    if (object.size() != 4 || !object.value("stroke").isString() || !object.value("widthPt").isDouble()
        || !object.value("fill").isString() || !object.value("singleUse").isBool()) return false;
    const auto parseColor = [](const QString& token) {
        if (token.size() != 9 || !token.startsWith(QLatin1Char('#'))) return QColor();
        const QColor result(token);
        return result.name(QColor::HexArgb).compare(token, Qt::CaseInsensitive) == 0 ? result : QColor();
    };
    ShapeToolPreference value;
    value.style = { parseColor(object.value("stroke").toString()), object.value("widthPt").toDouble(),
                    parseColor(object.value("fill").toString()) };
    value.singleUse = object.value("singleUse").toBool();
    if (!validShapeToolPreference(kind, value)) return false;
    result = value;
    return true;
}

struct ToolPreferences {
    bool inputFollowsTool = false;
    bool pressureEnabled = true;
    int pressureSensitivity = 50;
    qreal opacity = 1;
    QString eraserMode = QStringLiteral("whole");
    qreal eraserRadiusPt = 8;
    bool penSingleUse = false;
    QColor highlighterColor{255, 221, 67};
    qreal highlighterWidthPt = 14;
    int highlighterOpacity = 32;
    bool highlighterSingleUse = false;
    bool eraserSingleUse = false;
    bool textSingleUse = false;
    QString textFamily = QStringLiteral("Segoe UI");
    qreal textSizePt = 12;
    QString layout = QStringLiteral("single-continuous");
    int thumbnailSize = 180;
    std::array<ShapeToolPreference, ShapeToolKinds.size()> shapeTools{};

    ShapeToolPreference& shapeToolPreference(ShapeKind kind) { return shapeTools[static_cast<size_t>(kind)]; }
    const ShapeToolPreference& shapeToolPreference(ShapeKind kind) const { return shapeTools[static_cast<size_t>(kind)]; }
};
inline bool validHighlighterStyle(const QColor& color, qreal widthPt, int opacity)
{
    return color.isValid() && color.alpha() == 255 && std::isfinite(widthPt)
        && widthPt >= MinHighlighterWidthPt && widthPt <= MaxHighlighterWidthPt
        && opacity >= MinHighlighterOpacity && opacity <= MaxHighlighterOpacity;
}

inline bool validToolPreferences(const ToolPreferences& v) {
    for (qsizetype i=0;i<v.textFamily.size();++i) {
        const QChar c=v.textFamily.at(i);
        if(c.isNull() || c.category()==QChar::Other_Control) return false;
        if(c.isHighSurrogate()) {
            if(i+1>=v.textFamily.size() || !v.textFamily.at(i+1).isLowSurrogate()) return false;
            ++i;
        } else if(c.isLowSurrogate()) return false;
    }
    for (const auto kind : ShapeToolKinds)
        if (!validShapeToolPreference(kind, v.shapeToolPreference(kind))) return false;
    return v.pressureSensitivity >= 0 && v.pressureSensitivity <= 100 &&
        std::isfinite(v.opacity) && v.opacity >= 0 && v.opacity <= 1 &&
        validHighlighterStyle(v.highlighterColor, v.highlighterWidthPt, v.highlighterOpacity) &&
        (v.eraserMode == QStringLiteral("whole") || v.eraserMode == QStringLiteral("partial")) &&
        std::isfinite(v.eraserRadiusPt) && v.eraserRadiusPt >= 1 && v.eraserRadiusPt <= 100 &&
        !v.textFamily.trimmed().isEmpty() && v.textFamily.size() <= 160 &&
        !v.textFamily.contains(QChar('\n')) && !v.textFamily.contains(QChar('\r')) &&
        std::isfinite(v.textSizePt) && v.textSizePt >= 4 && v.textSizePt <= 144 &&
        QStringList{QStringLiteral("single"),QStringLiteral("facing"),QStringLiteral("single-continuous"),QStringLiteral("facing-continuous")}.contains(v.layout) &&
        v.thumbnailSize >= 80 && v.thumbnailSize <= 280;
}
inline QJsonObject toolPreferencesJson(const ToolPreferences& v) {
    QJsonObject shapes;
    for (const auto kind : ShapeToolKinds)
        shapes.insert(shapeToolPreferenceToken(kind), shapeToolPreferenceJson(v.shapeToolPreference(kind)));
    return {{"inputFollowsTool",v.inputFollowsTool},{"pressureEnabled",v.pressureEnabled},
        {"pressureSensitivity",v.pressureSensitivity},{"opacity",v.opacity},{"eraserMode",v.eraserMode},
        {"eraserRadiusPt",v.eraserRadiusPt},{"penSingleUse",v.penSingleUse},
        {"highlighterColor",v.highlighterColor.name(QColor::HexRgb)},{"highlighterWidthPt",v.highlighterWidthPt},
        {"highlighterOpacity",v.highlighterOpacity},{"highlighterSingleUse",v.highlighterSingleUse},
        {"eraserSingleUse",v.eraserSingleUse},{"textSingleUse",v.textSingleUse},
        {"textFamily",v.textFamily},{"textSizePt",v.textSizePt},{"layout",v.layout},
        {"thumbnailSize",v.thumbnailSize},{"shapes",shapes}};
}
inline bool readToolPreferences(const QJsonObject& o, ToolPreferences& result, int settingsVersion) {
    const bool hasHighlighterStyle = settingsVersion >= 5;
    const bool hasShapes = settingsVersion >= 9;
    const auto keys = o.keys();
    auto expected = toolPreferencesJson(ToolPreferences{}).keys();
    if (!hasHighlighterStyle) {
        for (const char* key : {"highlighterColor", "highlighterWidthPt", "highlighterOpacity", "highlighterSingleUse"})
            expected.removeAll(QLatin1String(key));
    }
    if (!hasShapes) expected.removeAll(QStringLiteral("shapes"));
    if (QSet<QString>(keys.begin(),keys.end()) != QSet<QString>(expected.begin(),expected.end())) return false;
    for (const auto& key : {"inputFollowsTool","pressureEnabled","penSingleUse","eraserSingleUse","textSingleUse"})
        if (!o.value(QLatin1String(key)).isBool()) return false;
    for (const auto& key : {"pressureSensitivity","opacity","eraserRadiusPt","textSizePt","thumbnailSize"})
        if (!o.value(QLatin1String(key)).isDouble() || !std::isfinite(o.value(QLatin1String(key)).toDouble())) return false;
    for (const auto& key : {"eraserMode","textFamily","layout"}) if (!o.value(QLatin1String(key)).isString()) return false;
    const qreal sensitivity=o.value("pressureSensitivity").toDouble(), thumb=o.value("thumbnailSize").toDouble();
    if (sensitivity != std::floor(sensitivity) || thumb != std::floor(thumb) || sensitivity<0 || sensitivity>100 || thumb<80 || thumb>280) return false;
    ToolPreferences v;
    v.inputFollowsTool=o.value("inputFollowsTool").toBool(); v.pressureEnabled=o.value("pressureEnabled").toBool();
    v.pressureSensitivity=int(sensitivity); v.opacity=o.value("opacity").toDouble();
    v.eraserMode=o.value("eraserMode").toString(); v.eraserRadiusPt=o.value("eraserRadiusPt").toDouble();
    v.penSingleUse=o.value("penSingleUse").toBool(); v.eraserSingleUse=o.value("eraserSingleUse").toBool();
    v.textSingleUse=o.value("textSingleUse").toBool(); v.textFamily=o.value("textFamily").toString();
    v.textSizePt=o.value("textSizePt").toDouble(); v.layout=o.value("layout").toString(); v.thumbnailSize=int(thumb);
    if (hasHighlighterStyle) {
        const auto color = o.value("highlighterColor");
        const auto width = o.value("highlighterWidthPt");
        const auto opacity = o.value("highlighterOpacity");
        const auto singleUse = o.value("highlighterSingleUse");
        const QString rgb = color.toString();
        const qreal percent = opacity.toDouble();
        if (!color.isString() || rgb.size() != 7 || !rgb.startsWith(QLatin1Char('#')) || !width.isDouble()
            || !opacity.isDouble() || !std::isfinite(percent) || percent != std::floor(percent)
            || percent < MinHighlighterOpacity || percent > MaxHighlighterOpacity || !singleUse.isBool())
            return false;
        v.highlighterColor = QColor(rgb);
        v.highlighterWidthPt = width.toDouble();
        v.highlighterOpacity = int(percent);
        v.highlighterSingleUse = singleUse.toBool();
    }
    if (hasShapes) {
        const auto shapes = o.value("shapes").toObject();
        if (shapes.size() != qsizetype(ShapeToolKinds.size())) return false;
        for (const auto kind : ShapeToolKinds) {
            if (!readShapeToolPreference(shapes.value(shapeToolPreferenceToken(kind)).toObject(), kind,
                                         v.shapeToolPreference(kind))) return false;
        }
    }
    if (!validToolPreferences(v)) return false;
    result=v; return true;
}
} // namespace propernotes
