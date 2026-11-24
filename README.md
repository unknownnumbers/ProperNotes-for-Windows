# ProperNotes for Windows

Developed through human and AI collaboration. Some AI-assisted code and text may not have received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協助產生的程式與文字可能尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协助产生的代码与文字可能尚未经过完整人工审查.

## Native preview (0.3.x)

English (en): The new focused prototype uses Qt 6 and PDF4QT, with tablet pressure through Qt. It implements opening an existing PDF, handwriting, whole/partial erasure, editable text boxes and document-session undo/redo, saving a single PDF, and reopening its own editable annotations. This is a separate program from the previous preview. It does not yet port the full earlier feature set. Compilation and packaging status are recorded with each artifact; application launch, functional tests and device acceptance are not performed by the workflow. See [native scope and portable use](propernotes-native/README.md), [limits](propernotes-native/KNOWN-LIMITATIONS.md) and [cloud build inputs](propernotes-native/cloud/README.md).

Traditional Chinese (zh-TC): 新原生原型採 Qt 6 + PDF4QT, 透過 Qt 接收筆壓, 聚焦開啟 PDF、手寫、整筆/局部擦除、文字框及整文件會話撤銷/重做、儲存單一 PDF, 以及重開繼續編輯原生批注. 此為獨立程式, 尚未移植前版全部功能. 每份產物附編譯及組包紀錄, 流程不啟動應用或執行功能/裝置驗收. 詳見上述原生版說明及限制.

Simplified Chinese (zh-SC): 新原生原型采用 Qt 6 + PDF4QT, 通过 Qt 接收笔压, 聚焦打开 PDF、手写、整笔/局部擦除、文字框及整文档会话撤销/重做、保存单一 PDF, 以及重开继续编辑原生批注. 这是独立程序, 尚未移植前版全部功能. 每份产物附编译及组包记录, 流程不启动应用或执行功能/设备验收. 详见上述原生版说明及限制.

The 0.2.1 native source additionally provides theme/sidebar/pen preferences and local recovery-backup management. Defaults are 5 minutes, 10 copies per PDF, and Backups beside the program; count 0 preserves old copies and stops creation. See the native README for scope, unperformed acceptance and the [large-document design](propernotes-native/LARGE-DOCUMENTS.md). 原生版亦增加設定/偏好及本地備份, 詳見原生版說明. 原生版亦增加设置/偏好及本地备份, 详见原生版说明.

The 0.3.0 native source adds single/facing continuous and paged reading, fit width/page/height, view history, full-screen resizable page previews, document-session undo/redo, whole/partial erasure, selection/movement, pressure/opacity controls and editable plain-text boxes. Mouse/touch routing and persistent/single-use behavior follow the project's confirmed interaction policy. Native format v2 adds text boxes while retaining editable native v1 ink. This is still part of the confirmed first batch, not completion of every earlier feature. The current 512MiB guard and deferred SumatraPDF-inspired large-file work remain documented in the native design record. 原生 0.3.0 增加上述閱讀與基礎批注能力, 仍待裝置及功能驗收. 原生 0.3.0 增加上述阅读与基础批注能力, 仍待设备及功能验收.


The 0.3.1 native source adds horizontal document tabs with independent edit/view histories, recent files, reading-state and lazy session recovery, and a single dockable/floating favorites bar with tool/style presets. Session metadata reopens current disk files; it does not contain unsaved annotations or undo stacks. Favorites activate styles for future annotations without changing existing objects or stored tool defaults. The workflow compiles and packages without launching or testing the application. 原生 0.3.1 增加多文件標籤、最近檔案、閱讀位置/延遲會話恢復與可停靠/浮動的單收藏欄, 仍待實際驗收. 原生 0.3.1 增加多文档标签、最近文件、阅读位置/按需会话恢复与可停靠/浮动的单收藏栏, 仍待实际验收.

The 0.3.2 native source adds text selection and copy, cancellable search of the existing PDF text layer, and Highlight, Underline, Squiggly and StrikeOut review marks. Review styles can be saved as favorites. Shift + wheel pans the document horizontally and Ctrl + wheel zooms. Files containing new review marks use native format v3. 原生 0.3.2 加入既有文字的選取、搜尋及四種審閱標記，Shift + 滾輪左右平移。原生 0.3.2 加入已有文字的选择、搜索及四种审阅标记，Shift + 滚轮左右平移。

The 0.3.3 revision prioritizes visible-page raster quality on high-DPI displays and makes review actions and their outcomes visible in text selection mode. It accepts additional reliable text geometry for review marks. 原生 0.3.3 改善高 DPI 可見頁清晰度及正文審閱操作。原生 0.3.3 改善高 DPI 可见页清晰度及正文审阅操作。

