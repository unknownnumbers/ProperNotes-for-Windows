# Native prototype limits

English (en): This is the focused Qt/PDF4QT handwriting prototype. It implements a new single-PDF editing format; it is not the full previous feature set. Actual device response, palm rejection, file compatibility and recovery behavior require acceptance. Compilation alone does not establish them.

Traditional Chinese (zh-TC): 此版是聚焦 Qt/PDF4QT 手寫閉環的原型, 使用新的單 PDF 編輯格式, 尚未移植前版全部功能. 裝置手感、防誤觸、檔案相容及異常恢復仍須驗收, 編譯成功不代表已完成驗收.

Simplified Chinese (zh-SC): 此版是聚焦 Qt/PDF4QT 手写闭环的原型, 使用新的单 PDF 编辑格式, 尚未移植前版全部功能. 设备手感、防误触、文件兼容及异常恢复仍须验收, 编译成功不代表已经完成验收.

## 0.3.17 orientation and fitting along the palette

English (en): For vertical palettes, corner avoidance preserves the user's horizontal placement and edge
anchor, shortening the palette and changing its top only as needed at that same
horizontal position. Fixed icon widths and touch targets are retained; the middle
favorite list scrolls. User-directed horizontal movement remains available from
the unlocked handle and keyboard. Passive fitting does not replace the saved
anchor, vertical ratio or desired height. When no usable vertical space remains
for the fixed controls and at least 44 logical pixels of favorite viewport, the
palette temporarily hides at that same horizontal position and restores when
room returns, preserving the visibility choice. Hidden corner groups continue
to reserve their bounds. Cloud compilation and packaging are pending at source
preparation time. No installation, application launch or test is performed on the
user's PC; actual sizing and input still require acceptance.

Settings offers vertical (default) and horizontal orientations with independent
position and length memory. Horizontal palettes are 80 logical pixels high with
variable width, using a top/bottom edge gap and horizontal travel ratio. Their
avoidance keeps the vertical position and only adjusts horizontal range and
length. Favorites scroll horizontally and transparent left/right edges resize
the unlocked palette. Top/bottom docking mirrors vertical left/right docking.
Settings v13 migrates older settings to vertical. Fixed icon and touch sizes,
passive preservation of saved placement, and temporary hiding when no usable
space remains apply in either orientation. This is the project's rule; source
compilation and user acceptance remain pending.

Traditional Chinese (zh-TC): 角落避讓保留使用者的水平位置與左右錨點，只縮短工具框並在同一水平位置按需要最小幅度調整上下位置。圖示寬度與觸控區固定，中間收藏區捲動；解鎖後仍可從把手或鍵盤自行左右移動。被動調整不覆寫保存的錨點、垂直比例及要求高度。同一水平位置若放不下固定控件及至少 44 邏輯像素收藏區，則暫時隱藏，空間恢復後自動顯示，保留顯示選擇；底角控件隱藏時仍保留邊界。源碼準備時仍待雲端編譯及打包，沒有在使用者 PC 安裝、啟動應用或執行測試，實際尺寸及操作仍待驗收。

上述為垂直工具框規則。設定可選垂直（預設）／水平，位置及長度各自記憶。水平工具框固定高 80 邏輯像素、寬度可調，按上／下邊間距與左右可移動空間比例定位；避讓保留上下位置，只調整水平範圍及長度。收藏左右捲動，解鎖後直接拖透明左／右邊緣調長短，停靠對應閱讀區上／下邊。設定 v13 將舊配置遷移為垂直。兩方向均固定圖示與觸控尺寸，被動排放不覆寫位置記憶，放不下時暫隱再恢復。這是本專案規則，編譯及使用者驗收仍待完成。

Simplified Chinese (zh-SC): 角落避让保留用户的水平位置和左右锚点，只缩短工具框并在同一水平位置按需要最小幅度调整上下位置。图标宽度和触控区固定，中间收藏区滚动；解锁后仍可从把手或键盘自行左右移动。被动调整不覆盖保存的锚点、垂直比例和要求高度。同一水平位置若放不下固定控件和至少 44 逻辑像素收藏区，则暂时隐藏，空间恢复后自动显示，保留显示选择；底角控件隐藏时仍保留边界。源码准备时仍待云端编译和打包，没有在用户 PC 安装、启动应用或运行测试，实际尺寸和操作仍待验收。

