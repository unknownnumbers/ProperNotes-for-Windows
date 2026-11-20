// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"

#include <pdfcatalog.h>
#include <pdfcms.h>
#include <pdfcolorspaces.h>
#include <pdfdocument.h>
#include <pdfexception.h>
#include <pdffont.h>
#include <pdfmeshqualitysettings.h>
#include <pdfoperationcontrol.h>
#include <pdfoptionalcontent.h>
#include <pdfpage.h>
#include <pdfpagecontentprocessor.h>
#include <pdfsecurityhandler.h>
#include <pdftextlayout.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace propernotes {
namespace {

constexpr int MaxPageCharacters = 50000;

bool isCancelled(const std::atomic_bool* flag)
{
    return flag && flag->load(std::memory_order_relaxed);
}

class TextExtractionControl final : public pdf::PDFOperationControl {
public:
    explicit TextExtractionControl(const std::atomic_bool* cancelled) : m_cancelled(cancelled) {}

    bool isOperationCancelled() const override
    {
        return isCancelled(m_cancelled) || m_characterLimit.load(std::memory_order_relaxed);
    }

    void stopAtCharacterLimit() { m_characterLimit.store(true, std::memory_order_relaxed); }
    bool characterLimitReached() const { return m_characterLimit.load(std::memory_order_relaxed); }

private:
    const std::atomic_bool* m_cancelled;
    std::atomic_bool m_characterLimit{false};
};

qreal length(const QPointF& point)
{
    return std::hypot(point.x(), point.y());
}

QRectF bounds(const TextMarkupQuad& quad)
{
    const qreal left = std::min({quad.topLeft.x(), quad.topRight.x(), quad.bottomLeft.x(), quad.bottomRight.x()});
    const qreal right = std::max({quad.topLeft.x(), quad.topRight.x(), quad.bottomLeft.x(), quad.bottomRight.x()});
    const qreal bottom = std::min({quad.topLeft.y(), quad.topRight.y(), quad.bottomLeft.y(), quad.bottomRight.y()});
    const qreal top = std::max({quad.topLeft.y(), quad.topRight.y(), quad.bottomLeft.y(), quad.bottomRight.y()});
    return QRectF(QPointF(left, bottom), QPointF(right, top));
}

// The corners follow the text baseline and its ascender direction, which are
// distinct from QRectF's numeric top/bottom in PDF user space.
TextMarkupQuad cardinalQuad(const QRectF& rect, const QPointF& right, const QPointF& up)
{
    if (std::abs(right.x()) >= std::abs(right.y())) {
        const qreal start = right.x() >= 0 ? rect.left() : rect.right();
        const qreal end = right.x() >= 0 ? rect.right() : rect.left();
        const qreal top = up.y() >= 0 ? rect.bottom() : rect.top();
        const qreal bottom = up.y() >= 0 ? rect.top() : rect.bottom();
        return {{start, top}, {end, top}, {start, bottom}, {end, bottom}};
    }
    const qreal start = right.y() >= 0 ? rect.top() : rect.bottom();
    const qreal end = right.y() >= 0 ? rect.bottom() : rect.top();
    const qreal top = up.x() >= 0 ? rect.right() : rect.left();
    const qreal bottom = up.x() >= 0 ? rect.left() : rect.right();
    return {{top, start}, {top, end}, {bottom, start}, {bottom, end}};
}

class PageTextCollector final : public pdf::PDFPageContentProcessor {
public:
    PageTextCollector(const pdf::PDFPage* page, const pdf::PDFDocument* document, const pdf::PDFFontCache* fonts,
                      const pdf::PDFICCProfileCache* profiles, const pdf::PDFCMS* cms,
                      const pdf::PDFOptionalContentActivity* optional, const pdf::PDFMeshQualitySettings& mesh,
                      TextExtractionControl& control)
        : PDFPageContentProcessor(page, document, fonts, profiles, cms, optional, QTransform(), mesh),
          m_control(control), m_crop(page->getCropBox())
    {
        // Identity device mapping keeps content in PDF user space. ProperNotes
        // applies crop, UserUnit, page rotation and the screen Y flip at display time.
        setOperationControl(&m_control);
        m_result.glyphs.reserve(2048);
        m_result.text.reserve(2304);
    }

    PdfTextPage takePage()
    {
        m_result.truncated |= m_control.characterLimitReached();
        return std::move(m_result);
    }

private:
    bool isContentKindSuppressed(ContentKind kind) const override
    {
        // Form XObjects can hold ordinary page text. Ignore graphics, raster
        // images and repeating background patterns during text extraction.
        return kind != ContentKind::Forms;
    }

