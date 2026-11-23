# ProperNotes for Windows - native Qt prototype

Developed through human and AI collaboration. Some AI-assisted material has not received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協作內容尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协作内容尚未经过完整人工审查.

## English (en)

The 0.3.13 source removes the large top and bottom resize-icon rows from the
floating palette. When it is unlocked and floating, drag its top or bottom edge
directly to change the height. Locked palettes cannot be resized, and docked
palettes keep the full height of their side strip. The bottom movement handle
and existing PDF commands and document formats are retained. At source
preparation time, cloud compilation and packaging are pending. No software is
installed, application started or test run on the recipient's PC.

The 0.3.12 source corrects icon sizing and alignment across the sidebar, floating
palette and bottom controls. Vector glyphs use a common visual scale and adapt
to high-DPI rendering. Favorite point-size badges are laid out separately from
their glyphs. Existing tool behavior and saved document formats are unchanged.
At source preparation time, cloud compilation and packaging are pending. The
recipient's PC is not used to install, start or test this revision.

Pan keeps a hand icon; Mouse and Touch Follow Tool uses a mouse icon, and palette
pinning shows a toolbar with a pin. Edge docking is restored: unlock the palette,
drag toward a reading-area edge, and release on the preview to occupy the left or
right side without covering the PDF. Drag the unlocked move handle out to float
again. Dock side and the separate floating geometry are saved in settings v11.
Pen settings explain the default pressure response and the independent settings
of saved presets. A button opens Windows Pen & Windows Ink settings only when
clicked. Custom pressure curves, device calibration, tilt and prediction are not
implemented by this revision. Bundled Lucide SVGs keep their own full notices.

The 0.3.11 source adopts the approved compact native layout: a narrow left rail,
a floating favorites palette, and separate bottom-corner history and zoom controls.
Pan and Select stay at the top of the palette, favorites scroll in the middle,
and the bottom + opens one tool/options picker with Add to Toolbar and Use.
Favorite icons show their color and applicable point size, with an options entry
for the selected preset. Unlock the palette from the left rail to move it or
resize its height from its edges. Locking hides its movement and resizing handles.
Its position, height and lock state are remembered. The settings gear opens the
categorized application settings. Bottom controls default to hiding after 3 seconds
of inactivity, adjustable from 1 to 30 seconds in Settings, and remain visible
during interaction. Cloud compilation and packaging of this revision are pending
at source preparation time. The application is not installed, started or tested
on the recipient's PC. Large-document work remains deferred. Earlier version
entries below retain their historical control placements.

0.3.10 adds a native annotation sidebar with page and kind filters and navigation
to an annotation. Select and Erase expose the text-review inclusion setting in
their toolbar. Page previews reuse suitable reading images and schedule visible
thumbnails without a fixed wait after each result.

0.3.9 adds editable lines, arrows, rectangles and ellipses with outline styles,
optional fills for rectangles and ellipses, and favorites. Shapes and ink can
move, scale and rotate together. Text boxes and shapes can be cut, copied and
pasted in the same PDF. Settings control whether Select and Erase include text
review marks. PDFs with ProperNotes shapes use native editing format v5.

0.3.8 adds proportional scaling and rotation for selected pen and freehand
highlighter strokes. Cut, copy and paste work across pages of the same open PDF,
with on-screen buttons and keyboard shortcuts. The floating favorites bar can be
pinned at a position restored on restart.

0.3.7 fixes canvas input routing. With Mouse & Touch Follow Tool off, a mouse or
single finger can pan, manually select text, or select and move existing annotations.
Drawing, erasing and automatic review marks still follow the pen.

0.3.6 adds an in-place style editor for a selected pen or freehand highlighter stroke.
Change color, width and opacity in one undoable edit, and reopen the saved PDF to
continue editing. The four text review tools can be activated from favorites and
used continuously or for a single completed mark.

0.3.5 adds Freehand Highlighter, a separate tool for drawing directly on PDF pages,
including scans. Save color, width and opacity as a favorite. Strokes support
undo, redo and editing after saving and reopening the PDF. See PORTABLE.md for the controls.

