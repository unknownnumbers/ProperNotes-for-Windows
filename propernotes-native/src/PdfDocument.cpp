// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"

#include <pdfannotation.h>
#include <pdfcatalog.h>
#include <pdfcms.h>
#include <pdfcolorspaces.h>
#include <pdfdocument.h>
#include <pdfdocumentbuilder.h>
#include <pdfdocumentreader.h>
#include <pdfdocumentwriter.h>
#include <pdfexception.h>
#include <pdffont.h>
#include <pdfmeshqualitysettings.h>
#include <pdfobject.h>
#include <pdfobjectutils.h>
#include <pdfoptionalcontent.h>
#include <pdfoptimizer.h>
#include <pdfpage.h>
#include <pdfrenderer.h>
#include <pdftextlayout.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

namespace propernotes {
namespace {
constexpr auto NativeKey = "ProperNotesNativeInk";
constexpr auto OwnerName = "ProperNotesNative";
constexpr auto IdPrefix = "ProperNotes.NativeInk/";
constexpr auto TextIdPrefix = "ProperNotes.NativeText/";
constexpr auto MarkupIdPrefix = "ProperNotes.NativeMarkup/";
constexpr int InkFormatVersion = 1; // Its pressure/outline interpretation never changes.
constexpr int TextFormatVersion = 2;
constexpr int MarkupFormatVersion = 3;
constexpr int HighlighterFormatVersion = 4;
constexpr qint64 MaxFileBytes = 512LL * 1024 * 1024;
constexpr qsizetype MaxMetadataBytes = 64 * 1024 * 1024;
constexpr qsizetype MaxTotalPoints = 2'000'000;
constexpr qsizetype MaxStrokePoints = 200'000;
constexpr qsizetype MaxStrokes = 100'000;
constexpr qsizetype MaxTextBoxes = 10'000;
constexpr qsizetype MaxTextLength = 100'000;
constexpr qsizetype MaxTotalTextLength = 2'000'000;
constexpr int MaxTextImageSide = 4096;
constexpr qint64 MaxTextImagePixels = 8'000'000;
// PDF4QT serializes PDF real objects rounded to five fractional digits.
// Private JSON and appearance content are compared independently at full precision.
constexpr double PdfRealWriteTolerance = 5.1e-6;

using Object = pdf::PDFObject;
using Dictionary = pdf::PDFDictionary;
using DictBuilder = pdf::PDFDictionaryBuilder;
using Reference = pdf::PDFObjectReference;

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

void put(DictBuilder& dictionary, const char* key, Object value)
{
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString(key), std::move(value));
}

Object arrayOf(std::initializer_list<double> values)
{
    pdf::PDFArrayBuilder array;
    for (double value : values)
        array.appendItem(Object::createReal(value));
    return Object::createArray(std::move(array));
}

Object streamObject(DictBuilder dictionary, QByteArray content)
{
    put(dictionary, "Length", Object::createInteger(content.size()));
    return Object::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
}

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(file.errorString());
    if (file.size() < 1 || file.size() > MaxFileBytes)
        fail(QStringLiteral("PDF 大小超出此原型支持的范围, 或文件为空."));
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || bytes.size() != file.size())
        fail(QStringLiteral("PDF 未完整读入, 未修改当前文档."));
    return bytes;
}

QString normalizedPath(const QString& path)
{
    QFileInfo info(path);
    if (info.exists())
        return info.canonicalFilePath();
    const QString directory = info.dir().canonicalPath();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(info.fileName());
}

bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

pdf::PDFDocument readPdf(const QByteArray& bytes)
{
    pdf::PDFDocumentReader reader(nullptr, [](bool* accepted) {
        if (accepted)
            *accepted = false;
        return QString();
    }, false, false);
    pdf::PDFDocument result = reader.readFromBuffer(bytes);
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        fail(reader.getErrorMessage().isEmpty()
                 ? QStringLiteral("无法读取此 PDF, 密码文件暂不受支持.")
                 : reader.getErrorMessage());
    if (!result.getCatalog() || result.getCatalog()->getPageCount() == 0)
        fail(QStringLiteral("PDF 没有可显示的页面."));
    if (result.getCatalog()->getPageCount() > size_t(std::numeric_limits<int>::max()))
        fail(QStringLiteral("PDF 页数超出支持范围."));
    return result;
}

const Dictionary* catalogDictionary(const pdf::PDFDocument& document)
{
    const auto* trailer = document.getTrailerDictionary();
    return trailer ? document.getDictionaryFromObject(trailer->get("Root")) : nullptr;
}

QByteArray stringValue(const pdf::PDFDocument& document, const Object& object)
{
    const auto& value = document.getObject(object);
    return value.isString() || value.isName() ? value.getString() : QByteArray();
}

double numberValue(const pdf::PDFDocument& document, const Object& object)
{
    const auto& value = document.getObject(object);
    if (value.isInt())
        return double(value.getInteger());
    if (value.isReal())
        return value.getReal();
    return std::numeric_limits<double>::quiet_NaN();
}

bool hasOnlyKeys(const Dictionary& dictionary, std::initializer_list<const char*> allowed)
{
    for (const auto& entry : dictionary) {
        if (std::none_of(allowed.begin(), allowed.end(),
                [&](const char* key) { return entry.first == key; }))
            return false;
    }
    return true;
}

bool finiteCoordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1e8;
}

bool supportedEditVersion(double version)
{
    return version == InkFormatVersion || version == TextFormatVersion
        || version == MarkupFormatVersion || version == HighlighterFormatVersion;
}

int strokeFormatVersion(const InkStroke& stroke)
{
    return stroke.kind == InkStrokeKind::Highlighter ? HighlighterFormatVersion : InkFormatVersion;
}

void validateStroke(const InkStroke& stroke)
{
    if (stroke.id.isNull() || !stroke.color.isValid()
        || (stroke.kind != InkStrokeKind::Pen && stroke.kind != InkStrokeKind::Highlighter)
        || !std::isfinite(stroke.widthPt) || stroke.widthPt < 0.05 || stroke.widthPt > 1000
        || stroke.points.isEmpty() || stroke.points.size() > MaxStrokePoints)
        fail(QStringLiteral("笔迹包含无效 ID, 颜色, 笔宽或采样数量."));
    for (const auto& point : stroke.points) {
        if (!finiteCoordinate(point.position.x()) || !finiteCoordinate(point.position.y())
            || !std::isfinite(point.pressure) || point.pressure < 0 || point.pressure > 1)
            fail(QStringLiteral("笔迹包含无效坐标或压力值."));
    }
}

QJsonObject strokeJson(const InkStroke& stroke)
{
    QJsonArray points;
    for (const auto& point : stroke.points)
        points.append(QJsonArray { point.position.x(), point.position.y(), point.pressure });
    QJsonObject result { { "id", stroke.id.toString(QUuid::WithoutBraces) },
        { "color", stroke.color.name(QColor::HexArgb) },
        { "widthPt", stroke.widthPt }, { "points", points } };
    // Keep ordinary pen JSON (and therefore its existing ownership digest)
    // identical for documents from all previous native versions.
    if (stroke.kind == InkStrokeKind::Highlighter)
        result.insert("kind", QStringLiteral("highlighter"));
    return result;
}

QByteArray strokeDigest(const InkStroke& stroke)
{
    return sha256(QJsonDocument(strokeJson(stroke)).toJson(QJsonDocument::Compact)).toHex();
}

void validateTextBox(const TextBox& box)
{
    if (box.id.isNull() || !box.color.isValid() || !box.rect.isValid()
        || !finiteCoordinate(box.rect.left()) || !finiteCoordinate(box.rect.top())
        || !finiteCoordinate(box.rect.right()) || !finiteCoordinate(box.rect.bottom())
        || box.rect.width() < 0.1 || box.rect.height() < 0.1
        || box.rect.width() > 100'000 || box.rect.height() > 100'000
        || !std::isfinite(box.fontSizePt) || box.fontSizePt < 1 || box.fontSizePt > 1000
        || box.fontFamily.trimmed().isEmpty() || box.fontFamily.size() > 256
        || box.text.size() > MaxTextLength || box.text.contains(QChar::Null)
        || box.fontFamily.contains(QChar::Null))
        fail(QStringLiteral("文字框包含无效 ID, 范围, 字体, 颜色或文本长度."));
    auto validUnicode = [](const QString& text) {
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text[i].isHighSurrogate()) {
                if (++i >= text.size() || !text[i].isLowSurrogate())
                    return false;
            } else if (text[i].isLowSurrogate()) {
                return false;
            }
        }
        return true;
    };
    if (!validUnicode(box.text) || !validUnicode(box.fontFamily))
        fail(QStringLiteral("文字框包含不完整的 Unicode 字符."));
    for (const QChar character : box.fontFamily) {
        if (character.category() == QChar::Other_Control)
            fail(QStringLiteral("文字框字体名称含控制字符."));
    }
}

QJsonObject textBoxJson(const TextBox& box)
{
    return QJsonObject { { "id", box.id.toString(QUuid::WithoutBraces) },
        { "rect", QJsonArray { box.rect.x(), box.rect.y(), box.rect.width(), box.rect.height() } },
        { "text", box.text }, { "fontFamily", box.fontFamily },
        { "fontSizePt", box.fontSizePt }, { "color", box.color.name(QColor::HexArgb) } };
}

QByteArray textBoxDigest(const TextBox& box)
{
    return sha256(QJsonDocument(textBoxJson(box)).toJson(QJsonDocument::Compact)).toHex();
}

QByteArray markupSubtype(TextMarkupKind kind)
{
    switch (kind) {
    case TextMarkupKind::Highlight: return "Highlight";
    case TextMarkupKind::Underline: return "Underline";
    case TextMarkupKind::StrikeOut: return "StrikeOut";
    case TextMarkupKind::Squiggly: return "Squiggly";
    }
    return {};
}

// Adobe Reader and the pinned PDF4QT use the Z order for QuadPoints.
// The explicit AP remains authoritative for the mark's visual appearance.
std::array<QPointF, 4> markupQuadPoints(const TextMarkupQuad& quad)
{
    return { quad.topLeft, quad.topRight, quad.bottomLeft, quad.bottomRight };
}

QJsonObject textMarkupJson(const TextMarkup& markup)
{
    QJsonArray quads;
    for (const auto& quad : markup.quads) {
        QJsonArray coordinates;
        for (const auto& point : markupQuadPoints(quad)) {
            coordinates.append(point.x());
            coordinates.append(point.y());
        }
        quads.append(coordinates);
    }
    return { { "id", markup.id.toString(QUuid::WithoutBraces) },
        { "kind", QString::fromLatin1(markupSubtype(markup.kind)) },
        { "color", markup.color.name(QColor::HexArgb) }, { "quads", quads } };
}

