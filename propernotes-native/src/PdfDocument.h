// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"

#include <QRectF>
#include <QByteArray>
#include <QSizeF>
#include <memory>
#include <functional>

namespace propernotes {

// One worker thread must own and call each instance. Returned QImage and ink
// values are detached snapshots suitable for transfer to the GUI thread.
class PdfDocument final {
public:
    PdfDocument();
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    // Transactional: an unsuccessful open leaves the current document intact.
    bool open(const QString& path);
    // Empty path means the current file. Performs conflict checks and an atomic
    // commit; protected or unrecognised editing data is never overwritten.
    bool save(const QString& path = {}, const QByteArray& expectedTargetSha256 = {});
    static QByteArray fileSha256(const QString& path, QString* error = nullptr);
    // Creates a complete recovery PDF at a new path without changing the loaded
    // base, currentPath, source fingerprint, dirty flag or lastError. No source
    // file or existing destination is overwritten. Error belongs to this call.
    bool writeRecoveryCopy(const QString& path, QString* error = nullptr,
                           QByteArray* writtenSha256 = nullptr) const;
    QByteArray recoveryFingerprint() const;

    bool hasDocument() const;
    bool isReadOnly() const;
    QString readOnlyReason() const;
    QString lastError() const;
    QString currentPath() const;
    bool isModified() const;

    int pageCount() const;
    QSizeF pageSize(int pageIndex) const;
    QRectF pageRect(int pageIndex) const;
    RenderedPage renderPage(int pageIndex, qreal pixelsPerPoint);
    QVector<InkStroke> strokes(int pageIndex) const;
    bool setStrokes(int pageIndex, const QVector<InkStroke>& strokes);

private:
    friend class BackupStore;
    // Storage claims the known digest/size in its journal before the payload
    // commit. Returning false from claim cancels an identical snapshot.
    bool writeRecoveryCopyClaimed(const QString& path,
        const std::function<bool(const QByteArray&, qint64)>& claim,
        QString* error, QByteArray* writtenSha256) const;
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace propernotes