上述是垂直工具框规则。设置可选垂直（默认）／水平，位置和长度各自记忆。水平工具框固定高 80 逻辑像素、宽度可调，按上／下边间距和左右可移动空间比例定位；避让保留上下位置，只调整水平范围和长度。收藏左右滚动，解锁后直接拖透明左／右边缘调长短，停靠对应阅读区上／下边。设置 v13 将旧配置迁移为垂直。两方向均固定图标和触控尺寸，被动排放不覆盖位置记忆，放不下时暂隐再恢复。这是本项目规则，编译和用户验收仍待完成。

## 0.3.16 corner-control avoidance

Historical note: automatic horizontal avoidance is superseded by the 0.3.17 rule
above. 歷史說明：自動水平避讓已由上述 0.3.17 規則取代。历史说明：自动水平避让已由上述 0.3.17 规则取代。

English (en): The floating palette avoids the actual bounds of the upper-right
page capsule and both lower control groups with a small gap. Automatically
hidden lower groups still reserve their bounds. Viewport, font and page-count
changes refresh placement; passive avoidance preserves the saved anchor, ratio
and desired height. Cloud compilation and packaging are pending at source
preparation time. No installation, application launch or test is performed on the
user's PC. Visual spacing, display scaling, very small viewports and interaction
still require acceptance. This project rule is not a claim about Drawboard's
unverified internal layout behavior.

The palette retains its fixed controls and at least 44 logical pixels of favorite
viewport. With no safe usable slot 80 logical pixels wide in an extremely small reading
area, it temporarily hides and restores when room returns, preserving the user's
visibility choice, anchor, ratio and desired height. Sidebar pin and settings
remain available.

Traditional Chinese (zh-TC): 懸浮工具框按實際控件邊界及小間距，避開右上頁碼框與兩組底角控件，底角自動隱藏時也保留邊界。閱讀區、字體及頁數變化會更新排放；被動避讓保留已保存的錨點、比例及要求高度。源碼準備時仍待雲端編譯及打包，沒有在使用者 PC 安裝、啟動應用或執行測試。實際間距、顯示縮放、極小閱讀區和操作仍待驗收。這是本專案規則，不代表已核實的 Drawboard 內部排版邏輯。

工具框保留固定控件及至少 44 邏輯像素收藏區。極小閱讀區若沒有寬 80 邏輯像素的安全可用位置則暫時隱藏，空間恢復後自動顯示；使用者的顯示選擇、錨點、比例及要求高度保留，側欄固定與設定入口仍可用。

Simplified Chinese (zh-SC): 悬浮工具框按实际控件边界和小间距，避开右上页码框和两组底角控件，底角自动隐藏时也保留边界。阅读区、字体和页数变化会更新摆放；被动避让保留已保存的锚点、比例和要求高度。源码准备时仍待云端编译和打包，没有在用户 PC 安装、启动应用或运行测试。实际间距、显示缩放、极小阅读区和操作仍待验收。这是本项目规则，不代表已核实的 Drawboard 内部布局逻辑。

工具框保留固定控件和至少 44 逻辑像素收藏区。极小阅读区若没有宽 80 逻辑像素的安全可用位置则暂时隐藏，空间恢复后自动显示；用户的显示选择、锚点、比例和要求高度保留，侧栏固定和设置入口仍可用。

## 0.3.15 relative floating-palette placement

English (en): The vertical floating palette remembers a left/right reading-edge
anchor, its logical-pixel gap and a vertical ratio within the available travel
space. Window reflow and temporary viewport constraints do not replace these
saved values or the requested height. Settings v12 migrates old absolute pixel
coordinates when a reading viewport is available; docking retains its separate
floating placement. Cloud compilation and packaging are pending at source
preparation time. No installation, application launch or test is performed on
the user's PC. Actual window resizing, display scaling, gesture cancellation,
pinning and dock/undock behavior still require user acceptance. This is the
project's confirmed interaction rule, not a claim about unverified Drawboard
behavior.

