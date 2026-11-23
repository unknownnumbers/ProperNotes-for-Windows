// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AppPreferences.h"
#include "BackupStore.h"
#include <QDialog>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTabWidget;

namespace propernotes {

class SettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    static constexpr int OpenRecoveredCopy = 2;
    explicit SettingsDialog(const AppPreferenceValues& values, const QString& applicationDirectory,
                            const QString& preferencesWarning, bool preferencesWritable, QWidget* parent = nullptr);
    AppPreferenceValues values() const;
    bool migrateRequested() const;
    void setCatalog(const BackupCatalog& catalog, const QString& currentDocumentId);
    void setBusy(bool busy, const QString& message = {});
    void setStatus(const QString& message);
    void setCanBackupNow(bool enabled);
    void showBackupManager();

signals:
    void refreshRequested();
    void createBackupRequested();
    void cleanupRequested(const QVector<propernotes::BackupGroupKey>& groups);
    void restoreRequested(const propernotes::BackupSnapshotKey& snapshot, const QString& suggestedName);
    void reloadPreferencesRequested();

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    QVector<BackupGroupKey> checkedGroups() const;
    void refreshButtons();
    AppPreferenceValues m_initial;
    BackupCatalog m_catalog;
    QString m_currentDocumentId;
    QColor m_penColor;
    bool m_busy = false;
    bool m_preferencesWritable = false;
    bool m_canBackupNow = false;
    QComboBox* m_theme = nullptr;
    QCheckBox* m_sidebar = nullptr;
    QSpinBox* m_sidebarWidth = nullptr;
    QPushButton* m_color = nullptr;
    QDoubleSpinBox* m_penWidth = nullptr;
    QCheckBox* m_editTextMarkups = nullptr;
    QSpinBox* m_interval = nullptr;
    QSpinBox* m_retain = nullptr;
    QLineEdit* m_directory = nullptr;
    QCheckBox* m_migrate = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_now = nullptr;
    QPushButton* m_current = nullptr;
    QPushButton* m_selected = nullptr;
    QPushButton* m_all = nullptr;
    QPushButton* m_selectAll = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_reload = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QTabWidget* m_tabs = nullptr;
};

} // namespace propernotes