In 0.3.4, mixed text selections can mark supported glyphs and report skipped
glyphs. Copy retains the whole selection. A mark uses one undo step. The favorites
bar has a close button and follows the main window when minimized or closed.

The 0.3.3 source improves visible-page raster resolution on high-DPI displays.
It also accepts review geometry for more horizontal text, including synthetic
italics, clipped glyphs and Type 3 fonts with reliable metrics. The text selection
strip exposes each review action and reports why a mark could not be applied.

The 0.3.2 source adds selection and copying from the existing PDF text layer,
page-by-page literal search with cancellation and jump-to-result, and Highlight,
Underline, Squiggly and StrikeOut. Review favorite presets store kind, color and
opacity. Hold Shift while turning the wheel to
pan the PDF horizontally. Ctrl + wheel zooms. See PORTABLE.md for the controls.

The source for `0.3.1-native-preview.1` integrates up to 32 horizontal document tabs,
with independent unsaved content, editing/view histories and reading positions. Closing
a tab or exiting checks unsaved documents individually; a cancelled or unsuccessful
save keeps the tab. Save As must not overwrite another tab's file, including an unloaded
session placeholder. Loaded background tabs retain independent backup revisions and schedules.

Workspace metadata supports 100 recent files, 256 reading records, a 4MiB metadata
limit and approximately 650ms coalesced writes. Startup tab restoration defaults on and
can be disabled. It restores tab order, the active tab and reading state through lazy
placeholders; missing or inaccessible files remain retryable. Loading reads the current
disk PDF. Session metadata contains no PDF content, unsaved ink or undo stack; a previous
unsaved-state marker prompts checking backups. Removing recent entries does not delete
PDFs, backups or the reading/session registry.

One compact favorites bar supports the five existing tools: Pen, Eraser,
Pan, Select/Move and Text Box. Tool/style snapshots support create, edit, duplicate,
delete, drag reorder, collapse, left/right docking and floating, with small icons and
a project limit of 128 presets.
Preset editing is separate from tool defaults and selected-object editing; activating a
preset configures subsequent operations. Multiple bars and presets for tools not yet
migrated remain later work. Compilation status is recorded in each artifact's provenance;
application startup, tests and runtime acceptance have not been performed.

The 0.3.0 source adds four reading layouts (single/facing, paged/continuous), fit
width/page/height, independent view back/forward, full-screen resizable page previews,
document-wide session undo/redo, whole/partial erasure, selection/movement and editable
plain-text boxes. The pen has pressure enable/sensitivity, color, width and independent
opacity. Creation/erasure tools have remembered persistent/single-use modes; successful
single use returns to the previous persistent tool. Flipping a supported pen to its
eraser end temporarily uses the chosen eraser mode and preserves the selected tool.

The mouse-and-touch annotation switch starts off. Pen input uses its selected tool.
Mouse-left and single-finger input support panning, manual text selection and moving
selected annotations. Turning the switch on lets those inputs follow the selected tool.
Two fingers browse, and pen contact suppresses canvas touch.
Ctrl+wheel zooms the document, or changes thumbnail size inside the full-screen preview.
These are this project's confirmed interaction rules, not a claim that every Drawboard
setting has been independently verified.

Native editing data is now version 2 for ink and text boxes. Version 1 native ink remains
editable with its original pressure interpretation. Text-box appearances are rasterized
with alpha for other readers, while plain Unicode text and geometry stay editable here.
No font files are bundled; a missing font may change the editing overlay's fallback.
Undo/redo and view histories are bounded session state and are not saved into the PDF.
Actual startup, device response, file round trips and failure recovery remain untested.

This separate native prototype uses Qt 6.11.3 and PDF4QT 1.7.0.0. It targets ordinary PDFs with the reading and basic-annotation features described above, saving one PDF and recovering editable native annotations after reopening. Mouse/touch annotation provides a constant-pressure fallback when enabled. PDF rendering and saving are serialized on a worker thread; the live overlay is separate.

