// SPDX-License-Identifier: GPL-3.0-only
#include "SettingsDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace propernotes {
namespace {
QString bytesText(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QString::number(double(bytes) / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" GiB");
    if (bytes >= 1024 * 1024)
        return QString::number(double(bytes) / (1024 * 1024), 'f', 1) + QStringLiteral(" MiB");
    return QString::number(double(bytes) / 1024, 'f', 1) + QStringLiteral(" KiB");
}
} // namespace

SettingsDialog::SettingsDialog(const AppPreferenceValues& value, const QString& applicationDirectory,
                               const QString& preferencesWarning, bool preferencesWritable, QWidget* parent)
    : QDialog(parent), m_initial(value), m_penColor(value.penColor), m_preferencesWritable(preferencesWritable)
{
    setWindowTitle(QStringLiteral("ProperNotes 设置与本地备份"));
    resize(940, 700);
    setMinimumSize(720, 540);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    m_tabs = tabs;
    auto* settings = new QWidget;
    auto* settingsLayout = new QVBoxLayout(settings);
    if (!preferencesWarning.isEmpty()) {
        auto* warning = new QLabel(preferencesWarning);
        warning->setTextFormat(Qt::PlainText);
        warning->setWordWrap(true);
        settingsLayout->addWidget(warning);
    }
    auto* appearance = new QGroupBox(QStringLiteral("已接入的界面与笔设置"));
    auto* form = new QFormLayout(appearance);
    m_theme = new QComboBox;
    m_theme->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    m_theme->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    m_theme->setCurrentIndex(m_theme->findData(value.theme));
    form->addRow(QStringLiteral("主题"), m_theme);
    m_sidebar = new QCheckBox(QStringLiteral("显示页面侧栏"));
    m_sidebar->setChecked(value.sidebarVisible);
    form->addRow(m_sidebar);
    m_sidebarWidth = new QSpinBox;
    m_sidebarWidth->setRange(112, 400);
    m_sidebarWidth->setSuffix(QStringLiteral(" 像素"));
    m_sidebarWidth->setValue(value.sidebarWidth);
    form->addRow(QStringLiteral("侧栏宽度"), m_sidebarWidth);
    m_color = new QPushButton(value.penColor.name());
    m_color->setStyleSheet(QStringLiteral("color:%1;").arg(value.penColor.name()));
    form->addRow(QStringLiteral("笔颜色"), m_color);
    m_penWidth = new QDoubleSpinBox;
    m_penWidth->setRange(0.5, 24.0);
    m_penWidth->setSingleStep(0.5);
    m_penWidth->setSuffix(QStringLiteral(" pt"));
    m_penWidth->setValue(value.penWidthPt);
    form->addRow(QStringLiteral("笔粗细"), m_penWidth);
    appearance->setEnabled(preferencesWritable);
    settingsLayout->addWidget(appearance);

    auto* backup = new QGroupBox(QStringLiteral("本地备份"));
    auto* backupForm = new QFormLayout(backup);
    m_interval = new QSpinBox;
    m_interval->setRange(1, 1440);
    m_interval->setSuffix(QStringLiteral(" 分钟"));
    m_interval->setValue(value.backupIntervalMinutes);
    backupForm->addRow(QStringLiteral("检查间隔"), m_interval);
    m_retain = new QSpinBox;
    m_retain->setRange(0, 1000);
    m_retain->setSuffix(QStringLiteral(" 份 / PDF"));
    m_retain->setValue(value.backupRetainCount);
    backupForm->addRow(QStringLiteral("保留数量"), m_retain);
    auto* directoryRow = new QWidget;
    auto* directoryLayout = new QHBoxLayout(directoryRow);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    m_directory = new QLineEdit(value.backupDirectory == QStringLiteral("Backups")
        ? QDir(applicationDirectory).filePath(QStringLiteral("Backups")) : value.backupDirectory);
    auto* browse = new QPushButton(QStringLiteral("选择…"));
    directoryLayout->addWidget(m_directory, 1);
    directoryLayout->addWidget(browse);
    backupForm->addRow(QStringLiteral("当前备份目录"), directoryRow);
    m_migrate = new QCheckBox(QStringLiteral("更换目录时迁移当前目录中已有的备份"));
    m_migrate->setChecked(false);
    backupForm->addRow(m_migrate);
    auto* explanation = new QLabel(QStringLiteral("只在内容变化后新增备份。数量 0 停止新增，已有备份保留。默认仅改变后续位置，旧目录仍登记并可管理。迁移会先复制校验，再清理匹配的旧副本。"));
    explanation->setWordWrap(true);
    backupForm->addRow(explanation);
    backup->setEnabled(preferencesWritable);
    settingsLayout->addWidget(backup);
    m_reload = new QPushButton(QStringLiteral("重新读取磁盘设置"));
    settingsLayout->addWidget(m_reload, 0, Qt::AlignLeft);
    settingsLayout->addStretch();
    tabs->addTab(settings, QStringLiteral("设置"));

    auto* manager = new QWidget;
    auto* managerLayout = new QVBoxLayout(manager);
    auto* hint = new QLabel(QStringLiteral("管理已保存设置中的全部登记目录。恢复先校验副本，另存为新文件后打开，不覆盖原 PDF。目录设置修改须先应用。"));
    hint->setWordWrap(true);
    managerLayout->addWidget(hint);
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({QStringLiteral("PDF / 备份时间"), QStringLiteral("原路径"), QStringLiteral("份数"), QStringLiteral("占用空间"), QStringLiteral("目录 / 状态")});
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    managerLayout->addWidget(m_tree, 1);
    auto* operations = new QHBoxLayout;
    m_refresh = new QPushButton(QStringLiteral("刷新"));
    m_now = new QPushButton(QStringLiteral("备份当前 PDF"));
    m_current = new QPushButton(QStringLiteral("清理当前 PDF"));
    m_selected = new QPushButton(QStringLiteral("清理勾选 PDF"));
    m_selectAll = new QPushButton(QStringLiteral("全选 PDF"));
    m_all = new QPushButton(QStringLiteral("清理全部 PDF"));
    for (auto* button : {m_refresh, m_now, m_current, m_selected, m_selectAll, m_all})
        operations->addWidget(button);
    managerLayout->addLayout(operations);
    m_restore = new QPushButton(QStringLiteral("校验并另存所选备份…"));
    managerLayout->addWidget(m_restore, 0, Qt::AlignLeft);
    tabs->addTab(manager, QStringLiteral("备份管理"));
    layout->addWidget(tabs, 1);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用设置"));
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(preferencesWritable);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_color, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(m_penColor, this, QStringLiteral("笔颜色"));
        if (selected.isValid()) {
            m_penColor = selected;
            m_color->setText(selected.name());
            m_color->setStyleSheet(QStringLiteral("color:%1;").arg(selected.name()));
        }
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("备份目录"), m_directory->text());
        if (!selected.isEmpty())
            m_directory->setText(selected);
    });
    connect(m_refresh, &QPushButton::clicked, this, &SettingsDialog::refreshRequested);
    connect(m_now, &QPushButton::clicked, this, &SettingsDialog::createBackupRequested);
    connect(m_reload, &QPushButton::clicked, this, &SettingsDialog::reloadPreferencesRequested);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SettingsDialog::refreshButtons);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this] { refreshButtons(); });
    connect(m_selectAll, &QPushButton::clicked, this, [this] {
        for (int index = 0; index < m_tree->topLevelItemCount(); ++index)
            m_tree->topLevelItem(index)->setCheckState(0, Qt::Checked);
    });
    connect(m_selected, &QPushButton::clicked, this, [this] { Q_EMIT cleanupRequested(checkedGroups()); });
    connect(m_all, &QPushButton::clicked, this, [this] {
        QVector<BackupGroupKey> keys;
        for (const auto& group : m_catalog.groups)
            keys.append(group.key());
        Q_EMIT cleanupRequested(keys);
    });
    connect(m_current, &QPushButton::clicked, this, [this] {
        QVector<BackupGroupKey> keys;
        for (const auto& group : m_catalog.groups) {
            if (group.documentId == m_currentDocumentId)
                keys.append(group.key());
        }
        Q_EMIT cleanupRequested(keys);
    });
    connect(m_restore, &QPushButton::clicked, this, [this] {
        auto* item = m_tree->currentItem();
        if (!item || !item->parent())
            return;
        const int groupIndex = item->data(0, Qt::UserRole).toInt();
        const int snapshotIndex = item->data(0, Qt::UserRole + 1).toInt();
        if (groupIndex < 0 || groupIndex >= m_catalog.groups.size())
            return;
        const auto& group = m_catalog.groups[groupIndex];
        if (snapshotIndex < 0 || snapshotIndex >= group.snapshots.size() || !group.snapshots[snapshotIndex].available)
            return;
        QString name = group.fileName;
        if (name.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
            name.chop(4);
        name += QStringLiteral(".恢复-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".pdf");
        Q_EMIT restoreRequested({group.rootPath, group.rootId, group.documentId, group.snapshots[snapshotIndex].id}, name);
    });
    refreshButtons();
}

AppPreferenceValues SettingsDialog::values() const
{
    AppPreferenceValues result = m_initial;
    result.theme = m_theme->currentData().toString();
    result.sidebarVisible = m_sidebar->isChecked();
    result.sidebarWidth = m_sidebarWidth->value();
    result.penColor = m_penColor;
    result.penWidthPt = m_penWidth->value();
    result.backupIntervalMinutes = m_interval->value();
    result.backupRetainCount = m_retain->value();
    result.backupDirectory = m_directory->text().trimmed();
    return result;
}

bool SettingsDialog::migrateRequested() const { return m_migrate->isChecked(); }

void SettingsDialog::showBackupManager() { m_tabs->setCurrentIndex(1); }

void SettingsDialog::setCatalog(const BackupCatalog& catalog, const QString& currentDocumentId)
{
    m_catalog = catalog;
    m_currentDocumentId = currentDocumentId;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    for (qsizetype index = 0; index < catalog.groups.size(); ++index) {
        const auto& group = catalog.groups[index];
        auto* row = new QTreeWidgetItem(m_tree);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(0, Qt::Unchecked);
        row->setData(0, Qt::UserRole, int(index));
        row->setText(0, group.fileName);
        row->setText(1, group.sourcePath);
        row->setText(2, QString::number(group.snapshots.size()));
        row->setText(3, bytesText(group.totalBytes));
        row->setText(4, group.rootPath);
        row->setToolTip(1, group.sourcePath);
        row->setToolTip(4, group.rootPath);
        for (qsizetype snapIndex = 0; snapIndex < group.snapshots.size(); ++snapIndex) {
            const auto& snapshot = group.snapshots[snapIndex];
            auto* entry = new QTreeWidgetItem(row);
            entry->setText(0, snapshot.createdUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            entry->setText(3, bytesText(snapshot.size));
            entry->setText(4, snapshot.available ? QStringLiteral("可校验恢复") : snapshot.problem);
            entry->setToolTip(4, snapshot.problem);
            entry->setData(0, Qt::UserRole, int(index));
            entry->setData(0, Qt::UserRole + 1, int(snapIndex));
        }
    }
    m_tree->setUpdatesEnabled(true);
    QStringList notices = catalog.warnings;
    for (const auto& root : catalog.roots) {
        if (!root.error.isEmpty())
            notices.append(root.rootPath + QStringLiteral(": ") + root.error);
    }
    setStatus(notices.isEmpty() ? QStringLiteral("共 %1 份 PDF 的备份；登记目录 %2 个。").arg(catalog.groups.size()).arg(catalog.roots.size()) : notices.join(QLatin1Char('\n')));
    refreshButtons();
}

QVector<BackupGroupKey> SettingsDialog::checkedGroups() const
{
    QVector<BackupGroupKey> keys;
    for (int index = 0; index < m_tree->topLevelItemCount(); ++index) {
        auto* item = m_tree->topLevelItem(index);
        const int groupIndex = item->data(0, Qt::UserRole).toInt();
        if (item->checkState(0) == Qt::Checked && groupIndex >= 0 && groupIndex < m_catalog.groups.size())
            keys.append(m_catalog.groups[groupIndex].key());
    }
    return keys;
}

void SettingsDialog::setCanBackupNow(bool enabled)
{
    m_canBackupNow = enabled;
    refreshButtons();
}

void SettingsDialog::setStatus(const QString& message) { m_status->setText(message); }

void SettingsDialog::setBusy(bool busy, const QString& message)
{
    m_busy = busy;
    if (!message.isEmpty())
        setStatus(message);
    refreshButtons();
}

void SettingsDialog::refreshButtons()
{
    const bool ready = !m_busy;
    m_refresh->setEnabled(ready);
    m_now->setEnabled(ready && m_canBackupNow);
    m_reload->setEnabled(ready);
    bool hasCurrent = false;
    for (const auto& group : m_catalog.groups)
        hasCurrent |= !m_currentDocumentId.isEmpty() && group.documentId == m_currentDocumentId;
    m_current->setEnabled(ready && hasCurrent);
    m_selected->setEnabled(ready && !checkedGroups().isEmpty());
    m_all->setEnabled(ready && !m_catalog.groups.isEmpty());
    m_selectAll->setEnabled(ready && !m_catalog.groups.isEmpty());
    auto* item = m_tree->currentItem();
    bool restorable = false;
    if (item && item->parent()) {
        const int group = item->data(0, Qt::UserRole).toInt();
        const int snapshot = item->data(0, Qt::UserRole + 1).toInt();
        restorable = group >= 0 && group < m_catalog.groups.size() && snapshot >= 0 &&
                     snapshot < m_catalog.groups[group].snapshots.size() && m_catalog.groups[group].snapshots[snapshot].available;
    }
    m_restore->setEnabled(ready && restorable);
    m_tree->setEnabled(ready);
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(ready && m_preferencesWritable);
    m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(ready);
}

void SettingsDialog::reject()
{
    if (!m_busy)
        QDialog::reject();
}

void SettingsDialog::closeEvent(QCloseEvent* event)
{
    if (m_busy)
        event->ignore();
    else
        QDialog::closeEvent(event);
}

} // namespace propernotes