QByteArray textMarkupDigest(const TextMarkup& markup)
{
    return sha256(QJsonDocument(textMarkupJson(markup)).toJson(QJsonDocument::Compact)).toHex();
}

struct MarkupBudget {
    qsizetype marks = 0;
    qsizetype quads = 0;
    qsizetype pathElements = 0;

    void add(const TextMarkup& markup, QSet<QUuid>& ids)
    {
        if (!validTextMarkup(markup))
            fail(QStringLiteral("正文标记包含无效颜色、坐标或区域数量。"));
        quads += markup.quads.size();
        pathElements += textMarkupPathElements(markup);
        if (ids.contains(markup.id) || ++marks > MaxDocumentTextMarkups
            || quads > MaxDocumentTextMarkupQuads || pathElements > MaxDocumentTextMarkupPathElements)
            fail(QStringLiteral("正文标记 ID 重复，或文档中的标记、区域或绘制点超出数量上限。"));
        ids.insert(markup.id);
    }
};

TextMarkup parseTextMarkup(const QJsonValue& value)
{
    const auto object = value.toObject();
    if (!value.isObject() || object.size() != 4 || !object.value("id").isString()
        || !object.value("kind").isString() || !object.value("color").isString()
        || !object.value("quads").isArray())
        fail(QStringLiteral("原生正文标记字段未知或缺失。"));
    TextMarkup markup;
    markup.id = QUuid(object.value("id").toString());
    const QByteArray subtype = object.value("kind").toString().toLatin1();
    if (subtype == "Highlight")
        markup.kind = TextMarkupKind::Highlight;
    else if (subtype == "Underline")
        markup.kind = TextMarkupKind::Underline;
    else if (subtype == "StrikeOut")
        markup.kind = TextMarkupKind::StrikeOut;
    else if (subtype == "Squiggly")
        markup.kind = TextMarkupKind::Squiggly;
    else
        fail(QStringLiteral("原生正文标记种类未知。"));
    const auto color = object.value("color").toString();
    markup.color = QColor(color);
    if (!markup.color.isValid() || markup.color.name(QColor::HexArgb) != color)
        fail(QStringLiteral("原生正文标记颜色格式无效。"));
    const auto quads = object.value("quads").toArray();
    if (quads.isEmpty() || quads.size() > MaxTextMarkupQuads)
        fail(QStringLiteral("原生正文标记区域数量无效。"));
    markup.quads.reserve(quads.size());
    for (const auto& quad : quads) {
        const auto coordinates = quad.toArray();
        if (coordinates.size() != 8
            || !std::all_of(coordinates.begin(), coordinates.end(), [](const QJsonValue& n) { return n.isDouble(); }))
            fail(QStringLiteral("原生正文标记坐标无效。"));
        auto point = [&coordinates](int i) { return QPointF(coordinates[i].toDouble(), coordinates[i + 1].toDouble()); };
        markup.quads.append({ point(0), point(2), point(4), point(6) });
    }
    return markup;
}

int formatVersionFor(const QVector<PageAnnotations>& pages)
{
    bool hasMarkup = false;
    for (const auto& page : pages) {
        if (std::any_of(page.strokes.cbegin(), page.strokes.cend(), [](const InkStroke& stroke) {
                return stroke.kind == InkStrokeKind::Highlighter;
            }))
            return HighlighterFormatVersion;
        hasMarkup |= !page.textMarkups.isEmpty();
    }
    return hasMarkup ? MarkupFormatVersion : TextFormatVersion;
}

QJsonObject pageJson(const PageAnnotations& page, int version)
{
    QJsonArray strokes;
    for (const auto& stroke : page.strokes)
        strokes.append(strokeJson(stroke));
    QJsonArray textBoxes;
    for (const auto& box : page.textBoxes)
        textBoxes.append(textBoxJson(box));
    QJsonObject result { { "strokes", strokes }, { "textBoxes", textBoxes } };
    if (version >= MarkupFormatVersion) {
        QJsonArray textMarkups;
        for (const auto& markup : page.textMarkups)
            textMarkups.append(textMarkupJson(markup));
        result.insert("textMarkups", textMarkups);
    }
    return result;
}

QByteArray editData(const QVector<PageAnnotations>& pages,
                  const QHash<QUuid, QByteArray>* textAppearanceHashes = nullptr)
{
    const int version = formatVersionFor(pages);
    QJsonArray pageArray;
    for (const auto& page : pages)
        pageArray.append(pageJson(page, version));
    QJsonObject root { { "application", "ProperNotes.Native" },
        { "version", version }, { "pages", pageArray } };
    if (textAppearanceHashes) {
        QJsonObject appearances;
        for (auto it = textAppearanceHashes->cbegin(); it != textAppearanceHashes->cend(); ++it)
            appearances.insert(it.key().toString(QUuid::WithoutBraces), QString::fromLatin1(it.value()));
        root.insert("textAppearances", appearances);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QVector<PageAnnotations> parseEditData(const QByteArray& bytes, int pageCount, int version,
                                     QHash<QUuid, QByteArray>& textAppearanceHashes)
{
    if (bytes.size() > MaxMetadataBytes)
        fail(QStringLiteral("原生编辑数据过大, 为保护原文件仅可阅读."));
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &error);
    const QJsonObject root = json.object();
    if (error.error != QJsonParseError::NoError || !json.isObject()
        || root.value("application").toString() != QStringLiteral("ProperNotes.Native")
        || root.value("version").toDouble(-1) != version
        || !supportedEditVersion(version)
        || !root.value("pages").isArray()
        || root.value("pages").toArray().size() != pageCount)
        fail(QStringLiteral("原生编辑数据损坏或版本不支持, 不允许覆盖保存."));
    QVector<PageAnnotations> result(pageCount);
    QSet<QUuid> ids;
    qsizetype pointCount = 0;
    qsizetype strokeCount = 0;
    qsizetype textCount = 0;
    qsizetype textLength = 0;
    QSet<QUuid> textIds;
    MarkupBudget markupBudget;
    bool hasHighlighter = false;
    const QJsonArray pages = root.value("pages").toArray();
    for (int p = 0; p < pageCount; ++p) {
        QJsonArray strokes;
        QJsonArray textBoxes;
        QJsonArray textMarkups;
        if (version == InkFormatVersion) {
            if (!pages[p].isArray())
                fail(QStringLiteral("原生 v1 页笔迹数据无效."));
            strokes = pages[p].toArray();
        } else {
            if (!pages[p].isObject())
                fail(QStringLiteral("原生页面对象无效."));
            const auto page = pages[p].toObject();
            const bool hasMarkupField = version >= MarkupFormatVersion;
            if (page.size() != (hasMarkupField ? 3 : 2) || !page.value("strokes").isArray()
                || (hasMarkupField && !page.value("textMarkups").isArray())
                || !page.value("textBoxes").isArray())
                fail(QStringLiteral("原生页面字段未知或缺失, 不允许丢弃未知对象."));
            strokes = page.value("strokes").toArray();
            textBoxes = page.value("textBoxes").toArray();
            if (hasMarkupField)
                textMarkups = page.value("textMarkups").toArray();
        }
        for (const auto& value : strokes) {
            if (!value.isObject())
                fail(QStringLiteral("原生笔迹记录无效."));
            const QJsonObject object = value.toObject();
            const bool highlighted = version == HighlighterFormatVersion
                && object.value("kind").toString() == QStringLiteral("highlighter");
            if (object.size() != (highlighted ? 5 : 4) || (!highlighted && object.contains("kind")))
                fail(QStringLiteral("原生笔迹含未知字段, 不允许丢弃未知编辑数据."));
            InkStroke stroke;
            if (highlighted) {
                stroke.kind = InkStrokeKind::Highlighter;
                hasHighlighter = true;
            }
            stroke.id = QUuid(object.value("id").toString());
            stroke.color = QColor(object.value("color").toString());
            stroke.widthPt = object.value("widthPt").toDouble(-1);
            if (!object.value("points").isArray())
                fail(QStringLiteral("原生笔迹采样缺失."));
            for (const auto& sample : object.value("points").toArray()) {
                if (!sample.isArray() || sample.toArray().size() != 3)
                    fail(QStringLiteral("原生压力采样无效."));
                const QJsonArray point = sample.toArray();
                if (!point[0].isDouble() || !point[1].isDouble() || !point[2].isDouble())
                    fail(QStringLiteral("原生压力采样类型无效."));
                stroke.points.append({ QPointF(point[0].toDouble(), point[1].toDouble()),
                                       point[2].toDouble() });
            }
            validateStroke(stroke);
            pointCount += stroke.points.size();
            if (pointCount > MaxTotalPoints || ++strokeCount > MaxStrokes || ids.contains(stroke.id))
                fail(QStringLiteral("原生笔迹超出数量上限或 ID 重复."));
            ids.insert(stroke.id);
            result[p].strokes.append(std::move(stroke));
        }
        for (const auto& value : textBoxes) {
            if (!value.isObject())
                fail(QStringLiteral("原生文字框对象无效."));
            const auto object = value.toObject();
            if (object.size() != 6 || !object.value("id").isString()
                || !object.value("text").isString() || !object.value("fontFamily").isString()
                || !object.value("color").isString() || !object.value("fontSizePt").isDouble()
                || !object.value("rect").isArray())
                fail(QStringLiteral("原生文字框字段未知或缺失."));
            const auto rect = object.value("rect").toArray();
            if (rect.size() != 4 || !rect[0].isDouble() || !rect[1].isDouble()
                || !rect[2].isDouble() || !rect[3].isDouble())
                fail(QStringLiteral("原生文字框坐标无效."));
            TextBox box;
            box.id = QUuid(object.value("id").toString());
            box.rect = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble());
            box.text = object.value("text").toString();
            box.fontFamily = object.value("fontFamily").toString();
            box.fontSizePt = object.value("fontSizePt").toDouble();
            box.color = QColor(object.value("color").toString());
            validateTextBox(box);
            textLength += box.text.size();
            if (++textCount > MaxTextBoxes || textLength > MaxTotalTextLength || ids.contains(box.id))
                fail(QStringLiteral("文字框数量, 文本总长超出范围或对象 ID 重复."));
            ids.insert(box.id);
            textIds.insert(box.id);
            result[p].textBoxes.append(std::move(box));
        }
        for (const auto& value : textMarkups) {
            auto markup = parseTextMarkup(value);
            markupBudget.add(markup, ids);
            result[p].textMarkups.append(std::move(markup));
        }
    }
    if (version == HighlighterFormatVersion && !hasHighlighter)
        fail(QStringLiteral("原生荧光笔数据不完整，不允许丢弃未知批注。"));
    if (version != InkFormatVersion) {
        if (root.size() != 4 || !root.value("textAppearances").isObject())
            fail(QStringLiteral("原生外观索引缺失或含未知数据."));
        const auto appearances = root.value("textAppearances").toObject();
        if (appearances.size() != textIds.size())
            fail(QStringLiteral("原生文字外观索引数量不匹配."));
        for (const auto& id : textIds) {
            const QByteArray digest = appearances.value(id.toString(QUuid::WithoutBraces)).toString().toLatin1();
            if (digest.size() != 64 || QByteArray::fromHex(digest).toHex() != digest)
                fail(QStringLiteral("原生文字外观校验值缺失或无效."));
            textAppearanceHashes.insert(id, digest);
        }
    }
    return result;
}

