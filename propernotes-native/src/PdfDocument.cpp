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
constexpr int FormatVersion = 1;
constexpr qint64 MaxFileBytes = 512LL * 1024 * 1024;
constexpr qsizetype MaxMetadataBytes = 64 * 1024 * 1024;
constexpr qsizetype MaxTotalPoints = 2'000'000;
constexpr qsizetype MaxStrokePoints = 200'000;
constexpr qsizetype MaxStrokes = 100'000;

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

bool finiteCoordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1e8;
}

void validateStroke(const InkStroke& stroke)
{
    if (stroke.id.isNull() || !stroke.color.isValid()
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
    return QJsonObject { { "id", stroke.id.toString(QUuid::WithoutBraces) },
                         { "color", stroke.color.name(QColor::HexArgb) },
                         { "widthPt", stroke.widthPt }, { "points", points } };
}

QByteArray strokeDigest(const InkStroke& stroke)
{
    return sha256(QJsonDocument(strokeJson(stroke)).toJson(QJsonDocument::Compact)).toHex();
}

QByteArray editData(const QVector<QVector<InkStroke>>& pages)
{
    QJsonArray pageArray;
    for (const auto& page : pages) {
        QJsonArray strokes;
        for (const auto& stroke : page)
            strokes.append(strokeJson(stroke));
        pageArray.append(strokes);
    }
    return QJsonDocument(QJsonObject { { "application", "ProperNotes.Native" },
        { "version", FormatVersion }, { "pages", pageArray } }).toJson(QJsonDocument::Compact);
}

QVector<QVector<InkStroke>> parseEditData(const QByteArray& bytes, int pageCount)
{
    if (bytes.size() > MaxMetadataBytes)
        fail(QStringLiteral("原生编辑数据过大, 为保护原文件仅可阅读."));
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &error);
    const QJsonObject root = json.object();
    if (error.error != QJsonParseError::NoError || !json.isObject()
        || root.value("application").toString() != QStringLiteral("ProperNotes.Native")
        || root.value("version").toDouble(-1) != FormatVersion
        || !root.value("pages").isArray()
        || root.value("pages").toArray().size() != pageCount)
        fail(QStringLiteral("原生编辑数据损坏或版本不支持, 不允许覆盖保存."));
    QVector<QVector<InkStroke>> result(pageCount);
    QSet<QUuid> ids;
    qsizetype pointCount = 0;
    const QJsonArray pages = root.value("pages").toArray();
    for (int p = 0; p < pageCount; ++p) {
        if (!pages[p].isArray())
            fail(QStringLiteral("原生页笔迹数据无效."));
        for (const auto& value : pages[p].toArray()) {
            if (!value.isObject())
                fail(QStringLiteral("原生笔迹记录无效."));
            const QJsonObject object = value.toObject();
            InkStroke stroke;
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
            if (pointCount > MaxTotalPoints || ids.size() >= MaxStrokes || ids.contains(stroke.id))
                fail(QStringLiteral("原生笔迹超出数量上限或 ID 重复."));
            ids.insert(stroke.id);
            result[p].append(std::move(stroke));
        }
    }
    return result;
}

struct OwnedAnnotation {
    Reference annotation;
    std::optional<Reference> appearance;
};

struct LoadedState {
    pdf::PDFDocument base;
    QVector<QVector<InkStroke>> pages;
    QHash<QUuid, OwnedAnnotation> owned;
    std::optional<Reference> privateData;
    QString readOnlyReason;
};

QByteArray appearanceContent(const InkStroke& stroke, const QRectF& box);