The 0.3.4 revision applies text review to supported glyphs within a mixed selection and reports skipped glyphs. Copy retains the full text. 原生 0.3.4 支援混合選區中的可用文字標記。原生 0.3.4 支持混合选区中的可用文字标记。

The floating favorites bar now has a close button and hides with the main window. 收藏欄可直接關閉，浮動時會隨主視窗隱藏。收藏栏可直接关闭，浮动时会随主窗口隐藏。

The 0.3.5 revision adds Freehand Highlighter to draw directly on PDF pages, including scans. Favorite presets store color, width and opacity. Freehand strokes support undo and redo and stay editable when saved to PDF and reopened in this version. 原生 0.3.5 新增自由螢光筆，可直接在掃描頁上劃線，收藏樣式，儲存重開後繼續編輯。原生 0.3.5 新增自由荧光笔，可直接在扫描页上划线，收藏样式，保存重开后继续编辑。

The 0.3.6 revision adds an in-place style editor for a selected pen or freehand highlighter stroke and review tools that can be activated from favorites for continuous or single use. 原生 0.3.6 加入筆跡就地樣式編輯和可持續或單次使用的正文審閱工具收藏。原生 0.3.6 加入笔迹就地样式编辑和可持续或单次使用的正文审阅工具收藏。

The 0.3.7 revision lets a mouse or one finger pan, manually select text, or select and move annotations while Mouse & Touch Follow Tool is off. Automatic review on release still follows the pen. 原生 0.3.7 關閉跟隨時允許平移、手動選字、框選和移動既有批注。原生 0.3.7 关闭跟随时允许平移、手动选字、框选和移动已有批注。

The 0.3.8 revision adds scaling and rotation for pen and freehand highlighter selections, with touch and keyboard cut, copy and paste within a PDF. Floating favorites can be pinned at a saved position. 原生 0.3.8 支援筆跡縮放、旋轉、同份 PDF 跨頁剪貼及固定浮動收藏欄。原生 0.3.8 支持笔迹缩放、旋转、同一 PDF 跨页剪贴及固定浮动收藏栏。

The 0.3.9 revision adds editable line, arrow, rectangle and ellipse annotations. Shapes and ink can scale and rotate together. Text boxes and shapes support clipboard operations within the same PDF. A setting controls whether Select and Erase include editable text review marks. 原生 0.3.9 新增可編輯的直線、箭頭、矩形和橢圓，支援文字方塊及圖形剪貼，以及審閱標記選取和擦除設定。原生 0.3.9 新增可编辑的直线、箭头、矩形和椭圆，支持文字框及形状剪贴，以及审阅标记选择和擦除设置。

The 0.3.10 revision adds the native annotation list with page/type filters and click-to-locate. The Select and Erase toolbar exposes the saved text-review inclusion switch. Sidebar previews reuse suitable rendered pages and schedule visible rows with no fixed per-image pause. 原生 0.3.10 增加批注列表、頁面及類型篩選和點選定位，工具列增加正文標記開關並改善側欄縮圖排程。原生 0.3.10 增加批注列表、页面和类型筛选及点击定位，工具栏增加正文标记开关并改善侧栏缩略图排队。

The 0.3.11 revision adopts a narrow native sidebar and an in-viewport floating favorites palette. Pan and Select stay above a scrollable preset list. The unified tool picker offers Add to toolbar and Use with the same applicable properties. Unlocking reveals movement and height controls. History and zoom groups appear near the bottom corners, with a saved idle delay of 1–30 seconds, default 3 seconds. The categorized settings window retains local backup management. The cloud workflow compiles and packages only, without launching the application or running tests. 原生 0.3.11 採用窄側欄及可調高度的浮動收藏工具框，底角控件預設 3 秒隱藏並可設定。原生 0.3.11 采用窄侧栏及可调高度的浮动收藏工具框，底角控件默认 3 秒隐藏并可设置。實際操作與裝置驗收仍待用戶試用。实际操作与设备验收仍待用户试用。

The 0.3.12 revision replaces inconsistent rasterized UI drawings with a pinned Lucide SVG subset, rendered at a common 24-pixel scale and the requested display pixel ratio. Favorite point-size badges use separate layout space. Pan and the mouse/touch follow switch have distinct hand and mouse icons; palette pinning depicts a toolbar and pin. Left/right edge docking is restored with a release preview, occupied layout space and separately saved floating geometry. Pen settings explain pressure response and preset independence, and provide a user-clicked Windows Pen & Windows Ink settings link. The full icon notices and source identities accompany the portable package. Cloud compilation and packaging do not launch or test the application. 原生 0.3.12 統一 SVG 圖示、獨立角標，恢復左右停靠並補充筆設定說明。原生 0.3.12 统一 SVG 图标、独立角标，恢复左右停靠并补充笔设置说明。實際外觀與輸入仍待用戶驗收。实际外观与输入仍待用户验收。