struct OwnedAnnotation {
    Reference annotation;
    std::optional<Reference> appearance;
    QVector<Reference> resources;
};

struct LoadedState {
    pdf::PDFDocument base;
    QVector<PageAnnotations> pages;
    QHash<QUuid, OwnedAnnotation> owned;
    std::optional<Reference> privateData;
    QHash<QUuid, QByteArray> textAppearanceHashes;
    QString readOnlyReason;
};

QByteArray appearanceContent(const InkStroke& stroke, const QRectF& box);
QByteArray pdfPathNumber(double value);
bool textAnnotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                           const TextBox& box, const QByteArray& expectedAppearanceHash,
                           QVector<Reference>& ownedResources);
bool markupAnnotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                             const TextMarkup& markup, Reference pageReference);

bool annotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                       const InkStroke& stroke, Reference pageReference)
{
    const bool highlighted = stroke.kind == InkStrokeKind::Highlighter;
    if (stringValue(document, annotation.get("Subtype")) != "Ink"
        || numberValue(document, annotation.get("PNVersion")) != strokeFormatVersion(stroke)
        || stringValue(document, annotation.get("NM")) != QByteArray(IdPrefix) + stroke.id.toByteArray(QUuid::WithoutBraces)
        || stringValue(document, annotation.get("PNStrokeSHA256")) != strokeDigest(stroke))
        return false;
    if (highlighted) {
        const auto& page = annotation.get("P");
        const double opacity = numberValue(document, annotation.get("CA"));
        if (stringValue(document, annotation.get("Type")) != "Annot"
            || !page.isReference() || page.getReference() != pageReference
            || numberValue(document, annotation.get("F")) != 4
            || !std::isfinite(opacity) || std::abs(opacity - stroke.color.alphaF()) > PdfRealWriteTolerance
            || !annotation.get("AS").isNull() || !annotation.get("OC").isNull() || !annotation.get("IRT").isNull())
            return false;
    }
    const auto& inkList = document.getObject(annotation.get("InkList"));
    if (!inkList.isArray() || inkList.getArray()->getCount() != 1)
        return false;
    const auto& path = document.getObject(inkList.getArray()->getItem(0));
    if (!path.isArray() || path.getArray()->getCount() != size_t(stroke.points.size()) * 2)
        return false;
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        const double x = numberValue(document, path.getArray()->getItem(size_t(i) * 2));
        const double y = numberValue(document, path.getArray()->getItem(size_t(i) * 2 + 1));
        if (!std::isfinite(x) || !std::isfinite(y)
            || std::abs(x - stroke.points[i].position.x()) > PdfRealWriteTolerance
            || std::abs(y - stroke.points[i].position.y()) > PdfRealWriteTolerance)
            return false;
    }
    const QRectF box = inkStrokeOutline(stroke).boundingRect();
    const auto& rect = document.getObject(annotation.get("Rect"));
    if (!rect.isArray() || rect.getArray()->getCount() != 4)
        return false;
    const double expected[] = { box.left(), box.top(), box.right(), box.bottom() };
    for (size_t i = 0; i < 4; ++i) {
        const double actual = numberValue(document, rect.getArray()->getItem(i));
        if (!std::isfinite(actual) || std::abs(actual - expected[i]) > PdfRealWriteTolerance)
            return false;
    }
    const auto* appearances = document.getDictionaryFromObject(annotation.get("AP"));
    if (!appearances)
        return false;
    if (highlighted && (!hasOnlyKeys(*appearances, { "N" }) || !appearances->get("N").isReference()))
        return false;
    const auto& appearance = document.getObject(appearances->get("N"));
    if (!appearance.isStream())
        return false;
    const auto* form = appearance.getStream()->getDictionary();
    auto matchesArray = [&](const Object& object, std::initializer_list<double> expectedValues) {
        const auto& value = document.getObject(object);
        if (!value.isArray() || value.getArray()->getCount() != expectedValues.size())
            return false;
        size_t index = 0;
        for (double expectedValue : expectedValues) {
            const double actual = numberValue(document, value.getArray()->getItem(index++));
            if (!std::isfinite(actual) || std::abs(actual - expectedValue) > PdfRealWriteTolerance)
                return false;
        }
        return true;
    };
    if (!matchesArray(form->get("BBox"), { 0, 0, box.width(), box.height() })
        || !matchesArray(form->get("Matrix"), { 1, 0, 0, 1, 0, 0 })
        || !matchesArray(annotation.get("C"), { stroke.color.redF(), stroke.color.greenF(), stroke.color.blueF() }))
        return false;
    const auto* resources = document.getDictionaryFromObject(form->get("Resources"));
    const auto* states = resources ? document.getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
    const auto* alpha = states ? document.getDictionaryFromObject(states->get("PNAlpha")) : nullptr;
    if (!alpha)
        return false;
    if (highlighted) {
        if (!hasOnlyKeys(*form, { "Type", "Subtype", "FormType", "BBox", "Matrix", "Resources", "Length" })
            || stringValue(document, form->get("Type")) != "XObject"
            || stringValue(document, form->get("Subtype")) != "Form"
            || numberValue(document, form->get("FormType")) != 1
            || !hasOnlyKeys(*resources, { "ExtGState" }) || !hasOnlyKeys(*states, { "PNAlpha" })
            || !hasOnlyKeys(*alpha, { "Type", "ca", "CA", "BM" })
            || stringValue(document, alpha->get("Type")) != "ExtGState"
            || stringValue(document, alpha->get("BM")) != "Multiply"
            || !matchesArray(annotation.get("Border"), { 0, 0, stroke.widthPt }))
            return false;
    }
    for (const char* key : { "ca", "CA" }) {
        const double actual = numberValue(document, alpha->get(key));
        if (!std::isfinite(actual) || std::abs(actual - stroke.color.alphaF()) > PdfRealWriteTolerance)
            return false;
    }
    if (highlighted)
        return *appearance.getStream()->getContent() == appearanceContent(stroke, box);
    return document.getDecodedStream(appearance.getStream()) == appearanceContent(stroke, box);
}