The toolbar exposes Pen, Eraser, Pan, Select/Move and Text Box tools. Input routing and single-use behavior follow the user-confirmed application policy. Device, palm-rejection, latency and PDF compatibility acceptance remain unperformed. This is not a port of every feature from the previous preview.

Native edit data is versioned inside the PDF with standard Ink, FreeText and text review annotations and their appearances. Files containing this build's text review marks use native schema v3. The app continues to read native v1 and v2. The older web preview uses a separate format and opens under overwrite protection.

The 0.2.1 preview adds native settings for system/light/dark theme, page-sidebar visibility/width and the current pen style. Preferences are stored separately from PDF content in `propernotes-native-settings.json` beside the program. Corrupt, future or externally changed settings are not silently overwritten. Local recovery backups default to `Backups` beside the program, every 5 minutes after content changes, retaining 10 copies per PDF. Count 0 stops new backups and preserves existing copies. Registered old directories remain manageable after a directory change; migration is optional. The manager supports current/selected/all PDF cleanup and verified restoration to a separate PDF. Backup success never marks the current document as saved. Legacy web-preview backup catalogs are not imported by this native catalog.

Large-document capacity is not yet established. The current input/output guard remains 512MiB; [the design record](LARGE-DOCUMENTS.md) explains why page caching alone cannot establish 1GB support.

Use `Start-ProperNotes.cmd` only in a complete compiled portable package. This program does not need WebView2, Node.js or a local Qt SDK. Build prerequisites and downloads are restricted to the GitHub-hosted build recipe under `cloud`; normal startup never builds or installs them. Consult packaged provenance and validation records for actual compilation and acceptance status. The application and tests are not launched by the recipe.

Original code is GPL-3.0-only; original explanatory documentation is CC BY-SA 4.0. Third-party terms, source and notices are preserved separately. See COPYRIGHT.md and the packaged licenses.

## Traditional Chinese (zh-TC)

0.3.13 源碼移除浮動工具框上下兩排大型拉伸圖示，解鎖且懸浮時可直接拖動上、下邊緣調整高度。鎖定時不能拉伸，停靠時維持側欄整高。底部移動把手、原有 PDF 操作及文件格式保留。源碼準備時仍待雲端編譯及打包，不在使用者 PC 安裝、啟動應用或執行測試。

0.3.12 源碼修正側邊欄、浮動工具框與底角控件的圖示大小及對齊，向量圖形採統一的視覺尺寸並適應高 DPI，收藏的 pt 角標與圖形分開排版。平移保留手形，跟隨工具使用滑鼠圖示，固定使用工具欄與圖釘圖示。恢復靠左右邊緣預覽、釋放停靠及解鎖後拖出懸浮，側欄留出版面空間，設定 v11 記住停靠側與獨立懸浮位置。筆設定說明預設筆與收藏的獨立壓感，並加入點擊後開啟 Windows 筆設定的入口。自訂曲線、校準、傾斜及預測尚未接入。Lucide 素材保留原授權通知。PDF 儲存格式保持原有邏輯。源碼準備時仍待雲端編譯及打包，不在使用者 PC 安裝、啟動或測試。

0.3.11 源碼採用已確認的精簡原生版面，包含左側窄邊欄、浮動收藏工具框，以及分置底角的歷史與縮放控件。平移和框選固定在工具框頂部，收藏區可用滾輪或手指捲動，底部「＋」統一開啟工具與選項，提供「加入工具欄」和「使用」。預設圖示反映顏色與適用的 pt 數值，選中後可開啟選項。從左側解除固定後可移動工具框或拖動邊緣調整高度，鎖定時隱藏移動及調高把手，位置、高度和鎖定狀態會保留。齒輪開啟分類軟體設定，底角控件預設空閒 3 秒隱藏，可在設定調整為 1 至 30 秒，操作期間保持顯示。此修訂在源碼準備時仍待雲端編譯與打包，不在使用者 PC 安裝、啟動或測試。大文件優化繼續留待後續，下方舊版條目保留當時的控件位置。