Traditional Chinese (zh-TC): 垂直懸浮工具框記住閱讀區左／右邊錨點、邏輯像素間距及可移動空間內的垂直比例。視窗排放與暫時的閱讀區限制不覆寫保存的定位及要求高度。設定 v12 待閱讀區可用後遷移舊絕對像素座標，停靠時保留獨立懸浮定位。源碼準備時仍待雲端編譯及打包，沒有在使用者 PC 安裝、啟動應用或執行測試。視窗縮放、顯示比例、取消手勢、固定及停靠／拖出仍待驗收；此為本專案確認規則，不代表已核實的 Drawboard 行為。

Simplified Chinese (zh-SC): 垂直悬浮工具框记住阅读区左／右边锚点、逻辑像素间距和可移动空间内的垂直比例。窗口排放与暂时的阅读区限制不覆盖保存的定位及要求高度。设置 v12 待阅读区可用后迁移旧绝对像素坐标，停靠时保留独立悬浮定位。源码准备时仍待云端编译和打包，没有在用户 PC 安装、启动应用或运行测试。窗口缩放、显示比例、取消手势、固定和停靠／拖出仍待验收；这是本项目确认规则，不代表已核实的 Drawboard 行为。

## 0.3.14 page-field display correction

English (en): The page picker now uses a separate integer-validated text field,
explicit digit sizing, centered text and an opaque theme-aware container. It
avoids the styled NoButtons spin-box path in the pinned Qt source that can make
the internal text area almost zero width. Display refresh and input drafts are
handled separately. Cloud compilation and packaging are pending at source
preparation time. No installation, application launch or test is performed on the
user's PC. Actual display scaling, current-page updates and page entry still need
user acceptance.

Traditional Chinese (zh-TC): 此修訂以整數校驗的獨立文字框、明確字寬、居中對齊和不透明主題背景處理頁碼，避開目前 Qt 的 NoButtons 數字框樣式造成文字區近乎零寬的路徑。顯示刷新與輸入草稿分開處理。源碼準備時仍待雲端編譯及打包，沒有在使用者 PC 安裝、啟動應用或執行測試。顯示縮放、目前頁更新和輸入跳頁仍待實際驗收。

Simplified Chinese (zh-SC): 此修订以整数校验的独立文字框、明确字宽、居中对齐和不透明主题背景处理页码，避开当前 Qt 的 NoButtons 数字框样式造成文字区近乎零宽的路径。显示刷新与输入草稿分开处理。源码准备时仍待云端编译和打包，没有在用户 PC 安装、启动应用或运行测试。显示缩放、当前页更新和输入跳页仍待实际验收。

## 0.3.13 palette edge resizing

English (en): The large top and bottom resize icons are removed. Direct edge
zones resize an unlocked floating palette. Locking prevents resizing, and a
docked palette retains the full side-strip height. PDF behavior and saved
document formats remain unchanged. Cloud compilation and portable packaging are
pending at source preparation time. No installation, application launch or test
is performed on the user's PC. Actual edge hit targets, input cancellation and
display scaling still require user acceptance.

Traditional Chinese (zh-TC): 此修訂移除上下大型拉伸圖示，解鎖且懸浮時直接拖動工具框上、下邊緣調整高度。鎖定時停用拉伸，停靠時維持側欄整高，PDF 操作及文件格式保留原有邏輯。源碼準備時仍待雲端編譯及打包，沒有在使用者 PC 安裝、啟動應用或執行測試。邊緣命中範圍、輸入取消和顯示縮放仍待使用者實際驗收。

Simplified Chinese (zh-SC): 此修订移除上下大型拉伸图标，解锁且悬浮时直接拖动工具框上、下边缘调整高度。锁定时停用拉伸，停靠时保持侧栏整高，PDF 操作和文档格式保留原有逻辑。源码准备时仍待云端编译和打包，没有在用户 PC 安装、启动应用或运行测试。边缘命中范围、输入取消和显示缩放仍待用户实际验收。

## 0.3.12 icons and restored edge docking