LoadedState loadState(const QByteArray& bytes)
{
    LoadedState state;
    state.base = readPdf(bytes);
    const int count = int(state.base.getCatalog()->getPageCount());
    state.pages.resize(count);
    const Dictionary* catalog = catalogDictionary(state.base);
    if (!catalog)
        fail(QStringLiteral("PDF Catalog 无效."));
    if (catalog->hasKey("InkPDF"))
        state.readOnlyReason = QStringLiteral("此文件含旧版 InkPDF v1-v4 或未知旧格式. 原生原型仅可阅读, 不覆盖其编辑数据.");
    if (!state.base.getTrailerDictionary()->get("Encrypt").isNull())
        state.readOnlyReason = QStringLiteral("此原型不修改加密 PDF, 可阅读已成功解锁的页面.");
    for (const auto& entry : state.base.getStorage().getObjects()) {
        const auto* dictionary = state.base.getDictionaryFromObject(entry.object);
        if (dictionary && (dictionary->hasKey("ByteRange")
            || stringValue(state.base, dictionary->get("Type")) == "Sig"))
            state.readOnlyReason = QStringLiteral("此 PDF 含数字签名. 原型不重写签名文件.");
    }
    for (int p = 0; p < count; ++p) {
        const auto* page = state.base.getCatalog()->getPage(size_t(p));
        if (!std::isfinite(page->getUserUnit()) || std::abs(page->getUserUnit() - 1.0) > 1e-9)
            state.readOnlyReason = QStringLiteral("此原型仅阅读非默认 UserUnit 页面, 暂不修改其批注.");
        for (const auto& reference : page->getAnnotations()) {
            const auto* annotation = state.base.getDictionaryFromObject(Object::createReference(reference));
            if (annotation && !annotation->get("InkPDFOwned").isNull())
                state.readOnlyReason = QStringLiteral("检测到旧版 InkPDF 批注. 原型仅阅读, 不覆盖旧数据.");
        }
    }
    const auto& native = state.base.getObject(catalog->get(NativeKey));
    QHash<QUuid, Reference> found;
    bool reservedAnnotations = false;
    for (int p = 0; p < count; ++p) {
        const auto* page = state.base.getCatalog()->getPage(size_t(p));
        for (const auto& reference : page->getAnnotations()) {
            const auto* annotation = state.base.getDictionaryFromObject(Object::createReference(reference));
            if (!annotation)
                continue;
            if (stringValue(state.base, annotation->get("PNOwner")) == OwnerName
                || stringValue(state.base, annotation->get("NM")).startsWith(IdPrefix)
                || stringValue(state.base, annotation->get("NM")).startsWith(TextIdPrefix)
                || stringValue(state.base, annotation->get("NM")).startsWith(MarkupIdPrefix))
                reservedAnnotations = true;
        }
    }
    if (native.isNull()) {
        if (reservedAnnotations || catalog->hasKey(NativeKey))
            state.readOnlyReason = QStringLiteral("发现原生批注但缺少编辑数据, 为保护文件仅可阅读.");
        return state;
    }
    if (!state.readOnlyReason.isEmpty())
        return state;
    try {
        if (!native.isDictionary()
            || stringValue(state.base, native.getDictionary()->get("Type")) != NativeKey
            || !hasOnlyKeys(*native.getDictionary(), { "Type", "Version", "Data", "SHA256" }))
            fail(QStringLiteral("原生编辑数据版本未知, 仅可阅读."));
        const double versionValue = numberValue(state.base, native.getDictionary()->get("Version"));
        if (!supportedEditVersion(versionValue))
            fail(QStringLiteral("原生编辑数据版本未知, 仅可阅读."));
        const int version = int(versionValue);
        const auto& data = state.base.getObject(native.getDictionary()->get("Data"));
        if (!data.isStream())
            fail(QStringLiteral("原生编辑数据流缺失, 仅可阅读."));
        // These versions write an unfiltered JSON stream. Refuse transformed or
        // oversized private streams instead of decoding an unbounded payload.
        if (!data.getStream()->getDictionary()->get("Filter").isNull()
            || data.getStream()->getContent()->size() > MaxMetadataBytes)
            fail(QStringLiteral("此原生编辑数据流需要迁移或超出原型范围, 仅可阅读."));
        const QByteArray decoded = state.base.getDecodedStream(data.getStream());
        if (sha256(decoded).toHex() != stringValue(state.base, native.getDictionary()->get("SHA256")))
            fail(QStringLiteral("原生编辑数据校验失败, 仅可阅读."));
        QHash<QUuid, QByteArray> textAppearanceHashes;
        auto pages = parseEditData(decoded, count, version, textAppearanceHashes);
        QSet<QUuid> expectedIds;
        for (int p = 0; p < count; ++p) {
            QHash<QUuid, InkStroke> expected;
            QHash<QUuid, TextBox> expectedText;
            QHash<QUuid, const TextMarkup*> expectedMarkup;
            for (const auto& stroke : pages[p].strokes) {
                expected.insert(stroke.id, stroke);
                expectedIds.insert(stroke.id);
            }
            for (const auto& box : pages[p].textBoxes) {
                expectedText.insert(box.id, box);
                expectedIds.insert(box.id);
            }
            for (const auto& markup : pages[p].textMarkups) {
                expectedMarkup.insert(markup.id, &markup);
                expectedIds.insert(markup.id);
            }
            const auto* page = state.base.getCatalog()->getPage(size_t(p));
            for (const auto& reference : page->getAnnotations()) {
                const auto* annotation = state.base.getDictionaryFromObject(Object::createReference(reference));
                if (!annotation)
                    continue;
                const bool owner = stringValue(state.base, annotation->get("PNOwner")) == OwnerName;
                const QByteArray name = stringValue(state.base, annotation->get("NM"));
                const bool isMarkup = name.startsWith(MarkupIdPrefix);
                const bool reserved = name.startsWith(IdPrefix) || name.startsWith(TextIdPrefix) || isMarkup;
                if (!owner && !reserved)
                    continue;
                const bool isText = stringValue(state.base, annotation->get("Subtype")) == "FreeText";
                const char* key = isMarkup ? "PNMarkupID" : isText ? "PNTextID" : "PNStrokeID";
                const QUuid id(QString::fromUtf8(stringValue(state.base, annotation->get(key))));
                QVector<Reference> resources;
                bool matches = false;
                if (owner && !id.isNull() && !found.contains(id)) {
                    if (isMarkup) {
                        if (const auto* markup = expectedMarkup.value(id, nullptr))
                            matches = markupAnnotationMatches(state.base, *annotation, *markup, page->getPageReference());
                    } else if (isText) {
                        matches = expectedText.contains(id) && textAnnotationMatches(state.base, *annotation,
                            expectedText.value(id), textAppearanceHashes.value(id), resources);
                    } else {
                        matches = expected.contains(id) && annotationMatches(state.base, *annotation,
                            expected.value(id), page->getPageReference());
                    }
                }
                if (!matches)
                    fail(QStringLiteral("原生批注与编辑数据不一致, 仅可阅读, 不覆盖外部修改."));
                found.insert(id, reference);
                OwnedAnnotation owned { reference, std::nullopt, resources };
                const auto* ap = state.base.getDictionaryFromObject(annotation->get("AP"));
                if (ap && ap->get("N").isReference())
                    owned.appearance = ap->get("N").getReference();
                state.owned.insert(id, owned);
            }
        }
        if (found.size() != expectedIds.size())
            fail(QStringLiteral("部分原生批注缺失, 不用旧元数据覆盖外部修改."));
        if (native.getDictionary()->get("Data").isReference())
            state.privateData = native.getDictionary()->get("Data").getReference();

        pdf::PDFDocumentBuilder clean(&state.base);
        std::set<Reference> ownedReferences;
        for (const auto& owned : state.owned)
            ownedReferences.insert(owned.annotation);
        for (int p = 0; p < count; ++p) {
            const auto* page = state.base.getCatalog()->getPage(size_t(p));
            const auto* dictionary = state.base.getDictionaryFromObject(Object::createReference(page->getPageReference()));
            const auto& annotations = state.base.getObject(dictionary->get("Annots"));
            if (!annotations.isArray())
                continue;
            pdf::PDFArrayBuilder kept;
            for (const auto& item : *annotations.getArray()) {
                const bool ours = item.isReference()
                    && ownedReferences.count(item.getReference()) != 0;
                if (!ours)
                    kept.appendItem(item);
            }
            DictBuilder updated(*dictionary);
            put(updated, "Annots", Object::createArray(std::move(kept)));
            clean.setObject(page->getPageReference(), Object::createDictionary(std::move(updated)));
        }
        DictBuilder updatedCatalog(*catalog);
        updatedCatalog.removeEntry(NativeKey);
        clean.setObject(clean.getCatalogReference(), Object::createDictionary(std::move(updatedCatalog)));
        state.base = clean.build();
        state.pages = std::move(pages);
        state.textAppearanceHashes = std::move(textAppearanceHashes);
    } catch (const std::exception& exception) {
        state.readOnlyReason = QString::fromUtf8(exception.what());
        state.pages = QVector<PageAnnotations>(count);
        state.owned.clear();
        state.privateData.reset();
        state.textAppearanceHashes.clear();
    }
    return state;
}

QByteArray real(double value)
{
    return QByteArray::number(std::abs(value) < 1e-12 ? 0.0 : value, 'g', 15);
}

QByteArray pathAppearanceContent(const QPainterPath& path, const QRectF& box, const QColor& color,
                                 QByteArray (*number)(double))
{
    QByteArray bytes = "q\n/PNAlpha gs\n";
    bytes += number(color.redF()) + ' ' + number(color.greenF()) + ' '
             + number(color.blueF()) + " rg\n";
    auto appendPoint = [&](qreal x, qreal y) {
        bytes += number(x - box.left()) + ' ' + number(y - box.top());
    };
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (element.type == QPainterPath::MoveToElement) {
            appendPoint(element.x, element.y); bytes += " m\n";
        } else if (element.type == QPainterPath::LineToElement) {
            appendPoint(element.x, element.y); bytes += " l\n";
        } else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            appendPoint(element.x, element.y); bytes += ' ';
            const auto second = path.elementAt(++i);
            appendPoint(second.x, second.y); bytes += ' ';
            const auto end = path.elementAt(++i);
            appendPoint(end.x, end.y); bytes += " c\n";
        }
    }
    bytes += "f\nQ\n";
    return bytes;
}

QByteArray appearanceContent(const InkStroke& stroke, const QRectF& box)
{
    return pathAppearanceContent(inkStrokeOutline(stroke), box, stroke.color,
        stroke.kind == InkStrokeKind::Highlighter ? pdfPathNumber : real);
}

Object createVectorAppearance(const QRectF& box, const QColor& color, QByteArray bytes, bool multiply)
{
    DictBuilder alpha;
    put(alpha, "Type", Object::createName(QByteArray("ExtGState")));
    put(alpha, "ca", Object::createReal(color.alphaF()));
    put(alpha, "CA", Object::createReal(color.alphaF()));
    if (multiply)
        put(alpha, "BM", Object::createName(QByteArray("Multiply")));
    DictBuilder states;
    put(states, "PNAlpha", Object::createDictionary(std::move(alpha)));
    DictBuilder resources;
    put(resources, "ExtGState", Object::createDictionary(std::move(states)));
    DictBuilder form;
    put(form, "Type", Object::createName(QByteArray("XObject")));
    put(form, "Subtype", Object::createName(QByteArray("Form")));
    put(form, "FormType", Object::createInteger(1));
    put(form, "BBox", arrayOf({ 0, 0, box.width(), box.height() }));
    put(form, "Matrix", arrayOf({ 1, 0, 0, 1, 0, 0 }));
    put(form, "Resources", Object::createDictionary(std::move(resources)));
    return streamObject(std::move(form), std::move(bytes));
}

Object createAppearance(const InkStroke& stroke, const QRectF& box)
{
    return createVectorAppearance(box, stroke.color, appearanceContent(stroke, box),
        stroke.kind == InkStrokeKind::Highlighter);
}

QByteArray pdfPathNumber(double value)
{
    // PDF content streams use decimal real tokens without scientific notation.
    QByteArray token = QByteArray::number(value, 'f', 8);
    while (token.endsWith('0'))
        token.chop(1);
    if (token.endsWith('.'))
        token.chop(1);
    return token == "-0" ? QByteArray("0") : token;
}

QByteArray markupAppearanceContent(const TextMarkup& markup, const QRectF& box)
{
    return pathAppearanceContent(textMarkupPath(markup), box, markup.color, pdfPathNumber);
}

QByteArray pdfUnicodeString(const QString& text)
{
    QByteArray bytes;
    bytes.reserve(2 + text.size() * 2);
    bytes.append(char(0xfe));
    bytes.append(char(0xff));
    for (const QChar character : text) {
        const ushort value = character.unicode();
        bytes.append(char(value >> 8));
        bytes.append(char(value & 0xff));
    }
    return bytes;
}

QSize textRasterSize(const TextBox& box)
{
    const qreal width = box.rect.width();
    const qreal height = box.rect.height();
    const qreal scale = std::min({ qreal(2), MaxTextImageSide / width, MaxTextImageSide / height,
        std::sqrt(qreal(MaxTextImagePixels) / (width * height)) });
    return QSize(qMax(1, int(std::floor(width * scale))), qMax(1, int(std::floor(height * scale))));
}

QByteArray textAppearanceContent(const TextBox& box)
{
    return "q\n" + real(box.rect.width()) + " 0 0 " + real(box.rect.height())
        + " 0 0 cm\n/PNTextImage Do\nQ\n";
}

QByteArray textDefaultAppearance(const TextBox& box)
{
    return "/Helv " + real(box.fontSizePt) + " Tf " + real(box.color.redF()) + ' '
        + real(box.color.greenF()) + ' ' + real(box.color.blueF()) + " rg";
}

// This binds the retained raster bytes to the editable object without asking
// another machine to reproduce its font fallback or rasterizer byte for byte.
QByteArray textAppearanceDigest(const TextBox& box, const QSize& size,
                              const QByteArray& rgb, const QByteArray& alpha)
{
    return sha256(textBoxDigest(box) + '\0' + QByteArray::number(size.width()) + 'x'
        + QByteArray::number(size.height()) + '\0' + sha256(rgb) + sha256(alpha)).toHex();
}

struct CreatedTextAppearance {
    Object appearance;
    QByteArray digest;
};

