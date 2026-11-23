# Native prototype limits

English (en): This is the focused Qt/PDF4QT handwriting prototype. It implements a new single-PDF editing format; it is not the full previous feature set. Actual device response, palm rejection, file compatibility and recovery behavior require acceptance. Compilation alone does not establish them.

Traditional Chinese (zh-TC): 此版是聚焦 Qt/PDF4QT 手寫閉環的原型, 使用新的單 PDF 編輯格式, 尚未移植前版全部功能. 裝置手感、防誤觸、檔案相容及異常恢復仍須驗收, 編譯成功不代表已完成驗收.

Simplified Chinese (zh-SC): 此版是聚焦 Qt/PDF4QT 手写闭环的原型, 使用新的单 PDF 编辑格式, 尚未移植前版全部功能. 设备手感、防误触、文件兼容及异常恢复仍须验收, 编译成功不代表已经完成验收.

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