The 0.3.13 revision removes the large top and bottom palette resize buttons. Drag an unlocked floating palette's upper or lower border directly to change height; no icon rows are reserved for resizing. The bottom move handle, edge docking, saved geometry and locked-state rules are retained. The workflow compiles and packages only. 原生 0.3.13 移除上下調高圖示，解鎖懸浮時直接拖動上下邊緣拉伸。原生 0.3.13 移除上下调高图标，解锁悬浮时直接拖动上下边缘拉伸。沒有在使用者 PC 安裝、啟動或測試。没有在用户 PC 安装、启动或测试。

The 0.3.14 revision fixes the blank current-page field in the upper-right picker. An independent numeric editor avoids the pinned Qt styled NoButtons spin-box path, with explicit digit width, readable centered text and an opaque group background. View refreshes preserve input drafts; Enter or leaving the editor commits a valid page, and Escape cancels. Cloud compilation and packaging do not start or test the application. 原生 0.3.14 修正右上頁碼白框，顯示目前頁並保留輸入跳頁。原生 0.3.14 修正右上页码白框，显示当前页并保留输入跳页。實際顯示與操作仍待用戶驗收。实际显示与操作仍待用户验收。

The 0.3.15 source adds relative placement for the vertical floating palette: the nearer reading edge and logical-pixel gap anchor it horizontally, and a ratio of available travel space positions it vertically. Window resizing follows those saved values; temporary size constraints do not replace its requested placement or height. Settings v12 preserves docking separately and migrates older pixel coordinates when a reading viewport is available. Cloud compilation and packaging are pending at source preparation time. No installation, application launch or test is performed on the user's PC. 原生 0.3.15 記住懸浮工具框左右錨點及邊距、上下可移動空間比例，隨視窗大小調整。原生 0.3.15 记住悬浮工具框左右锚点和边距、上下可移动空间比例，随窗口大小调整。實際輸入、停靠及縮放仍待驗收。实际输入、停靠和缩放仍待验收。

Historical note: the following 0.3.16 horizontal avoidance behavior is superseded by 0.3.17. 歷史說明：下述 0.3.16 水平避讓由 0.3.17 取代。历史说明：下述 0.3.16 水平避让由 0.3.17 取代。

The 0.3.16 source keeps the floating palette clear of the upper-right page capsule and the lower history/undo and zoom/view groups, using their actual widget bounds plus a small gap. Automatically hidden lower controls retain their reserved bounds. Viewport, font and page-count changes refresh placement without replacing the saved edge anchor, vertical ratio or desired height. Cloud compilation and packaging are pending at source preparation time; no installation, application launch or test is performed on the user's PC. 原生 0.3.16 按實際控件範圍避開頁碼及兩組底角控件，隱藏時仍預留空間。原生 0.3.16 按实际控件范围避开页码和两组底角控件，隐藏时仍预留空间。實際間距、小視窗及操作仍待驗收。实际间距、小窗口和操作仍待验收。

The 0.3.17 source preserves the user's horizontal palette position and edge anchor during corner avoidance. It shortens the palette and adjusts its top only as necessary at that same horizontal position; favorites scroll while icon widths and touch targets stay fixed. User-directed horizontal movement remains available. Passive fitting preserves the saved anchor, ratio and desired height; insufficient vertical space temporarily hides the palette until room returns. Cloud compilation and packaging are pending at source preparation time, with no installation, application launch or test on the user's PC. 原生 0.3.17 保留使用者的水平位置，僅縮短高度及調整必要上下位置，收藏區捲動。原生 0.3.17 保留用户的水平位置，仅缩短高度和调整必要上下位置，收藏区滚动。實際排放及輸入仍待驗收。实际排放和输入仍待验收。

The 0.3.17 source also offers vertical (default) or horizontal palettes in Settings,
with independent position and length memory. Horizontal palettes keep a fixed
80-logical-pixel height, anchor to a top/bottom edge gap and horizontal travel
ratio, and avoid corner controls by adjusting only horizontal range and length.
Favorites scroll horizontally; transparent left/right edges resize an unlocked
palette. Top/bottom docking mirrors vertical left/right docking. Settings v13
migrates older configurations to vertical. Icons and touch targets keep their
sizes. These are project rules, with cloud compilation and actual acceptance
pending. 原生 0.3.17 另可設定垂直／水平工具框，獨立記住位置及長度。原生 0.3.17 另可设置垂直／水平工具框，独立记住位置和长度。

## Retained previous preview (0.1.x)

The following sections describe the earlier WebView2/PDF.js preview and its feature scope. They do not describe the Qt prototype. 以下章節介紹保留的前版. 以下章节介绍保留的前版.

## English (en)

ProperNotes is a local Windows PDF reader and annotation project. Its source implements handwriting, shapes, text boxes, sticky notes, text review annotations, object editing, bookmarks, continuous/facing-page layouts, document tabs, recent files, reading-state recovery, preferences and local backups.