CreatedTextAppearance createTextAppearance(pdf::PDFDocumentBuilder& builder, const TextBox& box)
{
    validateTextBox(box);
    const QSize size = textRasterSize(box);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        fail(QStringLiteral("无法分配文字框外观图像, 未写入文件."));
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const qreal sx = qreal(size.width()) / box.rect.width();
    const qreal sy = qreal(size.height()) / box.rect.height();
    painter.setWorldTransform(QTransform(sx, 0, 0, -sy,
        -box.rect.left() * sx, box.rect.bottom() * sy));
    paintTextBox(painter, box);
    painter.end();
    image = image.convertToFormat(QImage::Format_ARGB32);
    QByteArray rgb(size.width() * size.height() * 3, Qt::Uninitialized);
    QByteArray alpha(size.width() * size.height(), Qt::Uninitialized);
    qsizetype pixel = 0;
    for (int y = 0; y < size.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < size.width(); ++x, ++pixel) {
            rgb[pixel * 3] = char(qRed(row[x]));
            rgb[pixel * 3 + 1] = char(qGreen(row[x]));
            rgb[pixel * 3 + 2] = char(qBlue(row[x]));
            alpha[pixel] = char(qAlpha(row[x]));
        }
    }
    // qCompress adds a four-byte Qt length prefix; a PDF Flate stream uses only
    // the following zlib bytes. Font files are not embedded or redistributed.
    rgb = qCompress(rgb, 6).mid(4);
    alpha = qCompress(alpha, 6).mid(4);
    auto imageDictionary = [&](const char* colorSpace) {
        DictBuilder dictionary;
        put(dictionary, "Type", Object::createName(QByteArray("XObject")));
        put(dictionary, "Subtype", Object::createName(QByteArray("Image")));
        put(dictionary, "Width", Object::createInteger(size.width()));
        put(dictionary, "Height", Object::createInteger(size.height()));
        put(dictionary, "BitsPerComponent", Object::createInteger(8));
        put(dictionary, "ColorSpace", Object::createName(QByteArray(colorSpace)));
        put(dictionary, "Filter", Object::createName(QByteArray("FlateDecode")));
        return dictionary;
    };
    const auto mask = builder.addObject(streamObject(imageDictionary("DeviceGray"), alpha));
    DictBuilder rgbDictionary = imageDictionary("DeviceRGB");
    put(rgbDictionary, "SMask", Object::createReference(mask));
    const auto raster = builder.addObject(streamObject(std::move(rgbDictionary), rgb));
    DictBuilder xobjects;
    put(xobjects, "PNTextImage", Object::createReference(raster));
    // A standard base-14 fallback is named by /DA for other readers. The saved
    // appearance itself uses the raster, including complete Chinese glyphs.
    DictBuilder font;
    put(font, "Type", Object::createName(QByteArray("Font")));
    put(font, "Subtype", Object::createName(QByteArray("Type1")));
    put(font, "BaseFont", Object::createName(QByteArray("Helvetica")));
    put(font, "Encoding", Object::createName(QByteArray("WinAnsiEncoding")));
    DictBuilder fonts;
    put(fonts, "Helv", Object::createDictionary(std::move(font)));
    DictBuilder resources;
    put(resources, "XObject", Object::createDictionary(std::move(xobjects)));
    put(resources, "Font", Object::createDictionary(std::move(fonts)));
    DictBuilder form;
    put(form, "Type", Object::createName(QByteArray("XObject")));
    put(form, "Subtype", Object::createName(QByteArray("Form")));
    put(form, "FormType", Object::createInteger(1));
    put(form, "BBox", arrayOf({ 0, 0, box.rect.width(), box.rect.height() }));
    put(form, "Matrix", arrayOf({ 1, 0, 0, 1, 0, 0 }));
    put(form, "Resources", Object::createDictionary(std::move(resources)));
    return { streamObject(std::move(form), textAppearanceContent(box)),
        textAppearanceDigest(box, size, rgb, alpha) };
}

bool matchesNumbers(const pdf::PDFDocument& document, const Object& object,
                    std::initializer_list<double> expected)
{
    const auto& array = document.getObject(object);
    if (!array.isArray() || array.getArray()->getCount() != expected.size())
        return false;
    size_t index = 0;
    for (double value : expected) {
        const double actual = numberValue(document, array.getArray()->getItem(index++));
        if (!std::isfinite(actual) || std::abs(actual - value) > PdfRealWriteTolerance)
            return false;
    }
    return true;
}

bool markupAnnotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                             const TextMarkup& markup, Reference pageReference)
{
    const QByteArray id = markup.id.toByteArray(QUuid::WithoutBraces);
    const QRectF box = textMarkupBounds(markup);
    const auto& page = annotation.get("P");
    if (stringValue(document, annotation.get("Type")) != "Annot"
        || stringValue(document, annotation.get("Subtype")) != markupSubtype(markup.kind)
        || numberValue(document, annotation.get("PNVersion")) != MarkupFormatVersion
        || stringValue(document, annotation.get("NM")) != QByteArray(MarkupIdPrefix) + id
        || stringValue(document, annotation.get("PNMarkupID")) != id
        || stringValue(document, annotation.get("PNMarkupSHA256")) != textMarkupDigest(markup)
        || !page.isReference() || page.getReference() != pageReference
        || numberValue(document, annotation.get("F")) != 4
        || !annotation.get("AS").isNull() || !annotation.get("OC").isNull() || !annotation.get("IRT").isNull()
        || !matchesNumbers(document, annotation.get("Rect"), { box.left(), box.top(), box.right(), box.bottom() })
        || !matchesNumbers(document, annotation.get("C"), { markup.color.redF(), markup.color.greenF(), markup.color.blueF() }))
        return false;
    const auto& coordinates = document.getObject(annotation.get("QuadPoints"));
    if (!coordinates.isArray() || coordinates.getArray()->getCount() != size_t(markup.quads.size()) * 8)
        return false;
    size_t index = 0;
    for (const auto& quad : markup.quads) {
        for (const auto& point : markupQuadPoints(quad)) {
            const double x = numberValue(document, coordinates.getArray()->getItem(index++));
            const double y = numberValue(document, coordinates.getArray()->getItem(index++));
            if (!std::isfinite(x) || !std::isfinite(y)
                || std::abs(x - point.x()) > PdfRealWriteTolerance || std::abs(y - point.y()) > PdfRealWriteTolerance)
                return false;
        }
    }
    const auto* ap = document.getDictionaryFromObject(annotation.get("AP"));
    if (!ap || !hasOnlyKeys(*ap, { "N" }) || !ap->get("N").isReference())
        return false;
    const auto& appearance = document.getObject(ap->get("N"));
    if (!appearance.isStream())
        return false;
    const auto* form = appearance.getStream()->getDictionary();
    if (!hasOnlyKeys(*form, { "Type", "Subtype", "FormType", "BBox", "Matrix", "Resources", "Length" })
        || stringValue(document, form->get("Type")) != "XObject"
        || stringValue(document, form->get("Subtype")) != "Form"
        || numberValue(document, form->get("FormType")) != 1
        || !matchesNumbers(document, form->get("BBox"), { 0, 0, box.width(), box.height() })
        || !matchesNumbers(document, form->get("Matrix"), { 1, 0, 0, 1, 0, 0 }))
        return false;
    const auto* resources = document.getDictionaryFromObject(form->get("Resources"));
    const auto* states = resources ? document.getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
    const auto* alpha = states ? document.getDictionaryFromObject(states->get("PNAlpha")) : nullptr;
    if (!alpha || !hasOnlyKeys(*resources, { "ExtGState" }) || !hasOnlyKeys(*states, { "PNAlpha" })
        || !hasOnlyKeys(*alpha, { "Type", "ca", "CA", "BM" })
        || stringValue(document, alpha->get("Type")) != "ExtGState")
        return false;
    if (markup.kind == TextMarkupKind::Highlight) {
        if (stringValue(document, alpha->get("BM")) != "Multiply")
            return false;
    } else if (!alpha->get("BM").isNull()) {
        return false;
    }
    for (double opacity : { numberValue(document, annotation.get("CA")),
             numberValue(document, alpha->get("ca")), numberValue(document, alpha->get("CA")) }) {
        if (!std::isfinite(opacity) || std::abs(opacity - markup.color.alphaF()) > PdfRealWriteTolerance)
            return false;
    }
    return *appearance.getStream()->getContent() == markupAppearanceContent(markup, box);
}