English (en): This source revision unifies vector icon scale, alignment and
high-DPI rendering, and lays out favorite point-size badges independently of
their glyphs. It preserves existing tool commands and saved document formats.
Cloud compilation and portable packaging are pending at source preparation time.
No local installation, application launch or test is performed. The user's
acceptance of appearance, display scaling and device input remains pending.

Edge docking uses child layout hosts, not a separate desktop window. Dragging
shows a preview and changes parent/layout only on release. Docked bars preserve
their separate floating geometry. Mouse, touch and pen dragging, cancellation,
display/text scaling and restored dock state still need device acceptance. The
Windows pen settings link is user initiated. It is not an embedded vendor driver
control panel or device calibration, and advanced pressure curves and tilt remain
outside the implemented scope.

Traditional Chinese (zh-TC): 此修訂統一向量圖示的視覺尺寸、對齊及高 DPI 顯示，收藏的 pt 角標與圖形獨立排版。工具命令和文件儲存格式維持原有邏輯。源碼準備時仍待雲端編譯及便攜包打包，沒有在使用者 PC 安裝、啟動應用或測試。外觀、顯示縮放及裝置輸入仍待使用者驗收。

Simplified Chinese (zh-SC): 此修订统一矢量图标的视觉尺寸、对齐和高 DPI 显示，收藏的 pt 角标与图形独立排版。工具命令和文档保存格式维持原有逻辑。源码准备时仍待云端编译和便携包打包，没有在用户 PC 安装、启动应用或测试。外观、显示缩放和设备输入仍待用户验收。

## 0.3.11 interface revision

English (en): This source revision moves controls into the approved narrow left
rail, floating scrollable favorites palette and transient bottom-corner groups.
The settings gear opens categorized application settings, including a 1–30 second
idle hide delay with a 3-second default. Palette geometry and pin state are saved.
At source preparation time, cloud compilation and packaging are pending. No local
application launch or test is authorized or claimed. Touch scrolling, resize bounds,
focus/hover hiding, restored geometry, pen input and PDF save/reopen still require
runtime acceptance by the user. The interaction design follows user decisions,
and does not establish unverified Drawboard behavior. Large-document optimization
remains a later task. Earlier version descriptions below are retained history.

Traditional Chinese (zh-TC): 此修訂把控件移入已確認的左側窄邊欄、可捲動浮動收藏工具框及底角浮現控件。齒輪開啟分類設定，包含預設 3 秒、可調 1 至 30 秒的空閒隱藏時間，工具框位置、高度和固定狀態會保留。源碼準備時仍待雲端編譯與打包，沒有在使用者 PC 啟動應用或執行測試。觸控捲動、邊界縮放、焦點／懸停隱藏、位置恢復、筆輸入及 PDF 儲存重開仍待使用者實際驗收。依使用者已確認的互動規則實作，未核實的 Drawboard 邏輯仍不作推定，大文件優化繼續後排。

Simplified Chinese (zh-SC): 此修订把控件移入已确认的左侧窄边栏、可滚动浮动收藏工具框和底角浮现控件。齿轮打开分类设置，包含默认 3 秒、可调 1 至 30 秒的空闲隐藏时间，工具框位置、高度和固定状态会保留。源码准备时仍待云端编译和打包，没有在用户 PC 启动应用或运行测试。触控滚动、边界缩放、焦点／悬停隐藏、位置恢复、笔输入及 PDF 保存重开仍待用户实际验收。按用户已确认的交互规则实现，未核实的 Drawboard 逻辑仍不作推定，大文件优化继续后排。

Documents saved with native shapes use format v5. Earlier edits use their original
native formats when the document has no shapes. Older native builds protect v5 from
overwrite. The shapes use PDF annotation appearances. This version supports
proportional transforms and reserves individual endpoint editing for later work.

含原生圖形的 PDF 使用 v5。其餘文件沿用對應的舊原生格式。舊原生版本對 v5 有覆寫保護。圖形使用 PDF 批注外觀，本版支援等比變換，單獨調整端點留待後續。