0.3.10 新增批注側欄，可按頁面範圍及類型篩選，點選批注可定位。框選和橡皮的工具列加入「包含正文標記」設定。頁面預覽會重用適用的閱讀圖像，依可見範圍排程，完成一張後接續處理。

0.3.9 新增可編輯的直線、箭頭、矩形和橢圓，支援線條樣式、矩形及橢圓填充與收藏。圖形和筆跡可一起移動、縮放及旋轉，圖形和文字方塊可在同一份 PDF 內跨頁剪貼。設定可控制框選和橡皮是否作用於正文審閱標記。含 ProperNotes 圖形的 PDF 使用原生編輯格式 v5。

0.3.8 可等比縮放及旋轉選取的普通筆跡和自由螢光筆跡。按鈕及快捷鍵可在同一份開啟的 PDF 內跨頁剪貼。浮動收藏欄可以固定，重新開啟程式後會恢復位置和固定狀態。

0.3.7 修正畫布輸入。關閉「鼠标与手跟随工具」時，滑鼠和單指可平移、手動選字、框選或移動既有批注。繪製、擦除和自動審閱仍由筆操作。

0.3.6 可就地編輯選取的普通筆跡或自由螢光筆跡。顏色、粗細和不透明度可一次套用並復原，儲存後可重新開啟 PDF 繼續編輯。四種正文審閱工具可從收藏啟用，支援持續或單次使用。

0.3.5 新增獨立的自由螢光筆，可直接在 PDF 頁面上劃線，掃描頁也能使用。顏色、粗細和不透明度可收藏。筆跡可復原、重做，儲存後重新開啟 PDF 仍可編輯。操作見 PORTABLE.md。

0.3.4 在混合選區中為有可靠座標的正文加標記，並顯示略過數量。複製保留整段選字，一次標記可一併撤銷。
收藏欄有獨立關閉按鈕，主視窗最小化或關閉時，浮動收藏欄也會隱藏。

0.3.3 依螢幕像素密度改善可見頁面的渲染清晰度。審閱支援更多橫排文字，包含模擬斜體、
頁邊裁切字形和具有可靠字寬的 Type 3 字型。選字工具列顯示四種標記及套用結果。

0.3.2 源碼加入 PDF 既有文字的拖選、複製、逐頁搜尋與結果跳轉，以及高亮、
底線、波浪線和刪除線。審閱收藏儲存類型、顏色及不透明度。
正文 Shift + 滾輪左右平移，Ctrl + 滾輪縮放。含本版審閱標記的 PDF 使用原生格式 v3，仍可讀原生 v1/v2。

`0.3.1-native-preview.1` 源碼已接入最多 32 個橫向文件標籤, 各自保留未儲存內容、
編輯/視角歷史及閱讀位置. 關閉標籤或退出時逐一確認未儲存文件, 取消或儲存失敗
保留標籤; 另存不得覆蓋其他標籤的文件, 包含尚未載入的會話佔位標籤.
已載入的背景標籤分別保留備份修訂及排程.

工作區上限為 100 筆最近文件、256 筆閱讀記錄及 4MiB 中繼資料, 約 650ms 合併寫入.
啟動恢復預設開啟且可關閉, 透過按需載入的佔位標籤恢復順序、活動標籤及閱讀狀態;
缺失或無權存取的文件保留重試. 載入讀取目前磁碟 PDF, 會話不含 PDF 正文、未儲存
墨跡或撤銷棧; 上次未儲存標記提示核對備份. 清理最近列表不刪 PDF、備份或閱讀/會話登記.

單一小圖示收藏欄已接入筆、橡皮、平移、框選/移動及文字框五種現有工具的樣式快照,
支援新增、編輯、複製、刪除、拖排、折疊、左右停靠及浮動, 專案上限 128 預設. 編輯收藏、工具預設及
已選物件分開, 點擊收藏才設定後續操作. 多收藏欄及尚未遷入工具的預設仍列後續.
編譯狀態以各構件的 provenance 為準; 應用尚未啟動、測試或實際驗收.