bool textAnnotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                           const TextBox& box, const QByteArray& expectedAppearanceHash,
                           QVector<Reference>& ownedResources)
{
    if (stringValue(document, annotation.get("Subtype")) != "FreeText"
        || numberValue(document, annotation.get("PNVersion")) != TextFormatVersion
        || stringValue(document, annotation.get("NM")) != QByteArray(TextIdPrefix) + box.id.toByteArray(QUuid::WithoutBraces)
        || stringValue(document, annotation.get("PNTextSHA256")) != textBoxDigest(box)
        || stringValue(document, annotation.get("PNTextAPSHA256")) != expectedAppearanceHash
        || stringValue(document, annotation.get("Contents")) != pdfUnicodeString(box.text)
        || stringValue(document, annotation.get("DA")) != textDefaultAppearance(box)
        || !annotation.get("RC").isNull()
        || numberValue(document, annotation.get("CA")) != 1
        || !matchesNumbers(document, annotation.get("Rect"),
            { box.rect.left(), box.rect.top(), box.rect.right(), box.rect.bottom() }))
        return false;
    const auto* ap = document.getDictionaryFromObject(annotation.get("AP"));
    if (!ap || !ap->get("N").isReference())
        return false;
    const auto& appearance = document.getObject(ap->get("N"));
    if (!appearance.isStream())
        return false;
    const auto* form = appearance.getStream()->getDictionary();
    if (stringValue(document, form->get("Type")) != "XObject"
        || stringValue(document, form->get("Subtype")) != "Form"
        || numberValue(document, form->get("FormType")) != 1
        || !hasOnlyKeys(*form, { "Type", "Subtype", "FormType", "BBox", "Matrix", "Resources", "Length" })
        || !form->get("Filter").isNull() || !form->get("Group").isNull()
        || !matchesNumbers(document, form->get("BBox"), { 0, 0, box.rect.width(), box.rect.height() })
        || !matchesNumbers(document, form->get("Matrix"), { 1, 0, 0, 1, 0, 0 })
        || *appearance.getStream()->getContent() != textAppearanceContent(box))
        return false;
    const auto* resources = document.getDictionaryFromObject(form->get("Resources"));
    const auto* xobjects = resources ? document.getDictionaryFromObject(resources->get("XObject")) : nullptr;
    if (!xobjects || !hasOnlyKeys(*resources, { "XObject", "Font" })
        || !hasOnlyKeys(*xobjects, { "PNTextImage" }) || !xobjects->get("PNTextImage").isReference())
        return false;
    const auto& rgb = document.getObject(xobjects->get("PNTextImage"));
    if (!rgb.isStream())
        return false;
    const auto* rgbDictionary = rgb.getStream()->getDictionary();
    if (!rgbDictionary->get("SMask").isReference())
        return false;
    const auto& alpha = document.getObject(rgbDictionary->get("SMask"));
    if (!alpha.isStream())
        return false;
    const QSize size = textRasterSize(box);
    auto validImage = [&](const pdf::PDFStream* stream, const char* colorSpace) {
        const auto* dictionary = stream->getDictionary();
        return stringValue(document, dictionary->get("Type")) == "XObject"
            && hasOnlyKeys(*dictionary, { "Type", "Subtype", "Width", "Height", "BitsPerComponent",
                "ColorSpace", "Filter", "SMask", "Length" })
            && stringValue(document, dictionary->get("Subtype")) == "Image"
            && numberValue(document, dictionary->get("Width")) == size.width()
            && numberValue(document, dictionary->get("Height")) == size.height()
            && numberValue(document, dictionary->get("BitsPerComponent")) == 8
            && stringValue(document, dictionary->get("ColorSpace")) == colorSpace
            && stringValue(document, dictionary->get("Filter")) == "FlateDecode"
            && dictionary->get("DecodeParms").isNull() && dictionary->get("Decode").isNull()
            && dictionary->get("Mask").isNull() && dictionary->get("ImageMask").isNull()
            && !stream->getContent()->isEmpty()
            && stream->getContent()->size() <= MaxTextImagePixels * 4 + 65536;
    };
    if (!validImage(rgb.getStream(), "DeviceRGB") || !validImage(alpha.getStream(), "DeviceGray")
        || !alpha.getStream()->getDictionary()->get("SMask").isNull())
        return false;
    if (textAppearanceDigest(box, size, *rgb.getStream()->getContent(), *alpha.getStream()->getContent())
        != expectedAppearanceHash)
        return false;
    ownedResources.append(xobjects->get("PNTextImage").getReference());
    ownedResources.append(rgbDictionary->get("SMask").getReference());
    return true;
}

pdf::PDFDocument withAnnotations(const LoadedState& state)
{
    const int dataVersion = formatVersionFor(state.pages);
    auto pdfVersion = state.base.getInfo()->version;
    // Squiggly and the new Multiply/opacity appearances require PDF 1.4.
    if (dataVersion >= MarkupFormatVersion && (pdfVersion.major < 1 || (pdfVersion.major == 1 && pdfVersion.minor < 4)))
        pdfVersion = pdf::PDFVersion(1, 4);
    pdf::PDFDocumentBuilder builder(state.base.getStorage(), pdfVersion);
    const auto* catalog = catalogDictionary(state.base);
    QHash<QUuid, QByteArray> textAppearanceHashes;
    for (int p = 0; p < state.pages.size(); ++p) {
        const auto* page = state.base.getCatalog()->getPage(size_t(p));
        const auto* dictionary = state.base.getDictionaryFromObject(Object::createReference(page->getPageReference()));
        if (!dictionary)
            fail(QStringLiteral("页面字典损坏, 未写入文件."));
        const auto& existing = state.base.getObject(dictionary->get("Annots"));
        if (!existing.isNull() && !existing.isArray())
            fail(QStringLiteral("页面批注数组损坏, 未写入文件."));
        pdf::PDFArrayBuilder annotations = existing.isArray()
            ? pdf::PDFArrayBuilder(*existing.getArray()) : pdf::PDFArrayBuilder();
        for (const auto& markup : state.pages[p].textMarkups) {
            if (!validTextMarkup(markup))
                fail(QStringLiteral("正文标记无效，未写入文件。"));
            const QRectF box = textMarkupBounds(markup);
            const QByteArray digest = textMarkupDigest(markup);
            const QByteArray id = markup.id.toByteArray(QUuid::WithoutBraces);
            const auto found = state.owned.constFind(markup.id);
            const auto* previous = found == state.owned.cend() ? nullptr
                : state.base.getDictionaryFromObject(Object::createReference(found.value().annotation));
            const bool unchanged = previous && found.value().appearance
                && stringValue(state.base, previous->get("PNMarkupSHA256")) == digest;
            const auto appearance = unchanged ? *found.value().appearance : builder.addObject(
                createVectorAppearance(box, markup.color, markupAppearanceContent(markup, box),
                    markup.kind == TextMarkupKind::Highlight));
            DictBuilder ap;
            put(ap, "N", Object::createReference(appearance));
            pdf::PDFArrayBuilder quadPoints;
            for (const auto& quad : markup.quads) {
                for (const auto& point : markupQuadPoints(quad)) {
                    quadPoints.appendItem(Object::createReal(point.x()));
                    quadPoints.appendItem(Object::createReal(point.y()));
                }
            }
            DictBuilder annotation = previous ? DictBuilder(*previous) : DictBuilder();
            put(annotation, "Type", Object::createName(QByteArray("Annot")));
            put(annotation, "Subtype", Object::createName(markupSubtype(markup.kind)));
            put(annotation, "Rect", arrayOf({ box.left(), box.top(), box.right(), box.bottom() }));
            put(annotation, "P", Object::createReference(page->getPageReference()));
            put(annotation, "F", Object::createInteger(4));
            put(annotation, "C", arrayOf({ markup.color.redF(), markup.color.greenF(), markup.color.blueF() }));
            put(annotation, "CA", Object::createReal(markup.color.alphaF()));
            put(annotation, "QuadPoints", Object::createArray(std::move(quadPoints)));
            put(annotation, "AP", Object::createDictionary(std::move(ap)));
            put(annotation, "NM", Object::createString(QByteArray(MarkupIdPrefix) + id));
            put(annotation, "PNOwner", Object::createName(QByteArray(OwnerName)));
            put(annotation, "PNVersion", Object::createInteger(MarkupFormatVersion));
            put(annotation, "PNMarkupID", Object::createString(id));
            put(annotation, "PNMarkupSHA256", Object::createString(digest));
            Reference reference;
            if (found != state.owned.cend()) {
                reference = found.value().annotation;
                builder.setObject(reference, Object::createDictionary(std::move(annotation)));
            } else {
                reference = builder.addObject(Object::createDictionary(std::move(annotation)));
            }
            annotations.appendItem(Object::createReference(reference));
        }
        for (const auto& stroke : state.pages[p].strokes) {
            validateStroke(stroke);
            const QRectF box = inkStrokeOutline(stroke).boundingRect();
            const auto found = state.owned.constFind(stroke.id);
            const auto* previous = found == state.owned.cend() ? nullptr
                : state.base.getDictionaryFromObject(Object::createReference(found.value().annotation));
            const bool unchanged = previous && found.value().appearance
                && stringValue(state.base, previous->get("PNStrokeSHA256")) == strokeDigest(stroke);
            const auto appearance = unchanged ? *found.value().appearance
                : builder.addObject(createAppearance(stroke, box));
            DictBuilder ap;
            put(ap, "N", Object::createReference(appearance));
            pdf::PDFArrayBuilder coordinates;
            for (const auto& point : stroke.points) {
                coordinates.appendItem(Object::createReal(point.position.x()));
                coordinates.appendItem(Object::createReal(point.position.y()));
            }
            pdf::PDFArrayBuilder inkList;
            inkList.appendItem(Object::createArray(std::move(coordinates)));
            // Keep additional fields on our annotation, such as a comment or
            // popup added by another reader. Only this format's fields change.
            DictBuilder annotation = previous ? DictBuilder(*previous) : DictBuilder();
            put(annotation, "Type", Object::createName(QByteArray("Annot")));
            put(annotation, "Subtype", Object::createName(QByteArray("Ink")));
            put(annotation, "Rect", arrayOf({ box.left(), box.top(), box.right(), box.bottom() }));
            put(annotation, "P", Object::createReference(page->getPageReference()));
            put(annotation, "F", Object::createInteger(4));
            put(annotation, "C", arrayOf({ stroke.color.redF(), stroke.color.greenF(), stroke.color.blueF() }));
            put(annotation, "CA", Object::createReal(stroke.color.alphaF()));
            put(annotation, "Border", arrayOf({ 0, 0, stroke.widthPt }));
            put(annotation, "InkList", Object::createArray(std::move(inkList)));
            put(annotation, "AP", Object::createDictionary(std::move(ap)));
            put(annotation, "NM", Object::createString(QByteArray(IdPrefix) + stroke.id.toByteArray(QUuid::WithoutBraces)));
            put(annotation, "PNOwner", Object::createName(QByteArray(OwnerName)));
            put(annotation, "PNVersion", Object::createInteger(strokeFormatVersion(stroke)));
            put(annotation, "PNStrokeID", Object::createString(stroke.id.toByteArray(QUuid::WithoutBraces)));
            put(annotation, "PNStrokeSHA256", Object::createString(strokeDigest(stroke)));
            Reference reference;
            if (found != state.owned.cend()) {
                reference = found.value().annotation;
                builder.setObject(reference, Object::createDictionary(std::move(annotation)));
            } else {
                reference = builder.addObject(Object::createDictionary(std::move(annotation)));
            }
            annotations.appendItem(Object::createReference(reference));
        }
        for (const auto& box : state.pages[p].textBoxes) {
            validateTextBox(box);
            const auto found = state.owned.constFind(box.id);
            const auto* previous = found == state.owned.cend() ? nullptr
                : state.base.getDictionaryFromObject(Object::createReference(found.value().annotation));
            const bool unchanged = previous && found.value().appearance
                && stringValue(state.base, previous->get("Subtype")) == "FreeText"
                && stringValue(state.base, previous->get("PNTextSHA256")) == textBoxDigest(box)
                && !state.textAppearanceHashes.value(box.id).isEmpty();
            Reference appearance;
            QByteArray digest;
            if (unchanged) {
                appearance = *found.value().appearance;
                digest = state.textAppearanceHashes.value(box.id);
            } else {
                auto created = createTextAppearance(builder, box);
                appearance = builder.addObject(std::move(created.appearance));
                digest = std::move(created.digest);
            }
            textAppearanceHashes.insert(box.id, digest);
            DictBuilder ap;
            put(ap, "N", Object::createReference(appearance));
            DictBuilder annotation = previous ? DictBuilder(*previous) : DictBuilder();
            put(annotation, "Type", Object::createName(QByteArray("Annot")));
            put(annotation, "Subtype", Object::createName(QByteArray("FreeText")));
            put(annotation, "Rect", arrayOf({ box.rect.left(), box.rect.top(), box.rect.right(), box.rect.bottom() }));
            put(annotation, "P", Object::createReference(page->getPageReference()));
            put(annotation, "F", Object::createInteger(4));
            put(annotation, "CA", Object::createReal(1)); // Alpha is baked into the raster mask once.
            put(annotation, "Contents", Object::createString(pdfUnicodeString(box.text)));
            put(annotation, "DA", Object::createString(textDefaultAppearance(box)));
            put(annotation, "Q", Object::createInteger(0));
            put(annotation, "Border", arrayOf({ 0, 0, 0 }));
            put(annotation, "AP", Object::createDictionary(std::move(ap)));
            put(annotation, "NM", Object::createString(QByteArray(TextIdPrefix) + box.id.toByteArray(QUuid::WithoutBraces)));
            put(annotation, "PNOwner", Object::createName(QByteArray(OwnerName)));
            put(annotation, "PNVersion", Object::createInteger(TextFormatVersion));
            put(annotation, "PNTextID", Object::createString(box.id.toByteArray(QUuid::WithoutBraces)));
            put(annotation, "PNTextSHA256", Object::createString(textBoxDigest(box)));
            put(annotation, "PNTextAPSHA256", Object::createString(digest));
            Reference reference;
            if (found != state.owned.cend()) {
                reference = found.value().annotation;
                builder.setObject(reference, Object::createDictionary(std::move(annotation)));
            } else {
                reference = builder.addObject(Object::createDictionary(std::move(annotation)));
            }
            annotations.appendItem(Object::createReference(reference));
        }
        DictBuilder updated(*dictionary);
        put(updated, "Annots", Object::createArray(std::move(annotations)));
        builder.setObject(page->getPageReference(), Object::createDictionary(std::move(updated)));
    }
    QByteArray data = editData(state.pages, &textAppearanceHashes);
    if (data.size() > MaxMetadataBytes)
        fail(QStringLiteral("原生编辑数据超出此原型支持范围."));
    DictBuilder dataDictionary;
    const auto dataReference = builder.addObject(streamObject(std::move(dataDictionary), data));
    DictBuilder metadata;
    put(metadata, "Type", Object::createName(QByteArray(NativeKey)));
    put(metadata, "Version", Object::createInteger(dataVersion));
    put(metadata, "Data", Object::createReference(dataReference));
    put(metadata, "SHA256", Object::createString(sha256(data).toHex()));
    DictBuilder updatedCatalog(*catalog);
    put(updatedCatalog, NativeKey, Object::createDictionary(std::move(metadata)));
    builder.setObject(builder.getCatalogReference(), Object::createDictionary(std::move(updatedCatalog)));
    return builder.build();
}