**Status: Windows x64 preview build.** The native host compiled successfully in GitHub Actions. The application has not been launched, and functional tests, device acceptance and performance validation have not been performed. Features listed above are present in source and are not claims of accepted behavior. Pen/touch routing and Tool Lock behavior remain unresolved. Printing, export, OCR, original-text editing, redaction and professional measurement are outside this preview. Consult the package's `provenance.json`, `validation-range.json` and file/license records for its actual build identity and validation limits.

The native layout uses WinForms, WebView2 and a bundled Node.js process. This preview uses an existing Evergreen WebView2 Runtime, includes no Fixed Runtime, and does not automatically install dependencies. Start a portable package with `Start-ProperNotes.cmd`; a source checkout does not build itself. Read [PORTABLE.md](PORTABLE.md) for prerequisites and build entry points.

Original code: GPL-3.0-only, with a narrow [WebView2 additional permission](LICENSE-WEBVIEW2-EXCEPTION) for the Windows host contributions. Original explanatory documentation: CC BY-SA 4.0. Third-party components keep their own terms. See [COPYRIGHT.md](COPYRIGHT.md), [LICENSE](LICENSE), [LICENSE-DOCS](LICENSE-DOCS) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The package records identify included components and remaining review limits.

## Traditional Chinese (zh-TC)

ProperNotes 是 Windows 本地 PDF 閱讀與批注專案. 原始碼已接入手寫, 圖形, 文字框, 便箋, 文字審閱批注, 物件編輯, 書籤, 連續/雙頁閱讀, 多文件標籤, 最近檔案, 閱讀狀態恢復, 偏好及本地備份.

**目前是 Windows x64 預覽構建.** 原生宿主已在 GitHub Actions 編譯成功. 應用尚未啟動驗收, 亦未執行功能測試, 裝置驗收或效能驗證. 上列功能是原始碼已接入的範圍, 不代表行為已通過驗收. 筆/觸控分工及 Tool Lock 行為仍待釐清. 列印, 匯出, OCR, 原文修改, 塗黑及專業測量不屬於本次預覽. 實際構建身份和驗證邊界以包內 `provenance.json`, `validation-range.json` 及檔案/授權記錄為準.

原生結構使用 WinForms, WebView2 及隨包 Node.js 程序. 本預覽使用已有 Evergreen WebView2 Runtime, 不附 Fixed Runtime, 不自動安裝依賴. 便攜包以 `Start-ProperNotes.cmd` 啟動; 原始碼目錄不會自行構建. 前置條件及構建入口見 [PORTABLE.md](PORTABLE.md).

原創程式採 GPL-3.0-only, Windows 宿主貢獻另有狹窄的 [WebView2 附加許可](LICENSE-WEBVIEW2-EXCEPTION). 原創說明文字採 CC BY-SA 4.0, 第三方元件保留原條款. 見 [COPYRIGHT.md](COPYRIGHT.md), [LICENSE](LICENSE), [LICENSE-DOCS](LICENSE-DOCS) 及 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). 實際隨包元件與尚存核對邊界見包內記錄.

## Simplified Chinese (zh-SC)

ProperNotes 是 Windows 本地 PDF 阅读与批注项目. 源码已接入手写, 图形, 文本框, 便笺, 文本审阅批注, 对象编辑, 书签, 连续/双页阅读, 多文档标签, 最近文件, 阅读状态恢复, 偏好及本地备份.

**目前是 Windows x64 预览构建.** 原生宿主已在 GitHub Actions 编译成功. 应用尚未启动验收, 亦未执行功能测试, 设备验收或性能验证. 上列功能是源码已接入的范围, 不代表行为已通过验收. 笔/触控分工及 Tool Lock 行为仍待厘清. 打印, 导出, OCR, 原文修改, 涂黑及专业测量不属于本次预览. 实际构建身份和验证边界以包内 `provenance.json`, `validation-range.json` 及文件/许可记录为准.

原生结构使用 WinForms, WebView2 及随包 Node.js 进程. 本预览使用已有 Evergreen WebView2 Runtime, 不附 Fixed Runtime, 不自动安装依赖. 便携包以 `Start-ProperNotes.cmd` 启动; 源码目录不会自行构建. 前置条件及构建入口见 [PORTABLE.md](PORTABLE.md).

原创代码采用 GPL-3.0-only, Windows 宿主贡献另有狭窄的 [WebView2 附加许可](LICENSE-WEBVIEW2-EXCEPTION). 原创说明文字采用 CC BY-SA 4.0, 第三方组件保留原条款. 见 [COPYRIGHT.md](COPYRIGHT.md), [LICENSE](LICENSE), [LICENSE-DOCS](LICENSE-DOCS) 及 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). 实际随包组件与尚存核对边界见包内记录.
