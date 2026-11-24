// SPDX-License-Identifier: GPL-3.0-only
#include "SettingsDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPair>
#include <QScrollArea>
#include <QSpinBox>
#include <QSize>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

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
    setWindowTitle(QStringLiteral("ProperNotes 设置"));
    resize(940, 700);
    setMinimumSize(720, 540);
    auto* layout = new QVBoxLayout(this);
    if (!preferencesWarning.isEmpty()) {
        auto* warning = new QLabel(preferencesWarning);
        warning->setTextFormat(Qt::PlainText);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }
    auto* body = new QHBoxLayout;
    m_categories = new QListWidget;
    m_categories->setAccessibleName(QStringLiteral("设置分类"));
    m_categories->setMinimumWidth(138);
    m_categories->setMaximumWidth(180);
    m_categories->setSpacing(3);
    m_pages = new QStackedWidget;
    body->addWidget(m_categories);
    body->addWidget(m_pages, 1);
    layout->addLayout(body, 1);
    auto addPage = [this](const QString& title) {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget;
        auto* content = new QVBoxLayout(page);
        content->setContentsMargins(16, 12, 16, 12);
        content->setSpacing(16);
        scroll->setWidget(page);
        m_categories->addItem(title);
        m_categories->item(m_categories->count() - 1)->setSizeHint(QSize(136, 44));
        m_pages->addWidget(scroll);
        return content;
    };
    auto* appearancePage = addPage(QStringLiteral("外观与界面"));
    auto* appearance = new QGroupBox(QStringLiteral("外观与界面"));
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
    m_favoritesVisible = new QCheckBox(QStringLiteral("显示收藏工具栏"));
    m_favoritesVisible->setChecked(value.favoritesBar.visible);
    form->addRow(m_favoritesVisible);
    m_favoritesOrientation = new QComboBox;
    m_favoritesOrientation->addItem(QStringLiteral("垂直"),QStringLiteral("vertical"));
    m_favoritesOrientation->addItem(QStringLiteral("水平"),QStringLiteral("horizontal"));
    m_favoritesOrientation->setCurrentIndex(m_favoritesOrientation->findData(value.favoritesBar.orientation));
    m_favoritesOrientation->setAccessibleName(QStringLiteral("收藏工具栏方向"));
    form->addRow(QStringLiteral("收藏工具栏方向"),m_favoritesOrientation);
    auto* orientationHelp = new QLabel(QStringLiteral("垂直与水平分别记住位置和长度。解除固定后，拖动垂直栏的上下边缘或水平栏的左右边缘调整长度。"));
    orientationHelp->setWordWrap(true);
    form->addRow(orientationHelp);
    m_favoritesPinned = new QCheckBox(QStringLiteral("固定收藏工具栏位置与长度"));
    m_favoritesPinned->setChecked(value.favoritesBar.pinned);
    form->addRow(m_favoritesPinned);
    m_hideDelay = new QSpinBox;
    m_hideDelay->setRange(1, 30);
    m_hideDelay->setSuffix(QStringLiteral(" 秒"));
    m_hideDelay->setValue(value.controlsHideDelaySeconds);
    m_hideDelay->setAccessibleName(QStringLiteral("底角控件空闲隐藏时间"));
    form->addRow(QStringLiteral("底角控件空闲隐藏时间"), m_hideDelay);
    auto* hideHelp = new QLabel(QStringLiteral("缩放与视图、视角历史和撤销重做在操作后显示。悬停、焦点、触摸或菜单展开期间保持显示，轻点相应底角可再次唤出。"));
    hideHelp->setWordWrap(true);
    form->addRow(hideHelp);
    appearance->setEnabled(preferencesWritable);
    appearancePage->addWidget(appearance);
    m_reload = new QPushButton(QStringLiteral("重新读取磁盘设置"));
    appearancePage->addWidget(m_reload, 0, Qt::AlignLeft);
    appearancePage->addStretch();

    auto* readingPage = addPage(QStringLiteral("阅读与会话"));
    auto* reading = new QGroupBox(QStringLiteral("阅读与会话"));
    auto* readingForm = new QFormLayout(reading);
    m_layoutMode = new QComboBox;
    m_layoutMode->addItem(QStringLiteral("单页"), QStringLiteral("single"));
    m_layoutMode->addItem(QStringLiteral("单页连续"), QStringLiteral("single-continuous"));
    m_layoutMode->addItem(QStringLiteral("双页"), QStringLiteral("facing"));
    m_layoutMode->addItem(QStringLiteral("双页连续"), QStringLiteral("facing-continuous"));
    m_layoutMode->setCurrentIndex(m_layoutMode->findData(value.tools.layout));
    readingForm->addRow(QStringLiteral("默认页面布局"), m_layoutMode);
    m_thumbnailSize = new QSpinBox;
    m_thumbnailSize->setRange(80, 280);
    m_thumbnailSize->setSuffix(QStringLiteral(" 像素"));
    m_thumbnailSize->setValue(value.tools.thumbnailSize);
    readingForm->addRow(QStringLiteral("展开页面的预览尺寸"), m_thumbnailSize);
    reading->setEnabled(preferencesWritable);
    readingPage->addWidget(reading);
    m_restoreSession = new QCheckBox(QStringLiteral("启动时恢复上次的文档标签与阅读位置"));
    m_restoreSession->setChecked(true);
    m_restoreSession->setEnabled(false);
    readingPage->addWidget(m_restoreSession);
    auto* navigationHelp = new QLabel(QStringLiteral("Ctrl + 滚轮缩放正文或展开页面的预览，Shift + 滚轮左右平移正文。适合宽度、页面和高度可从右下视图入口选择。"));
    navigationHelp->setWordWrap(true);
    readingPage->addWidget(navigationHelp);
    readingPage->addStretch();

    auto* inputPage = addPage(QStringLiteral("笔与输入"));
    auto* input = new QGroupBox(QStringLiteral("笔与输入"));
    auto* inputForm = new QFormLayout(input);
    m_inputFollowsTool = new QCheckBox(QStringLiteral("鼠标与手跟随当前工具"));
    m_inputFollowsTool->setChecked(value.tools.inputFollowsTool);
    inputForm->addRow(m_inputFollowsTool);
    auto* inputHelp = new QLabel(QStringLiteral("关闭后，鼠标与手用于浏览、选择正文及框选和移动已有批注。手写笔始终使用当前工具，两指用于平移和缩放。"));
    inputHelp->setWordWrap(true);
    inputForm->addRow(inputHelp);
    m_pressureEnabled = new QCheckBox(QStringLiteral("默认笔启用笔压"));
    m_pressureEnabled->setChecked(value.tools.pressureEnabled);
    inputForm->addRow(m_pressureEnabled);
    m_pressureSensitivity = new QSpinBox;
    m_pressureSensitivity->setRange(0, 100);
    m_pressureSensitivity->setValue(value.tools.pressureSensitivity);
    m_pressureSensitivity->setEnabled(value.tools.pressureEnabled);
    inputForm->addRow(QStringLiteral("默认笔的笔压灵敏度"), m_pressureSensitivity);
    connect(m_pressureEnabled, &QCheckBox::toggled, m_pressureSensitivity, &QWidget::setEnabled);
    auto* pressureHelp = new QLabel(QStringLiteral("50 为线性响应。数值越大，轻压时越容易写粗；数值越小，需要更重的笔压。此数值调整本应用的响应曲线，不代表笔的硬件压力级数。"));
    pressureHelp->setWordWrap(true);
    inputForm->addRow(pressureHelp);
    auto* pressureExample = new QLabel;
    pressureExample->setWordWrap(true);
    pressureExample->setTextFormat(Qt::PlainText);
    inputForm->addRow(pressureExample);
    const auto updatePressureExample = [this, pressureExample] {
        if (!m_pressureEnabled->isChecked()) {
            pressureExample->setText(QStringLiteral("默认笔已关闭笔压，新笔迹使用固定粗细。"));
            return;
        }
        const qreal exponent = std::pow(2.0, (50.0 - m_pressureSensitivity->value()) / 50.0);
        const qreal response = std::pow(0.25, exponent);
        pressureExample->setText(QStringLiteral("默认笔响应示例：设备上报 25% 压力时，响应约为 %1%。这是响应示例，并非实际力度测量。")
            .arg(QString::number(response * 100, 'f', 1)));
    };
    connect(m_pressureSensitivity, &QSpinBox::valueChanged, this, [updatePressureExample](int) { updatePressureExample(); });
    connect(m_pressureEnabled, &QCheckBox::toggled, this, [updatePressureExample](bool) { updatePressureExample(); });
    updatePressureExample();
    auto* pressureScope = new QLabel(QStringLiteral("此处设置默认笔。当前收藏工具的笔压请在该工具的「···」中调整。设置只影响新笔迹，已有笔迹保持不变。自由荧光笔使用固定线宽。"));
    pressureScope->setWordWrap(true);
    inputForm->addRow(pressureScope);
    m_eraserMode = new QComboBox;
    m_eraserMode->addItem(QStringLiteral("整笔擦除"), QStringLiteral("whole"));
    m_eraserMode->addItem(QStringLiteral("局部擦除"), QStringLiteral("partial"));
    m_eraserMode->setCurrentIndex(m_eraserMode->findData(value.tools.eraserMode));
    inputForm->addRow(QStringLiteral("橡皮与笔尾模式"), m_eraserMode);
    m_eraserRadius = new QDoubleSpinBox;
    m_eraserRadius->setRange(1, 100);
    m_eraserRadius->setSuffix(QStringLiteral(" pt"));
    m_eraserRadius->setValue(value.tools.eraserRadiusPt);
    inputForm->addRow(QStringLiteral("擦除半径"), m_eraserRadius);
    input->setEnabled(preferencesWritable);
    inputPage->addWidget(input);