pdf::PDFDocument compactSupersededOwnedObjects(const LoadedState& previous,
                                               const pdf::PDFDocument& output)
{
    // These references were established by validating the native metadata and
    // matching Ink/AP records on open. Never infer ownership merely because an
    // arbitrary original PDF object is unreachable.
    std::set<Reference> candidates;
    if (previous.privateData)
        candidates.insert(*previous.privateData);
    for (const auto& owned : previous.owned) {
        candidates.insert(owned.annotation);
        if (owned.appearance)
            candidates.insert(*owned.appearance);
        for (const auto& resource : owned.resources)
            candidates.insert(resource);
    }
    if (candidates.empty())
        return output;

    const auto& storage = output.getStorage();
    const auto reachable = pdf::PDFObjectUtils::getReferences(
        { storage.getTrailerDictionary() }, storage);
    pdf::PDFDocumentBuilder cleaned(&output);
    bool removed = false;
    for (const auto& reference : candidates) {
        // A foreign annotation, reply, attachment or resource may still refer
        // to an older owned object. Preserve it whenever the final graph does.
        if (!reachable.count(reference)
            && !storage.getObjectByReference(reference).isNull()) {
            cleaned.setObject(reference, Object::createNull());
            removed = true;
        }
    }
    if (!removed)
        return output;

    const auto originalVersion = output.getInfo()->version;
    pdf::PDFDocument pruned = cleaned.build();
    // Only compact free slots and remap actual PDF reference objects. The
    // upstream visitor copies raw stream bytes unchanged. Global unused-object
    // removal, resource merging, dereferencing and recompression stay disabled.
    pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
    optimizer.setDocument(&pruned);
    optimizer.optimize();
    // takeOptimizedDocument() hard-codes PDF 2.0 in this upstream version.
    // Rebuild from the optimized storage with the original version instead.
    pdf::PDFDocumentBuilder compacted(optimizer.getStorage(), originalVersion);
    return compacted.build();
}

struct PdfCandidate {
    QByteArray bytes;
    QByteArray hash;
    std::unique_ptr<LoadedState> verified;
};

PdfCandidate buildVerifiedCandidate(const LoadedState& state)
{
    pdf::PDFDocument output = compactSupersededOwnedObjects(state, withAnnotations(state));
    PdfCandidate candidate;
    QBuffer buffer(&candidate.bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        fail(QStringLiteral("无法创建 PDF 写入缓冲区."));
    pdf::PDFDocumentWriter writer(nullptr);
    const auto result = writer.write(&buffer, &output);
    if (!result)
        fail(result.getErrorMessage());
    buffer.close();
    if (candidate.bytes.isEmpty() || candidate.bytes.size() > MaxFileBytes)
        fail(QStringLiteral("输出 PDF 大小超出此原型范围, 未覆盖原文件."));
    candidate.verified = std::make_unique<LoadedState>(loadState(candidate.bytes));
    if (!candidate.verified->readOnlyReason.isEmpty()
        || editData(candidate.verified->pages) != editData(state.pages))
        fail(QStringLiteral("写入后重新读取校验失败, 未覆盖原文件."));
    candidate.hash = sha256(candidate.bytes);
    return candidate;
}

QTransform renderTransform(const pdf::PDFPage* page, qreal scale)
{
    const QRectF box = page->getCropBox();
    scale *= page->getUserUnit();
    switch (page->getPageRotation()) {
    case pdf::PageRotation::Rotate90:
        return QTransform(0, scale, scale, 0, -box.top() * scale, -box.left() * scale);
    case pdf::PageRotation::Rotate180:
        return QTransform(-scale, 0, 0, scale, box.right() * scale, -box.top() * scale);
    case pdf::PageRotation::Rotate270:
        return QTransform(0, -scale, -scale, 0, box.bottom() * scale, box.right() * scale);
    default:
        return QTransform(scale, 0, 0, -scale, -box.left() * scale, box.bottom() * scale);
    }
}
} // namespace

struct PdfDocument::Impl {
    std::unique_ptr<LoadedState> state;
    QString path;
    QByteArray sourceSha256;
    QString error;
    bool modified = false;
};

PdfDocument::PdfDocument() : d(std::make_unique<Impl>()) {}
PdfDocument::~PdfDocument() = default;

bool PdfDocument::open(const QString& path)
{
    try {
        const QString resolved = normalizedPath(path);
        if (resolved.isEmpty())
            fail(QStringLiteral("PDF 路径无效."));
        const QByteArray bytes = readFile(resolved);
        auto candidate = std::make_unique<LoadedState>(loadState(bytes));
        d->state = std::move(candidate);
        d->path = resolved;
        d->sourceSha256 = sha256(bytes);
        d->modified = false;
        d->error.clear();
        return true;
    } catch (const pdf::PDFException& exception) {
        d->error = exception.getMessage();
    } catch (const std::exception& exception) {
        d->error = QString::fromUtf8(exception.what());
    }
    return false;
}

QByteArray PdfDocument::fileSha256(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error) *error = file.errorString();
        return {};
    }
    if (error) error->clear();
    return hash.result();
}

QByteArray PdfDocument::recoveryFingerprint() const
{
    if (!d->state)
        return {};
    QByteArray identity = d->sourceSha256;
    identity += isReadOnly() ? QByteArray("\0readonly", 9) : QByteArray("\0editable", 9);
    if (!isReadOnly())
        identity += editData(d->state->pages);
    return sha256(identity);
}

bool PdfDocument::writeRecoveryCopy(const QString& path, QString* error,
                                    QByteArray* writtenSha256) const
{
    return writeRecoveryCopyClaimed(path, {}, error, writtenSha256);
}