0.3.0 原生源碼增加單/雙頁及連續/分頁閱讀、適寬/適頁/適高、獨立視角歷史、
真正全螢幕且可調尺寸的縮圖、整文件會話撤銷/重做、整筆/局部擦除、框選移動及
純文字框. 筆有壓感開關/靈敏度、顏色/粗細/透明度, 工具持續/單次設定可記憶.
滑鼠與觸摸跟隨工具開關預設關閉。筆使用所選工具，滑鼠左鍵與單指可平移、手動選字及框選移動。開啟後
兩者也使用所選工具。兩指可瀏覽，筆接觸時抑制畫布觸摸。Ctrl+滾輪分別調整
正文倍率或全螢幕縮圖尺寸. 這是本專案確認規則, 不宣稱全部 Drawboard 邏輯已核實.
原生資料 v2 保存墨跡及文字框, 舊原生 v1 墨跡可編輯. 文字外觀含 alpha 點陣圖,
Unicode 原文仍可編輯; 不打包字型, 缺字型時編輯覆蓋層可能回退. 歷史不寫入 PDF.
目前只有源碼/構建交付流程, 啟動、裝置、檔案往返和異常恢復仍待驗收.

此獨立原生原型使用 Qt 6.11.3 與 PDF4QT 1.7.0.0, 提供上述閱讀/基礎批注範圍及單 PDF 儲存重開編輯. 啟用批注後的滑鼠/觸摸採固定壓力. PDF 渲染及儲存於工作執行緒串行處理, 即時覆蓋層分開.

工具欄提供筆、橡皮、平移、框選/移動與文字框, 輸入及單次模式按使用者確認規則實作. 裝置、防誤觸、延遲和 PDF 相容性尚未驗收, 亦未移植前版全部功能.

原生編輯資料與標準批注外觀一起存在 PDF 中, 不需要旁邊的工程檔. 舊 Web 版 v1-v4、未知或不一致的資料禁止覆蓋; 完整舊 Web 格式遷移另行安排. 原生 v1 墨跡可編輯.

0.2.1 增加原生設定及偏好記憶: 跟隨系統/淺/深主題、頁面側欄顯隱/寬度和目前筆色/粗細. 設定檔為程式旁的 `propernotes-native-settings.json`, 不含 PDF 正文; 損壞、未來版本或外部改動不靜默覆蓋. 本地備份預設程式根目錄 `Backups`, 每 5 分鐘內容變動才新增, 每 PDF 保留 10 份; 數量 0 停新增並保留舊份. 目錄變更預設不遷移, 舊登記目錄仍可管理, 可另選遷移. 管理器可清理目前/勾選/全部 PDF 備份, 校驗後另存恢復副本. 備份不替代普通儲存, 舊 Web 版目錄格式暫不匯入. 目前仍限制 512MiB, 1GB 級改造見 LARGE-DOCUMENTS.md, 尚未驗收.

完整便攜包以 `Start-ProperNotes.cmd` 啟動, 不需要 WebView2、Node.js 或本機 Qt SDK. 依賴下載及構建只在 GitHub 託管流程執行, 普通啟動不構建或安裝. 實際編譯及驗收範圍見包內紀錄; 構建流程不啟動應用或測試. 原創程式 GPL-3.0-only, 原創說明 CC BY-SA 4.0, 第三方保留原條款.

## Simplified Chinese (zh-SC)

0.3.13 源码移除浮动工具框上下两排大型拉伸图标，解锁且悬浮时可直接拖动上、下边缘调整高度。锁定时不能拉伸，停靠时保持侧栏整高。底部移动把手、原有 PDF 操作和文档格式保留。源码准备时仍待云端编译和打包，不在用户 PC 安装、启动应用或运行测试。

0.3.12 源码修正侧边栏、浮动工具框与底角控件的图标大小和对齐，矢量图形采用统一的视觉尺寸并适应高 DPI，收藏的 pt 角标与图形分开排版。平移保留手形，跟随工具使用鼠标图标，固定使用工具栏与图钉图标。恢复靠左右边缘预览、释放停靠和解锁后拖出悬浮，侧栏留出版面空间，设置 v11 记住停靠侧与独立悬浮位置。笔设置说明默认笔和收藏的独立压感，并加入点击后打开 Windows 笔设置的入口。自定义曲线、校准、倾斜和预测尚未接入。Lucide 素材保留原许可通知。PDF 保存格式保持原有逻辑。源码准备时仍待云端编译和打包，不在用户 PC 安装、启动或测试。