含原生形状的 PDF 使用 v5。其余文件沿用对应的旧原生格式。旧原生版本对 v5 有覆写保护。形状使用 PDF 批注外观，本版支持等比变换，单独调整端点留待后续。

Freehand highlighter uses fixed-width strokes. The presence of freehand strokes
requires native format v4. The editor also reads v1–v3, and v4 is overwrite-protected in older native builds.

自由螢光筆採固定粗細。含自由螢光筆跡的 PDF 使用原生 v4，本版也可讀 v1–v3，舊版開啟 v4 時有覆寫保護。

自由荧光笔采用固定粗细。含自由荧光笔迹的 PDF 使用原生 v4，本版也可读 v1–v3，旧版打开 v4 时有覆写保护。

Text features in 0.3.2 use the PDF's existing text layer. Scan-only pages need
an existing OCR text layer from another tool for text selection and search. Search is literal
and page-local, with up to 1,000 results. Reading order and geometry depend on
the PDF's text encoding and layout. Review marks require a same-page selection
with supported orthogonal glyph geometry. Native v3 PDFs with review marks open
under overwrite protection in older native builds. Review marks can be erased
or deleted as whole objects. Their geometry stays anchored to the selected text.

0.3.2 讀取 PDF 既有文字層。搜尋採單頁字面比對，上限 1,000 筆。文字順序與座標取決於 PDF 編碼和排版。
審閱限同一頁及支援的直角文字座標。審閱標記可整筆擦除或刪除，位置錨定正文。含審閱標記的原生 v3 PDF 在舊原生版受覆寫保護。

0.3.2 读取 PDF 已有文字层。搜索按页作字面匹配，上限 1,000 条。文字顺序与坐标取决于 PDF 编码和排版。
审阅限同一页及支持的直角文字坐标。审阅标记可整笔擦除或删除，位置锚定正文。含审阅标记的原生 v3 PDF 在旧原生版受覆写保护。

English (en): The `0.3.1-native-preview.1` source integrates the workspace and favorites
features below. Compilation status comes from each artifact's provenance; application
startup, tests and runtime acceptance have not been performed. The scope is 32
horizontal tabs with independent unsaved models and content/view histories, individual
unsaved checks on close/exit, protection against Save As over another tab including an
unloaded placeholder, and independent backups for loaded background tabs. Recent files
are limited to 100 entries, reading records to 256, metadata to 4MiB, with approximately
650ms coalesced writes. Startup restoration defaults on and creates ordered lazy,
retryable placeholders with active-tab/reading state; it reads current disk files and
does not recover unsaved ink, PDF content or undo stacks. A previous dirty marker directs
the user to backups. Clearing recents preserves PDFs, backups and reading/session records.
The single small-icon favorites bar supports the five existing tools, with snapshot
create/edit/duplicate/delete/drag reorder/collapse/left-right dock/float, up to 128 presets.
Preset management,
tool defaults and selected objects remain separate; applying a preset affects later
operations. Multiple bars and other tool presets remain later work. Retaining loaded
background PDF models increases memory use; these limits are not a process-memory budget.

Traditional Chinese (zh-TC): `0.3.1-native-preview.1` 工作區/收藏源碼已接入, 編譯狀態見各構件
provenance; 應用尚未啟動、測試或實際驗收.
範圍為 32 個橫向標籤、各自未儲存模型及內容/視角歷史、關閉/退出逐一確認、另存保護其他標籤
含未載入佔位項, 以及已載入背景標籤的獨立備份. 最近 100 筆、閱讀 256 筆、中繼資料 4MiB,
約 650ms 合併寫入. 啟動恢復預設開啟, 恢復標籤順序/活動項/閱讀狀態, 按需載入且失敗可重試.
讀取目前磁碟文件, 不恢復未儲存墨跡、PDF 正文或撤銷棧; 舊未儲存標記提示核對備份.
清理最近不刪 PDF、備份或閱讀/會話登記. 單一小圖示收藏欄覆蓋現有五工具快照,
可新增/編輯/複製/刪除/拖排/折疊/左右停靠/浮動, 上限 128 預設; 管理收藏、工具預設及已選物件分開,
點擊後僅影響後續操作. 多收藏欄及其他工具預設後續. 背景 PDF 模型仍佔記憶體, 不由這些數量推定總記憶體上限.