bool annotationMatches(const pdf::PDFDocument& document, const Dictionary& annotation,
                       const InkStroke& stroke)
{
    if (stringValue(document, annotation.get("Subtype")) != "Ink"
        || numberValue(document, annotation.get("PNVersion")) != FormatVersion
        || stringValue(document, annotation.get("NM")) != QByteArray(IdPrefix) + stroke.id.toByteArray(QUuid::WithoutBraces)
        || stringValue(document, annotation.get("PNStrokeSHA256")) != strokeDigest(stroke))
        return false;
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
            || std::abs(x - stroke.points[i].position.x()) > 1e-6
            || std::abs(y - stroke.points[i].position.y()) > 1e-6)
            return false;
    }
    const QRectF box = inkStrokeOutline(stroke).boundingRect();
    const auto& rect = document.getObject(annotation.get("Rect"));
    if (!rect.isArray() || rect.getArray()->getCount() != 4)
        return false;
    const double expected[] = { box.left(), box.top(), box.right(), box.bottom() };
    for (size_t i = 0; i < 4; ++i) {
        const double actual = numberValue(document, rect.getArray()->getItem(i));
        if (!std::isfinite(actual) || std::abs(actual - expected[i]) > 1e-5)
            return false;
    }
    const auto* appearances = document.getDictionaryFromObject(annotation.get("AP"));
    if (!appearances)
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
            if (!std::isfinite(actual) || std::abs(actual - expectedValue) > 1e-6)
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
    for (const char* key : { "ca", "CA" }) {
        const double actual = numberValue(document, alpha->get(key));
        if (!std::isfinite(actual) || std::abs(actual - stroke.color.alphaF()) > 1e-6)
            return false;
    }
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
                || stringValue(state.base, annotation->get("NM")).startsWith(IdPrefix))
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
            || numberValue(state.base, native.getDictionary()->get("Version")) != FormatVersion)
            fail(QStringLiteral("原生编辑数据版本未知, 仅可阅读."));
        const auto& data = state.base.getObject(native.getDictionary()->get("Data"));
        if (!data.isStream())
            fail(QStringLiteral("原生编辑数据流缺失, 仅可阅读."));
        // Version 1 writes an unfiltered JSON stream. Refuse transformed or
        // oversized private streams instead of decoding an unbounded payload.
        if (!data.getStream()->getDictionary()->get("Filter").isNull()
            || data.getStream()->getContent()->size() > MaxMetadataBytes)
            fail(QStringLiteral("此原生编辑数据流需要迁移或超出原型范围, 仅可阅读."));
        const QByteArray decoded = state.base.getDecodedStream(data.getStream());
        if (sha256(decoded).toHex() != stringValue(state.base, native.getDictionary()->get("SHA256")))
            fail(QStringLiteral("原生编辑数据校验失败, 仅可阅读."));
        auto pages = parseEditData(decoded, count);
        QSet<QUuid> expectedIds;
        for (int p = 0; p < count; ++p) {
            QHash<QUuid, InkStroke> expected;
            for (const auto& stroke : pages[p]) {
                expected.insert(stroke.id, stroke);
                expectedIds.insert(stroke.id);
            }
            const auto* page = state.base.getCatalog()->getPage(size_t(p));
            for (const auto& reference : page->getAnnotations()) {
                const auto* annotation = state.base.getDictionaryFromObject(Object::createReference(reference));
                if (!annotation)
                    continue;
                const bool owner = stringValue(state.base, annotation->get("PNOwner")) == OwnerName;
                const bool reserved = stringValue(state.base, annotation->get("NM")).startsWith(IdPrefix);
                if (!owner && !reserved)
                    continue;
                const QUuid id(QString::fromUtf8(stringValue(state.base, annotation->get("PNStrokeID"))));
                if (!owner || id.isNull() || !expected.contains(id) || found.contains(id)
                    || !annotationMatches(state.base, *annotation, expected.value(id)))
                    fail(QStringLiteral("原生 Ink 与编辑数据不一致, 仅可阅读, 不覆盖外部修改."));
                found.insert(id, reference);
                OwnedAnnotation owned { reference, std::nullopt };
                const auto* ap = state.base.getDictionaryFromObject(annotation->get("AP"));
                if (ap && ap->get("N").isReference())
                    owned.appearance = ap->get("N").getReference();
                state.owned.insert(id, owned);
            }
        }
        if (found.size() != expectedIds.size())
            fail(QStringLiteral("部分原生 Ink 缺失, 不用旧元数据覆盖外部修改."));
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
    } catch (const std::exception& exception) {
        state.readOnlyReason = QString::fromUtf8(exception.what());
        state.pages = QVector<QVector<InkStroke>>(count);
        state.owned.clear();
        state.privateData.reset();
    }
    return state;
}

QByteArray real(double value)
{
    return QByteArray::number(std::abs(value) < 1e-12 ? 0.0 : value, 'g', 15);
}