0.3.11 源码采用已确认的紧凑原生布局，包含左侧窄边栏、浮动收藏工具框，以及分置底角的历史和缩放控件。平移和框选固定在工具框顶部，收藏区可用滚轮或手指滚动，底部「＋」统一打开工具与选项，提供「加入工具栏」和「使用」。预设图标反映颜色和适用的 pt 数值，选中后可打开选项。从左侧解除固定后可移动工具框或拖动边缘调整高度，锁定时隐藏移动和调高把手，位置、高度和锁定状态会保留。齿轮打开分类软件设置，底角控件默认空闲 3 秒隐藏，可在设置调整为 1 至 30 秒，操作期间保持显示。此修订在源码准备时仍待云端编译和打包，不在用户 PC 安装、启动或测试。大文件优化继续留待后续，下方旧版条目保留当时的控件位置。

0.3.10 新增批注侧栏，可按页面范围和类型筛选，点击批注可定位。框选和橡皮的工具栏加入「包含正文标记」设置。页面预览会复用适用的阅读图像，按可见范围排队，完成一张后接着处理。

0.3.9 新增可编辑的直线、箭头、矩形和椭圆，支持线条样式、矩形及椭圆填充和收藏。形状和笔迹可一起移动、缩放和旋转，形状和文字框可在同一份 PDF 内跨页剪贴。设置可控制框选和橡皮是否作用于正文审阅标记。含 ProperNotes 形状的 PDF 使用原生编辑格式 v5。

0.3.8 可等比缩放及旋转选中的普通笔迹和自由荧光笔迹。按钮及快捷键可在同一份已打开的 PDF 内跨页剪贴。浮动收藏栏可以固定，重启后恢复位置和固定状态。

0.3.7 修复画布输入。关闭「鼠标与手跟随工具」时，鼠标和单指可平移、手动选字、框选或移动已有批注。绘写、擦除和自动审阅仍由笔操作。

0.3.6 可就地编辑选中的普通笔迹或自由荧光笔迹。颜色、粗细和不透明度可一次应用并撤销，保存后可重新打开 PDF 继续编辑。四种正文审阅工具可从收藏启用，支持持续或单次使用。

0.3.5 新增独立的自由荧光笔，可直接在 PDF 页面上划线，扫描页也能使用。颜色、粗细和不透明度可收藏。笔迹可撤销、重做，保存后重新打开 PDF 仍可编辑。操作见 PORTABLE.md。

0.3.4 在混合选区中为有可靠坐标的正文添加标记，并显示跳过数量。复制保留整段选字，一次标记可一并撤销。
收藏栏有独立关闭按钮，主窗口最小化或关闭时，浮动收藏栏也会隐藏。

0.3.3 按屏幕像素密度改善可见页面的渲染清晰度。审阅支持更多横排文字，包括模拟斜体、
页边裁切字形和具有可靠字宽的 Type 3 字体。选字工具栏显示四种标记及应用结果。

0.3.2 源码加入 PDF 既有文字的拖选、复制、逐页搜索与结果跳转，以及高亮、
下划线、波浪线和删除线。审阅收藏保存类型、颜色和不透明度。
正文 Shift + 滚轮左右平移，Ctrl + 滚轮缩放。含本版审阅标记的 PDF 使用原生格式 v3，仍可读原生 v1/v2。

`0.3.1-native-preview.1` 源码已接入最多 32 个横向文档标签, 各自保留未保存内容、
编辑/视角历史及阅读位置. 关闭标签或退出时逐一确认未保存文档, 取消或保存失败
保留标签; 另存不得覆盖其他标签的文件, 包含尚未加载的会话占位标签.
已加载的后台标签分别保留备份修订及调度.

