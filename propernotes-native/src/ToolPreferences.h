// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <cmath>

namespace propernotes {
inline constexpr qreal MinHighlighterWidthPt = 2;
inline constexpr qreal MaxHighlighterWidthPt = 48;
inline constexpr int MinHighlighterOpacity = 1;
inline constexpr int MaxHighlighterOpacity = 100;

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
    return {{"inputFollowsTool",v.inputFollowsTool},{"pressureEnabled",v.pressureEnabled},
        {"pressureSensitivity",v.pressureSensitivity},{"opacity",v.opacity},{"eraserMode",v.eraserMode},
        {"eraserRadiusPt",v.eraserRadiusPt},{"penSingleUse",v.penSingleUse},
        {"highlighterColor",v.highlighterColor.name(QColor::HexRgb)},{"highlighterWidthPt",v.highlighterWidthPt},
        {"highlighterOpacity",v.highlighterOpacity},{"highlighterSingleUse",v.highlighterSingleUse},
        {"eraserSingleUse",v.eraserSingleUse},{"textSingleUse",v.textSingleUse},
        {"textFamily",v.textFamily},{"textSizePt",v.textSizePt},{"layout",v.layout},
        {"thumbnailSize",v.thumbnailSize}};
}
inline bool readToolPreferences(const QJsonObject& o, ToolPreferences& result, bool hasHighlighterStyle) {
    const auto keys = o.keys();
    auto expected = toolPreferencesJson(ToolPreferences{}).keys();
    if (!hasHighlighterStyle) {
        for (const char* key : {"highlighterColor", "highlighterWidthPt", "highlighterOpacity", "highlighterSingleUse"})
            expected.removeAll(QLatin1String(key));
    }
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
    if (!validToolPreferences(v)) return false;
    result=v; return true;
}
} // namespace propernotes