bool PdfDocument::writeRecoveryCopyClaimed(const QString& path,
    const std::function<bool(const QByteArray&, qint64)>& claim,
    QString* error, QByteArray* writtenSha256) const
{
    if (writtenSha256) writtenSha256->clear();
    try {
        if (!d->state)
            fail(QStringLiteral("尚未打开 PDF, 无法创建恢复副本."));
        const QString target = normalizedPath(path);
        auto targetOccupied = [&] {
            const QFileInfo info(target);
            return info.exists() || info.isSymLink() || info.isJunction();
        };
        if (target.isEmpty() || samePath(target, d->path) || targetOccupied())
            fail(QStringLiteral("恢复副本必须使用全新路径, 不覆盖源 PDF 或现有文件."));
        QByteArray bytes;
        QByteArray hash;
        if (isReadOnly()) {
            // Preserve protected legacy data, signatures and encryption byte for
            // byte. A changed external source is not silently snapshotted.
            bytes = readFile(d->path);
            hash = sha256(bytes);
            if (hash != d->sourceSha256)
                fail(QStringLiteral("只读源文件已被外部修改, 未创建不匹配的恢复副本."));
        } else {
            auto candidate = buildVerifiedCandidate(*d->state);
            bytes = std::move(candidate.bytes);
            hash = std::move(candidate.hash);
        }
        QLockFile lock(target + QStringLiteral(".propernotes-save.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0) || targetOccupied())
            fail(QStringLiteral("恢复副本目标已出现或正被使用, 未覆盖任何文件."));
        if (claim && !claim(hash, bytes.size())) {
            if (error) error->clear();
            return false;
        }
        QSaveFile destination(target);
        destination.setDirectWriteFallback(false);
        if (!destination.open(QIODevice::WriteOnly))
            fail(destination.errorString());
        qint64 offset = 0;
        while (offset < bytes.size()) {
            const qint64 count = destination.write(bytes.constData() + offset, bytes.size() - offset);
            if (count <= 0)
                fail(destination.errorString());
            offset += count;
        }
        if (targetOccupied())
            fail(QStringLiteral("恢复副本目标在提交前已出现, 未覆盖它."));
        if (!destination.commit())
            fail(destination.errorString());
        if (writtenSha256) *writtenSha256 = hash;
        if (error) error->clear();
        return true;
    } catch (const pdf::PDFException& exception) {
        if (error) *error = exception.getMessage();
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
    }
    return false;
}

bool PdfDocument::save(const QString& requestedPath, const QByteArray& expectedTargetSha256)
{
    try {
        if (!d->state || isReadOnly())
            fail(d->state ? readOnlyReason() : QStringLiteral("尚未打开 PDF."));
        const QString target = normalizedPath(requestedPath.isEmpty() ? d->path : requestedPath);
        if (target.isEmpty() || QFileInfo(target).isDir())
            fail(QStringLiteral("保存路径无效, 请选择已有目录中的 PDF 文件名."));
        const bool original = samePath(target, d->path);
        const bool existed = QFileInfo::exists(target);
        if ((original || !expectedTargetSha256.isEmpty()) && !existed)
            fail(QStringLiteral("预期的目标 PDF 已被移走或删除, 未重建或覆盖它. 请另存为新文件."));
        // An ephemeral coordination lock is not an editing-data sidecar. It is
        // removed by QLockFile on scope exit, including failed writes.
        QLockFile saveLock(target + QStringLiteral(".propernotes-save.lock"));
        saveLock.setStaleLockTime(0);
        if (!saveLock.tryLock(0))
            fail(QStringLiteral("另一个 ProperNotes 保存操作正在处理此目标, 请稍后重试."));
        QByteArray expected = original ? d->sourceSha256 : expectedTargetSha256;
        if (expected.size() == 64)
            expected = QByteArray::fromHex(expected);
        auto checkTarget = [&] {
            if (existed) {
                if (expected.size() != 32)
                    fail(QStringLiteral("目标已存在. 覆盖另一个文件须先确认并提供其 SHA256, 或选择新文件名."));
                QString error;
                const QByteArray current = fileSha256(target, &error);
                if (current.isEmpty() || current != expected)
                    fail(QStringLiteral("目标 PDF 已被外部修改或无法读取. 未覆盖文件, 请重新打开或另存.") + error);
            } else if (QFileInfo::exists(target)) {
                fail(QStringLiteral("保存期间目标文件已出现, 未覆盖该文件."));
            }
        };
        checkTarget();
        if (original && !d->modified) {
            d->error.clear();
            return true;
        }
        auto candidate = buildVerifiedCandidate(*d->state);
        const auto& bytes = candidate.bytes;
        QSaveFile destination(target);
        destination.setDirectWriteFallback(false);
        if (!destination.open(QIODevice::WriteOnly))
            fail(destination.errorString());
        qint64 offset = 0;
        while (offset < bytes.size()) {
            const qint64 count = destination.write(bytes.constData() + offset, bytes.size() - offset);
            if (count <= 0) {
                destination.cancelWriting();
                fail(destination.errorString());
            }
            offset += count;
        }
        checkTarget();
        if (!destination.commit())
            fail(destination.errorString());
        d->state = std::move(candidate.verified); // Already stripped of owned AP and native catalog data.
        d->path = target;
        d->sourceSha256 = std::move(candidate.hash);
        d->modified = false;
        d->error.clear();
        return true;
    } catch (const pdf::PDFException& exception) {
        d->error = exception.getMessage();
    } catch (const std::exception& exception) {
        d->error = QString::fromUtf8(exception.what());
    }
    return false;
}

bool PdfDocument::hasDocument() const { return bool(d->state); }
bool PdfDocument::isReadOnly() const { return !d->state || !d->state->readOnlyReason.isEmpty(); }
QString PdfDocument::readOnlyReason() const { return d->state ? d->state->readOnlyReason : QStringLiteral("尚未打开 PDF."); }
QString PdfDocument::lastError() const { return d->error; }
QString PdfDocument::currentPath() const { return d->path; }
bool PdfDocument::isModified() const { return d->modified; }

pdf::PDFDocument* PdfDocument::textSourceDocument() const
{
    return d->state ? &d->state->base : nullptr;
}
int PdfDocument::pageCount() const { return d->state ? int(d->state->pages.size()) : 0; }

QRectF PdfDocument::pageRect(int index) const
{
    return index >= 0 && index < pageCount()
        ? d->state->base.getCatalog()->getPage(size_t(index))->getCropBox() : QRectF();
}

QSizeF PdfDocument::pageSize(int index) const
{
    if (index < 0 || index >= pageCount())
        return {};
    const auto* page = d->state->base.getCatalog()->getPage(size_t(index));
    return pdf::PDFPage::getRotatedSize(page->getCropBox().size(), page->getPageRotation()) * page->getUserUnit();
}

QTransform PdfDocument::pageToDisplayTransform(int index) const
{
    return index >= 0 && index < pageCount()
        ? renderTransform(d->state->base.getCatalog()->getPage(size_t(index)), 1.0) : QTransform();
}

PageAnnotations PdfDocument::annotations(int index) const
{
    return index >= 0 && index < pageCount() ? d->state->pages[index] : PageAnnotations();
}

bool PdfDocument::setAnnotations(int index, const PageAnnotations& annotations)
{
    try {
        if (isReadOnly() || index < 0 || index >= pageCount())
            fail(isReadOnly() ? readOnlyReason() : QStringLiteral("页面索引无效."));
        PageAnnotations normalized = annotations;
        // The on-disk color contract is ARGB8, as in native v1. Normalize before
        // writing an AP so a higher precision QColor cannot fail its round trip.
        for (auto& stroke : normalized.strokes) {
            if (!stroke.color.isValid())
                fail(QStringLiteral("笔迹颜色无效."));
            stroke.color = QColor(stroke.color.name(QColor::HexArgb));
        }
        for (auto& box : normalized.textBoxes) {
            if (!box.color.isValid())
                fail(QStringLiteral("文字框颜色无效."));
            box.color = QColor(box.color.name(QColor::HexArgb));
        }
        for (auto& markup : normalized.textMarkups) {
            if (!markup.color.isValid())
                fail(QStringLiteral("正文标记颜色无效。"));
            markup.color = QColor(markup.color.name(QColor::HexArgb));
        }
        QSet<QUuid> ids;
        qsizetype count = 0;
        qsizetype strokeCount = 0;
        qsizetype textCount = 0;
        qsizetype textLength = 0;
        MarkupBudget markupBudget;
        for (int p = 0; p < pageCount(); ++p) {
            const auto& page = p == index ? normalized : d->state->pages[p];
            for (const auto& stroke : page.strokes) {
                validateStroke(stroke);
                if (ids.contains(stroke.id) || ++strokeCount > MaxStrokes)
                    fail(QStringLiteral("笔迹 ID 重复或笔迹数量超出上限."));
                ids.insert(stroke.id);
                count += stroke.points.size();
                if (count > MaxTotalPoints)
                    fail(QStringLiteral("笔迹采样数量超出此原型上限."));
            }
            for (const auto& box : page.textBoxes) {
                validateTextBox(box);
                textLength += box.text.size();
                if (ids.contains(box.id) || ++textCount > MaxTextBoxes || textLength > MaxTotalTextLength)
                    fail(QStringLiteral("文字框 ID 重复, 对象数量或文本长度超出原型范围."));
                ids.insert(box.id);
            }
            for (const auto& markup : page.textMarkups)
                markupBudget.add(markup, ids);
        }
        if (!(d->state->pages[index] == normalized)) {
            d->state->pages[index] = std::move(normalized);
            d->modified = true;
        }
        d->error.clear();
        return true;
    } catch (const std::exception& exception) {
        d->error = QString::fromUtf8(exception.what());
        return false;
    }
}

QVector<InkStroke> PdfDocument::strokes(int index) const
{
    return annotations(index).strokes;
}

bool PdfDocument::setStrokes(int index, const QVector<InkStroke>& strokes)
{
    auto page = annotations(index);
    page.strokes = strokes;
    return setAnnotations(index, page);
}

RenderedPage PdfDocument::renderPage(int index, qreal pixelsPerPoint)
{
    RenderedPage result;
    try {
        if (index < 0 || index >= pageCount() || !std::isfinite(pixelsPerPoint) || pixelsPerPoint <= 0)
            fail(QStringLiteral("渲染页面或比例无效."));
        const QSizeF points = pageSize(index);
        const double width = std::ceil(points.width() * pixelsPerPoint);
        const double height = std::ceil(points.height() * pixelsPerPoint);
        if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1
            || width > 16384 || height > 16384 || width * height > 32'000'000)
            fail(QStringLiteral("页面渲染尺寸过大, 请降低缩放比例."));
        result.image = QImage(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
        if (result.image.isNull())
            fail(QStringLiteral("无法分配页面图像."));
        result.image.fill(Qt::white);
        auto& document = d->state->base;
        const auto* page = document.getCatalog()->getPage(size_t(index));
        result.pdfToImage = renderTransform(page, pixelsPerPoint);
        pdf::PDFOptionalContentActivity optional(&document, pdf::OCUsage::View, nullptr);
        pdf::PDFModifiedDocument modified(&document, &optional);
        pdf::PDFFontCache fonts(256, 512);
        fonts.setDocument(modified);
        pdf::PDFICCProfileCache profiles;
        profiles.setDocument(&document);
        pdf::PDFCMSManager cms(nullptr);
        cms.setDocument(&document);
        cms.setSettings(cms.getDefaultSettings());
        const auto colors = cms.getCurrentCMS();
        const auto features = pdf::PDFRenderer::getDefaultFeatures();
        const pdf::PDFMeshQualitySettings mesh;
        pdf::PDFRenderer renderer(&document, &fonts, &profiles, colors.data(), &optional, features, mesh);
        QPainter painter(&result.image);
        auto errors = renderer.render(&painter, result.pdfToImage, size_t(index));
        pdf::PDFAnnotationManager annotations(&fonts, &profiles, &cms, &optional, mesh,
            features, pdf::PDFAnnotationManager::Target::View, nullptr);
        annotations.setDocument(modified);
        pdf::PDFTextLayoutGetter layout(nullptr, index);
        const auto convertor = colors->getColorConvertor();
        annotations.drawPage(&painter, index, nullptr, layout, result.pdfToImage, convertor, errors);
        painter.end();
        QStringList failures;
        for (const auto& error : errors) {
            if (error.type == pdf::RenderErrorType::Error)
                failures.append(error.message);
        }
        if (!failures.isEmpty())
            fail(QStringLiteral("PDF 页面未能完整渲染: ") + failures.join(QStringLiteral("; ")));
        d->error.clear();
    } catch (const pdf::PDFException& exception) {
        result.error = exception.getMessage();
    } catch (const pdf::PDFRendererException& exception) {
        result.error = exception.getError().message;
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what());
    }
    if (!result.error.isEmpty()) {
        result.image = {};
        d->error = result.error;
    }
    return result;
}

} // namespace propernotes