Simplified Chinese (zh-SC): `0.3.1-native-preview.1` 工作区/收藏源码已接入, 编译状态见各构件
provenance; 应用尚未启动、测试或实际验收.
范围为 32 个横向标签、各自未保存模型及内容/视角历史、关闭/退出逐一确认、另存保护其他标签
含未加载占位项, 以及已加载后台标签的独立备份. 最近 100 条、阅读 256 条、元数据 4MiB,
约 650ms 合并写入. 启动恢复默认开启, 恢复标签顺序/活动项/阅读状态, 按需加载且失败可重试.
读取当前磁盘文档, 不恢复未保存墨迹、PDF 正文或撤销栈; 旧未保存标记提示核对备份.
清理最近不删 PDF、备份或阅读/会话登记. 单个小图标收藏栏覆盖现有五工具快照,
可新建/编辑/复制/删除/拖排/折叠/左右停靠/浮动, 上限 128 预设; 管理收藏、工具默认及已选对象分开,
点击后仅影响后续操作. 多收藏栏及其他工具预设后续. 后台 PDF 模型仍占内存, 不由这些数量推定总内存上限.

- Pen pressure is collected through Qt tablet events when the device/driver reports it. Mouse input is a constant-pressure fallback. Device and driver coverage is not asserted.
- The 0.3.0 baseline adds reading layouts, view history, page overview, session document undo/redo, partial erasure, selection/movement, basic text boxes and the confirmed input/single-use policies. The 0.3.1 source integrates tabs, recents, session restoration and single-bar favorites. Bookmarks, annotation lists and locking, full custom navigation and shortcuts, and other confirmed first-batch tools remain open.
- Input and serialized output are currently capped at 512MiB. A 1GB-class document is a future design/acceptance target, not supported capacity claimed by this build. See LARGE-DOCUMENTS.md for the verified parser/serialization constraints and planned work.
- Recovery backups are full PDFs in the native backup catalog, not edits to the original file. Restore creates a separate PDF. Legacy web-preview backup catalogs are not imported. Atomic writes, source checks and verified copies do not guarantee recovery under every disk, power or filesystem failure.
- An interrupted migration can leave pending entries visible for inspection or retry. Source data is preserved until destination copies are verified and committed. A corrupt or foreign catalog is protected rather than silently repaired.
- The mouse/touch switch and persistent/single-use tools follow the user's explicit rules. This does not assert that every corresponding Drawboard branch or default has been independently verified. Device buttons beyond a reported eraser end are not mapped by guessing.
- Older ProperNotes v1-v4 data, unknown/inconsistent native data, protected document cases and nonstandard page units are not silently migrated or overwritten. The UI shows a read-only reason where applicable. Keep the previous preview for legacy editing.
- Pressure-shaped vector appearances and the original pressure samples are stored in the same PDF. Other readers can display the standard annotation appearance; they are not promised to preserve or edit all ProperNotes-specific pressure data after their own modifications.
- Saving checks source/target byte changes and uses a temporary atomic-file commit. This does not provide a cross-application compare-and-swap guarantee or guarantee recovery under every storage failure.
- No application's actual launch, PDF round trip, handwriting device behavior or automated test is claimed by the build recipe. Consult included provenance and validation records for the exact scope.
- New native v2 files with text boxes are protected as read-only by older native builds. This build reads native v1 ink with its original interpretation; the older web preview's v1-v4 catalog is a separate legacy format and remains protected from overwrite.
- Text boxes use plain Unicode text and a raster PDF appearance, not embedded font programs. The native editing overlay uses available system fonts and may fall back if the original family is missing. The saved appearance is retained for unchanged objects. Other readers may not preserve this program's editable metadata after their own edits.
- Editing and view histories are bounded in-memory session records; saving does not clear editing history, but closing/reopening does. Facing layouts pair (1,2), (3,4); this stage has no separate-cover option. Page-image cache budgets do not include all decoded PDF resources, canvas overlays or transient allocations, and are not a 1GB-document capacity claim.