工作区上限为 100 条最近文件、256 条阅读记录及 4MiB 元数据, 约 650ms 合并写入.
启动恢复默认开启且可关闭, 通过按需加载的占位标签恢复顺序、活动标签及阅读状态;
缺失或无权访问的文件保留重试. 加载读取当前磁盘 PDF, 会话不含 PDF 正文、未保存
墨迹或撤销栈; 上次未保存标记提示核对备份. 清理最近列表不删 PDF、备份或阅读/会话登记.

单个小图标收藏栏已接入笔、橡皮、平移、框选/移动及文字框五种现有工具的样式快照,
支持新建、编辑、复制、删除、拖排、折叠、左右停靠及浮动, 项目上限 128 预设. 编辑收藏、工具默认及
已选对象分开, 点击收藏才设置后续操作. 多收藏栏及尚未迁入工具的预设仍列后续.
编译状态以各构件的 provenance 为准; 应用尚未启动、测试或实际验收.

0.3.0 原生源码增加单/双页及连续/分页阅读、适宽/适页/适高、独立视角历史、
真正全屏且可调尺寸的缩略图、整文档会话撤销/重做、整笔/局部擦除、框选移动及
纯文字框. 笔有压感开关/灵敏度、颜色/粗细/透明度, 工具持续/单次设置可记忆.
鼠标与触摸跟随工具开关默认关闭。笔使用所选工具，鼠标左键与单指可平移、手动选字及框选移动。开启后
两者也使用所选工具。两指可浏览，笔接触时抑制画布触摸。Ctrl+滚轮分别调整
正文倍率或全屏缩略图尺寸. 这是本项目确认规则, 不宣称全部 Drawboard 逻辑已核实.
原生数据 v2 保存墨迹及文字框, 旧原生 v1 墨迹可编辑. 文字外观含 alpha 位图,
Unicode 原文仍可编辑; 不打包字体, 缺字体时编辑覆盖层可能回退. 历史不写入 PDF.
目前只有源码/构建交付流程, 启动、设备、文件往返和异常恢复仍待验收.

此独立原生原型使用 Qt 6.11.3 与 PDF4QT 1.7.0.0, 提供上述阅读/基础批注范围及单 PDF 保存重开编辑. 启用批注后的鼠标/触摸采用固定压力. PDF 渲染及保存于工作线程串行处理, 实时覆盖层分开.

工具栏提供笔、橡皮、平移、框选/移动与文字框, 输入及单次模式按用户确认规则实现. 设备、防误触、延迟和 PDF 兼容性尚未验收, 亦未移植前版全部功能.

原生编辑数据与标准批注外观一起存在 PDF 中, 不需要旁边的工程文件. 旧 Web 版 v1-v4、未知或不一致的数据禁止覆盖; 完整旧 Web 格式迁移另行安排. 原生 v1 墨迹可编辑.

0.2.1 增加原生设置及偏好记忆: 跟随系统/浅/深主题、页面侧栏显隐/宽度和当前笔色/粗细. 设置文件为程序旁的 `propernotes-native-settings.json`, 不含 PDF 正文; 损坏、未来版本或外部改动不静默覆盖. 本地备份默认程序根目录 `Backups`, 每 5 分钟内容变化才新增, 每 PDF 保留 10 份; 数量 0 停新增并保留旧份. 目录变更默认不迁移, 旧登记目录仍可管理, 可另选迁移. 管理器可清理当前/勾选/全部 PDF 备份, 校验后另存恢复副本. 备份不替代普通保存, 旧 Web 版目录格式暂不导入. 目前仍限制 512MiB, 1GB 级改造见 LARGE-DOCUMENTS.md, 尚未验收.

完整便携包以 `Start-ProperNotes.cmd` 启动, 不需要 WebView2、Node.js 或本机 Qt SDK. 依赖下载及构建只在 GitHub 托管流程执行, 普通启动不构建或安装. 实际编译及验收范围见包内记录; 构建流程不启动应用或测试. 原创代码 GPL-3.0-only, 原创说明 CC BY-SA 4.0, 第三方保留原条款.