    void performOutputCharacter(const pdf::PDFTextCharacterInfo& info) override
    {
        if (m_control.isOperationCancelled() || isContentSuppressed())
            return;
        if (++m_characterCount > MaxPageCharacters) {
            m_control.stopAtCharacterLimit();
            return;
        }
        if (info.character.isSpace()) {
            m_pendingSpace = true;
            return;
        }
        if (info.character.isNull() || info.character.category() == QChar::Other_Control)
            return;

        const bool vertical = info.isVerticalWritingSystem;
        const QTransform& transform = info.matrix;
        const QPointF origin = transform.map(QPointF());
        const QPointF right = transform.map(QPointF(1, 0)) - origin;
        const QPointF up = transform.map(QPointF(0, 1)) - origin;
        const qreal rightScale = length(right);
        const qreal upScale = length(up);
        if (!transform.isAffine() || !std::isfinite(rightScale) || !std::isfinite(upScale)
            || rightScale < 1e-9 || upScale < 1e-9 || !std::isfinite(info.fontSize) || !std::isfinite(info.advance)) {
            m_result.truncated = true;
            return;
        }

        const qreal type3Em = info.outline ? 0 : type3EmHeight(info.advance);
        const qreal em = info.outline ? std::abs(info.fontSize) : (type3Em > 0 ? type3Em : 1000);
        if (em <= 0)
            return;
        const QRectF ink = info.outline ? info.outline->getBoundingRect() : QRectF();
        const qreal localAdvance = std::abs(info.advance);
        // Horizontal rows use advance for selection through narrow glyphs, plus
        // the actual outline for overhangs and accent marks. Type 3 uses
        // nominal bounds because its glyphs are PDF content streams.
        const qreal leftEdge = ink.isValid() ? std::min(qreal(0), ink.left()) : 0;
        const qreal rightEdge = std::max({em * 0.12, vertical ? em * 0.65 : localAdvance,
                                           ink.isValid() ? ink.right() : qreal(0)});
        const qreal lowEdge = std::min(-em * 0.20, ink.isValid() ? ink.top() : qreal(0));
        const qreal highEdge = std::max(em * 0.80, ink.isValid() ? ink.bottom() : qreal(0));
        TextMarkupQuad quad{transform.map(QPointF(leftEdge, highEdge)),
                            transform.map(QPointF(rightEdge, highEdge)),
                            transform.map(QPointF(leftEdge, lowEdge)),
                            transform.map(QPointF(rightEdge, lowEdge))};
        if (!validTextMarkupQuad(quad)) {
            m_result.truncated = true;
            return;
        }
        const QRectF glyphBounds = bounds(quad);
        if (!m_crop.intersects(glyphBounds))
            return;

        const QPointF rightAxis = right / rightScale;
        const QPointF upAxis = up / upScale;
        const QPointF axis = vertical ? upAxis : rightAxis;
        const qreal height = em * (vertical ? rightScale : upScale);
        const qreal advance = localAdvance * (vertical ? upScale : rightScale);
        bool canReview = false;
        // Slanted ascenders, such as synthetic italics, don't rotate the
        // baseline. Use the visible bounds in the baseline's direction when
        // the original quad is sheared or only partially inside CropBox.
        const qreal cross = rightAxis.x() * upAxis.y() - rightAxis.y() * upAxis.x();
        if (!vertical && (info.outline || type3Em > 0) && nearlyAxisAligned(rightAxis) && std::abs(cross) >= 0.8) {
            const bool inside = m_crop.contains(glyphBounds);
            if (inside && nearlyAxisAligned(upAxis) && std::abs(QPointF::dotProduct(rightAxis, upAxis)) < 0.0175) {
                canReview = true;
            } else {
                QRectF visible = glyphBounds;
                if (!inside) {
                    QPolygonF polygon;
                    polygon << quad.topLeft << quad.topRight << quad.bottomRight << quad.bottomLeft;
                    visible = polygon.intersected(QPolygonF(m_crop)).boundingRect().intersected(m_crop);
                }
                const TextMarkupQuad bounded = cardinalQuad(visible, rightAxis, upAxis);
                if (validTextMarkupQuad(bounded)) {
                    quad = bounded;
                    canReview = true;
                }
            }
        }
        appendCharacter(info.character, origin, axis, height, advance, quad, canReview);
    }

    qreal type3EmHeight(qreal advance) const
    {
        if (advance <= 0)
            return 0;
        const auto* type3 = dynamic_cast<const pdf::PDFType3Font*>(getGraphicState()->getTextFont().get());
        if (!type3)
            return 0;
        const QTransform& fontMatrix = type3->getFontMatrix();
        const qreal x = fontMatrix.m11();
        const qreal y = fontMatrix.m22();
        if (x <= 0 || y <= 0 || std::abs(fontMatrix.m12()) > x * 0.001 || std::abs(fontMatrix.m21()) > y * 0.001)
            return 0;
        const qreal em = 1 / y;
        return std::isfinite(em) ? em : 0;
    }

