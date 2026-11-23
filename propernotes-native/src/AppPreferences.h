// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>
#include "ToolPreferences.h"
#include "FavoriteTools.h"

namespace propernotes {

struct AppPreferenceValues {
    QString theme = QStringLiteral("system");
    bool sidebarVisible = true;
    int sidebarWidth = 160;
    QColor penColor = QColor(QStringLiteral("#202833"));
    qreal penWidthPt = 2.5;
    ToolPreferences tools;
    bool editTextMarkups = true;
    QVector<FavoriteTool> favorites = defaultFavoriteTools();
    FavoritesBarPreferences favoritesBar;
    int backupIntervalMinutes = 5;
    int backupRetainCount = 10;
    // The literal Backups entry moves with the portable program directory.
    QString backupDirectory = QStringLiteral("Backups");
    QStringList backupDirectories = {QStringLiteral("Backups")};
};

// Owned by the PDF/file worker. The trusted application root is resolved once;
// settings leaf links remain forbidden.
class AppPreferences final {
public:
    explicit AppPreferences(QString applicationDirectory);
    bool load();
    bool save(const AppPreferenceValues& values);
    const AppPreferenceValues& values() const { return m_values; }
    bool canWrite() const { return m_loaded && m_writable; }
    QString lastError() const { return m_error; }
    QString filePath() const;
    QString resolveDirectory(const QString& stored) const;
    QString normalizeDirectory(const QString& selected) const;
    static bool validate(const AppPreferenceValues& values, QString* error = nullptr);

private:
    bool readCurrent(bool* exists, QByteArray* bytes, QString* error) const;
    bool baselineUnchanged(QString* error) const;
    QString m_applicationDirectory;
    QString m_applicationDirectoryAlias;
    AppPreferenceValues m_values;
    QByteArray m_baselineHash;
    bool m_baselineExists = false;
    bool m_loaded = false;
    bool m_writable = false;
    QString m_error;
};

} // namespace propernotes