QByteArray appearanceContent(const InkStroke& stroke, const QRectF& box)
{
    const QPainterPath path = inkStrokeOutline(stroke);
    QByteArray bytes = "q\n/PNAlpha gs\n";
    bytes += real(stroke.color.redF()) + ' ' + real(stroke.color.greenF()) + ' '
             + real(stroke.color.blueF()) + " rg\n";
    auto appendPoint = [&](qreal x, qreal y) {
        bytes += real(x - box.left()) + ' ' + real(y - box.top());
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

Object createAppearance(const InkStroke& stroke, const QRectF& box)
{
    DictBuilder alpha;
    put(alpha, "Type", Object::createName(QByteArray("ExtGState")));
    put(alpha, "ca", Object::createReal(stroke.color.alphaF()));
    put(alpha, "CA", Object::createReal(stroke.color.alphaF()));
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
    return streamObject(std::move(form), appearanceContent(stroke, box));
}

pdf::PDFDocument withInk(const LoadedState& state)
{
    pdf::PDFDocumentBuilder builder(&state.base);
    const auto* catalog = catalogDictionary(state.base);
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
        for (const auto& stroke : state.pages[p]) {
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
            put(annotation, "PNVersion", Object::createInteger(FormatVersion));
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
        DictBuilder updated(*dictionary);
        put(updated, "Annots", Object::createArray(std::move(annotations)));
        builder.setObject(page->getPageReference(), Object::createDictionary(std::move(updated)));
    }
    QByteArray data = editData(state.pages);
    if (data.size() > MaxMetadataBytes)
        fail(QStringLiteral("原生编辑数据超出此原型支持范围."));
    DictBuilder dataDictionary;
    const auto dataReference = builder.addObject(streamObject(std::move(dataDictionary), data));
    DictBuilder metadata;
    put(metadata, "Type", Object::createName(QByteArray(NativeKey)));
    put(metadata, "Version", Object::createInteger(FormatVersion));
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
        pdf::PDFDocument output = compactSupersededOwnedObjects(*d->state, withInk(*d->state));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly))
            fail(QStringLiteral("无法创建 PDF 写入缓冲区."));
        pdf::PDFDocumentWriter writer(nullptr);
        const auto writeResult = writer.write(&buffer, &output);
        if (!writeResult)
            fail(writeResult.getErrorMessage());
        buffer.close();
        if (bytes.isEmpty() || bytes.size() > MaxFileBytes)
            fail(QStringLiteral("输出 PDF 大小超出此原型范围, 未覆盖原文件."));
        auto verified = std::make_unique<LoadedState>(loadState(bytes));
        if (!verified->readOnlyReason.isEmpty() || editData(verified->pages) != editData(d->state->pages))
            fail(QStringLiteral("写入后重新读取校验失败, 未覆盖原文件."));
        const QByteArray verifiedHash = sha256(bytes);
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
        d->state = std::move(verified); // Already stripped of owned AP and native catalog data.
        d->path = target;
        d->sourceSha256 = verifiedHash;
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

QVector<InkStroke> PdfDocument::strokes(int index) const
{
    return index >= 0 && index < pageCount() ? d->state->pages[index] : QVector<InkStroke>();
}

bool PdfDocument::setStrokes(int index, const QVector<InkStroke>& strokes)
{
    try {
        if (isReadOnly() || index < 0 || index >= pageCount())
            fail(isReadOnly() ? readOnlyReason() : QStringLiteral("页面索引无效."));
        QSet<QUuid> ids;
        qsizetype count = 0;
        for (int p = 0; p < pageCount(); ++p) {
            const auto& page = p == index ? strokes : d->state->pages[p];
            for (const auto& stroke : page) {
                validateStroke(stroke);
                if (ids.contains(stroke.id) || ids.size() >= MaxStrokes)
                    fail(QStringLiteral("笔迹 ID 重复或笔迹数量超出上限."));
                ids.insert(stroke.id);
                count += stroke.points.size();
                if (count > MaxTotalPoints)
                    fail(QStringLiteral("笔迹采样数量超出此原型上限."));
            }
        }
        QJsonArray oldState, newState;
        for (const auto& stroke : d->state->pages[index]) oldState.append(strokeJson(stroke));
        for (const auto& stroke : strokes) newState.append(strokeJson(stroke));
        if (oldState != newState) {
            d->state->pages[index] = strokes;
            d->modified = true;
        }
        d->error.clear();
        return true;
    } catch (const std::exception& exception) {
        d->error = QString::fromUtf8(exception.what());
        return false;
    }
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