    static bool nearlyAxisAligned(const QPointF& unit)
    {
        return std::max(std::abs(unit.x()), std::abs(unit.y())) >= 0.99985;
    }

    void appendCharacter(QChar character, const QPointF& origin, const QPointF& axis, qreal height,
                         qreal advance, const TextMarkupQuad& quad, bool canReview)
    {
        // Preserve PDF content order. Infer separators from the actual origins,
        // retaining CJK adjacency without adding a space between each glyph.
        if (!m_result.glyphs.isEmpty()) {
            const QPointF delta = origin - m_previousOrigin;
            const QPointF normal(-m_lineAxis.y(), m_lineAxis.x());
            const qreal step = std::abs(QPointF::dotProduct(delta, m_lineAxis));
            const qreal nominalHeight = std::max(height, m_previousHeight);
            const bool sameLine = QPointF::dotProduct(axis, m_lineAxis) >= 0.9994
                && std::abs(QPointF::dotProduct(origin - m_lineOrigin, normal)) <= nominalHeight * 0.42
                && step <= m_previousAdvance + nominalHeight * 4;
            if (!sameLine) {
                m_result.text.append(QLatin1Char('\n'));
                ++m_line;
                m_lineOrigin = origin;
                m_lineAxis = axis;
            } else if (m_pendingSpace || (m_previousAdvance > 0
                       && step - m_previousAdvance > std::max(qreal(0.55), nominalHeight * 0.18))) {
                m_result.text.append(QLatin1Char(' '));
            }
        } else {
            m_lineOrigin = origin;
            m_lineAxis = axis;
        }

        // The pinned callback carries one QChar. A replacement character keeps
        // copying valid UTF-16; the separately verified quad can still be marked.
        if (character.isSurrogate()) character = QChar(QChar::ReplacementCharacter);
        const int offset = int(m_result.text.size());
        m_result.text.append(character);
        m_result.glyphs.append({offset, 1, quad, canReview, m_line});
        m_previousOrigin = origin;
        m_previousAdvance = advance;
        m_previousHeight = height;
        m_pendingSpace = false;
    }

    TextExtractionControl& m_control;
    QRectF m_crop;
    PdfTextPage m_result;
    int m_characterCount = 0;
    int m_line = 0;
    QPointF m_lineOrigin;
    QPointF m_lineAxis;
    QPointF m_previousOrigin;
    qreal m_previousAdvance = 0;
    qreal m_previousHeight = 0;
    bool m_pendingSpace = false;
};

} // namespace

PdfTextPage PdfDocument::extractTextPage(int pageIndex, const std::atomic_bool* cancelled) const
{
    PdfTextPage result;
    pdf::PDFDocument* document = textSourceDocument();
    if (!document || pageIndex < 0 || pageIndex >= pageCount()) {
        result.error = QStringLiteral("文本页码无效。");
        return result;
    }
    if (isCancelled(cancelled))
        return result;
    const auto* security = document->getStorage().getSecurityHandler();
    if (security && !security->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent)) {
        result.error = QStringLiteral("此 PDF 的权限不允许提取正文，无法选择、复制或搜索文字。");
        return result;
    }

    try {
        const auto* page = document->getCatalog()->getPage(size_t(pageIndex));
        if (page->getContents().isNull())
            return result;
        pdf::PDFOptionalContentActivity optional(document, pdf::OCUsage::View, nullptr);
        pdf::PDFModifiedDocument modified(document, &optional);
        pdf::PDFFontCache fonts(256, 512);
        fonts.setDocument(modified);
        pdf::PDFICCProfileCache profiles;
        profiles.setDocument(document);
        pdf::PDFCMSManager cms(nullptr);
        cms.setDocument(document);
        cms.setSettings(cms.getDefaultSettings());
        const auto colors = cms.getCurrentCMS();
        const pdf::PDFMeshQualitySettings mesh;
        TextExtractionControl control(cancelled);
        PageTextCollector collector(page, document, &fonts, &profiles, colors.data(), &optional, mesh, control);
        const auto errors = collector.processContents();
        if (isCancelled(cancelled))
            return {};
        if (!control.characterLimitReached()) {
            for (const auto& error : errors) {
                if (error.type == pdf::RenderErrorType::Error) {
                    result.error = QStringLiteral("页面文字提取失败：") + error.message.left(1024);
                    return result;
                }
            }
        }
        return collector.takePage();
    } catch (const pdf::PDFException& exception) {
        result.error = exception.getMessage().left(1024);
    } catch (const pdf::PDFRendererException& exception) {
        result.error = exception.getError().message.left(1024);
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what()).left(1024);
    }
    if (result.error.isEmpty())
        result.error = QStringLiteral("页面文字提取失败。");
    return result;
}

} // namespace propernotes