#ifdef Q_OS_WIN
    auto* systemPen = new QGroupBox(QStringLiteral("Windows 与设备设置"));
    auto* systemPenLayout = new QVBoxLayout(systemPen);
    auto* systemPenHelp = new QLabel(QStringLiteral("打开 Windows 的「笔与 Windows Ink」页面。设备厂商提供的其他选项可能需在其配套应用中设置。"));
    systemPenHelp->setWordWrap(true);
    systemPenLayout->addWidget(systemPenHelp);
    auto* openSystemPen = new QPushButton(QStringLiteral("Windows 笔与 Windows Ink 设置"));
    openSystemPen->setMinimumHeight(44);
    systemPenLayout->addWidget(openSystemPen, 0, Qt::AlignLeft);
    connect(openSystemPen, &QPushButton::clicked, this, [systemPenHelp] {
        // Only an explicit click launches this documented Settings URI.
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:pen"))))
            systemPenHelp->setText(QStringLiteral("无法打开系统设置。可在 Windows 设置中查找「笔与 Windows Ink」。"));
    });
    inputPage->addWidget(systemPen);
#endif
    inputPage->addStretch();

    auto* annotationPage = addPage(QStringLiteral("批注与工具"));
    auto* pen = new QGroupBox(QStringLiteral("默认笔样式"));
    auto* penForm = new QFormLayout(pen);
    m_color = new QPushButton(value.penColor.name());
    m_color->setStyleSheet(QStringLiteral("color:%1;").arg(value.penColor.name()));
    penForm->addRow(QStringLiteral("笔颜色"), m_color);
    m_penWidth = new QDoubleSpinBox;
    m_penWidth->setRange(0.5, 24.0);
    m_penWidth->setSingleStep(0.5);
    m_penWidth->setSuffix(QStringLiteral(" pt"));
    m_penWidth->setValue(value.penWidthPt);
    penForm->addRow(QStringLiteral("笔粗细"), m_penWidth);
    m_penOpacity = new QDoubleSpinBox;
    m_penOpacity->setRange(0, 100);
    m_penOpacity->setDecimals(2);
    m_penOpacity->setSuffix(QStringLiteral(" %"));
    m_penOpacity->setValue(value.tools.opacity * 100);
    penForm->addRow(QStringLiteral("不透明度"), m_penOpacity);
    pen->setEnabled(preferencesWritable);
    annotationPage->addWidget(pen);

    auto* annotations = new QGroupBox(QStringLiteral("正文审阅标记"));
    auto* annotationLayout = new QVBoxLayout(annotations);
    m_editTextMarkups = new QCheckBox(QStringLiteral("框选和橡皮作用于正文审阅标记"));
    m_editTextMarkups->setChecked(value.editTextMarkups);
    auto* markupExplanation = new QLabel(QStringLiteral("适用于本应用可编辑的高亮、下划线、波浪线和删除线。开启后，橡皮命中会移除整项标记。自由荧光笔按笔迹处理。"));
    markupExplanation->setWordWrap(true);
    m_editTextMarkups->setAccessibleDescription(markupExplanation->text());
    annotationLayout->addWidget(m_editTextMarkups);
    annotationLayout->addWidget(markupExplanation);
    annotations->setEnabled(preferencesWritable);
    annotationPage->addWidget(annotations);
    auto* toolHelp = new QLabel(QStringLiteral("从工具栏「＋」选择工具并设置适用的颜色、粗细、透明度及持续或单次使用。收藏图标下方「···」可修改该预设。"));
    toolHelp->setWordWrap(true);
    annotationPage->addWidget(toolHelp);
    annotationPage->addStretch();

    auto* backupPage = addPage(QStringLiteral("本地备份"));
    auto* tabs = new QTabWidget;
    m_tabs = tabs;
    backupPage->addWidget(tabs, 1);
    auto* settings = new QWidget;
    auto* settingsLayout = new QVBoxLayout(settings);
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
    settingsLayout->addStretch();
    tabs->addTab(settings, QStringLiteral("备份设置"));

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
    auto* operations = new QGridLayout;
    m_refresh = new QPushButton(QStringLiteral("刷新"));
    m_now = new QPushButton(QStringLiteral("备份当前 PDF"));
    m_current = new QPushButton(QStringLiteral("清理当前 PDF"));
    m_selected = new QPushButton(QStringLiteral("清理勾选 PDF"));
    m_selectAll = new QPushButton(QStringLiteral("全选 PDF"));
    m_all = new QPushButton(QStringLiteral("清理全部 PDF"));
    int operationIndex = 0;
    for (auto* button : {m_refresh, m_now, m_current, m_selected, m_selectAll, m_all}) {
        operations->addWidget(button, operationIndex / 3, operationIndex % 3);
        ++operationIndex;
    }
    managerLayout->addLayout(operations);
    m_restore = new QPushButton(QStringLiteral("校验并另存所选备份…"));
    managerLayout->addWidget(m_restore, 0, Qt::AlignLeft);
    tabs->addTab(manager, QStringLiteral("备份管理"));
    auto* shortcutsPage = addPage(QStringLiteral("快捷键"));
    auto* shortcuts = new QGroupBox(QStringLiteral("键盘快捷键"));
    auto* shortcutsForm = new QFormLayout(shortcuts);
    const QList<QPair<QString, QString>> bindings{
        {QStringLiteral("打开 PDF"), QStringLiteral("Ctrl + O")},
        {QStringLiteral("保存"), QStringLiteral("Ctrl + S")},
        {QStringLiteral("另存为"), QStringLiteral("Ctrl + Shift + S")},
        {QStringLiteral("撤销"), QStringLiteral("Ctrl + Z")},
        {QStringLiteral("重做"), QStringLiteral("Ctrl + Y")},
        {QStringLiteral("搜索正文"), QStringLiteral("Ctrl + F")},
        {QStringLiteral("剪切／复制／粘贴批注"), QStringLiteral("Ctrl + X / C / V")},
        {QStringLiteral("切换文档标签"), QStringLiteral("Ctrl + Tab")},
        {QStringLiteral("关闭当前标签"), QStringLiteral("Ctrl + W")},
        {QStringLiteral("缩放"), QStringLiteral("Ctrl + 滚轮")},
        {QStringLiteral("左右平移"), QStringLiteral("Shift + 滚轮")}
    };
    for (const auto& binding : bindings) shortcutsForm->addRow(binding.first, new QLabel(binding.second));
    shortcutsPage->addWidget(shortcuts);
    auto* shortcutHelp = new QLabel(QStringLiteral("此页列出当前快捷键。自定义快捷键与冲突处理仍在后续待办中。"));
    shortcutHelp->setWordWrap(true);
    shortcutsPage->addWidget(shortcutHelp);
    shortcutsPage->addStretch();
    auto* aboutPage = addPage(QStringLiteral("关于"));
    auto* about = new QLabel(QStringLiteral("ProperNotes for Windows\n版本 %1\n\nPDF 阅读与手写批注\n\n原创代码 GPL-3.0-only\n原创说明 CC BY-SA 4.0\n第三方组件保留各自许可，详见程序目录的版权和许可文件。")
        .arg(QCoreApplication::applicationVersion()));
    about->setWordWrap(true);
    about->setTextFormat(Qt::PlainText);
    about->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutPage->addWidget(about);
    aboutPage->addStretch();
    connect(m_categories, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);
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
    result.controlsHideDelaySeconds = m_hideDelay->value();
    result.favoritesBar.visible = m_favoritesVisible->isChecked();
    result.favoritesBar.pinned = m_favoritesPinned->isChecked();
    result.favoritesBar.orientation = m_favoritesOrientation->currentData().toString();
    result.tools.layout = m_layoutMode->currentData().toString();
    result.tools.thumbnailSize = m_thumbnailSize->value();
    result.tools.inputFollowsTool = m_inputFollowsTool->isChecked();
    result.tools.pressureEnabled = m_pressureEnabled->isChecked();
    result.tools.pressureSensitivity = m_pressureSensitivity->value();
    result.tools.eraserMode = m_eraserMode->currentData().toString();
    result.tools.eraserRadiusPt = m_eraserRadius->value();
    result.penColor = m_penColor;
    result.penWidthPt = m_penWidth->value();
    result.tools.opacity = m_penOpacity->value() / 100.0;
    result.editTextMarkups = m_editTextMarkups->isChecked();
    result.backupIntervalMinutes = m_interval->value();
    result.backupRetainCount = m_retain->value();
    result.backupDirectory = m_directory->text().trimmed();
    return result;
}

bool SettingsDialog::migrateRequested() const { return m_migrate->isChecked(); }

void SettingsDialog::showBackupManager()
{
    m_categories->setCurrentRow(4);
    m_tabs->setCurrentIndex(1);
}

void SettingsDialog::setRestoreSessionEnabled(bool enabled, bool writable)
{
    m_restoreSession->setChecked(enabled);
    m_restoreSession->setEnabled(writable);
}

bool SettingsDialog::restoreSessionEnabled() const { return m_restoreSession->isChecked(); }

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
