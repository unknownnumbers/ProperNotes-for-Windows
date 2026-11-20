# ProperNotes native preview - portable use

Original explanatory text: CC BY-SA 4.0. See COPYRIGHT.md.

## English (en)

Click **Select Text** (选文字), then drag over the PDF text on one page. In 0.3.3,
the strip above the page shows the four review buttons and what happened to the selection.
Use Ctrl+C to copy or click a review button to mark the selection. The review
menu lets you choose Highlight, Underline, Squiggly or StrikeOut, change color and
opacity, and save the style as a favorite. Click a review favorite, select text,
then apply the mark. Search with Ctrl+F, Enter to start, and click a result to jump.
Search supports a case option, cancellation, and up to 1,000 matches. It reads the
existing PDF text layer. Hold Shift + wheel to pan horizontally in the document.
Ctrl + wheel zooms the document or resizes previews in the full-screen page grid.
In 0.3.4, a selection that mixes supported and unsupported glyph geometry can
mark the supported text. The strip reports how many glyphs were skipped. Copy
uses the full selected text. An oversized mark is refused as a whole.
Close the favorites bar with its × button and reopen it from View. A floating
favorites bar hides when the main window is minimized or closes.

The `0.3.1-native-preview.1` source integrates the workspace features below. See each
artifact's provenance for compilation status; application startup, tests and runtime
acceptance have not been performed. Open adds a horizontal tab or activates the same registered
path, up to 32 tabs. Each tab keeps its own unsaved content and editing/view history.
Closing or exiting checks unsaved tabs individually; cancelling or failing a save keeps
them open. Save As protects other tabs' paths, including unloaded restoration placeholders.
Loaded background documents retain their own backup revisions and schedules.

Recent files (100 visible entries) support reopen/activate, individual removal and clear.
Clearing does not delete PDFs, backups or the separate reading/session registry (256
records). Workspace metadata is limited to 4MiB and writes are coalesced for approximately
650ms. Startup restoration defaults on: ordered lazy placeholders preserve the active tab
and reading state, and a failed file remains retryable. Reopening reads the current disk
file; unsaved ink, PDF content and undo stacks are not restored from session metadata.
An earlier unsaved-state marker directs you to backups.

The small-icon favorites bar snapshots Pen, Eraser, Pan, Select/Move and Text Box
styles. Create/edit/duplicate/delete or drag-reorder presets, collapse the bar, dock it
left/right or float it, up to the project's 128-preset limit. Editing a preset does not modify tool defaults or selected PDF
objects; selecting the preset applies it to subsequent operations. Multiple bars and
presets for additional tools remain future work.

For 0.3.0: pen input uses the selected tool. Mouse-left and one finger initially pan;
enable the visible mouse-and-touch switch to draw/select/create text with those inputs.
Two fingers always pan/zoom. The tool's Persistent button retains that tool; switch it
off for one successful operation followed by a return to the previous persistent tool.
Pen-tail erasure is temporary and uses the selected whole/partial eraser option.

Use the layout selector and Fit Width/Page/Height to read; the view back/forward buttons
are independent from Ctrl+Z/Ctrl+Y content undo/redo. Text-entry undo stays in the editor.
New text: drag a frame, type (Enter inserts a line), then Ctrl+Enter or click outside to
confirm; Save also confirms valid text. Esc cancels the draft. Empty new frames create
nothing; clearing and confirming an existing text box is an undoable removal.
Choose Select/Move to intersect-select objects, then drag the selection; double-click a
text box to edit and use its handles to resize. Whole/partial erasure is undoable.

Expand Pages opens a real full-screen page grid. Adjust its slider or Ctrl+wheel to
resize previews. Click a page to return there; Esc returns to the previous reading view.
This does not change the document's zoom. Ordinary Ctrl+wheel changes document zoom.

Keep the complete program directory together. Double-click `Start-ProperNotes.cmd`.
No Qt SDK, Node.js or WebView2 installation is required by this package.

1. Choose Open PDF and select an ordinary local PDF.
2. Choose Pen, a color and a width. A tablet pen uses pressure reported by Qt;
   mouse drawing uses a constant pressure. Touch browsing and the Pan tool move
   the page. Ctrl + wheel changes zoom.
3. Use the object eraser to remove complete strokes made by this native preview.
   Undo/redo follows document editing order in this open session, including across pages.
4. Save or Save As writes a PDF containing both standard annotation appearances
   and this preview's editable pressure samples. Reopen that PDF in this preview
   to continue adding or erasing its strokes.
5. Open Settings to choose the system/light/dark theme, sidebar and current pen
   style. Applied preferences persist beside the program. Moving the entire
   portable directory keeps its default relative Backups location.
6. Local backups default to 5 minutes and 10 copies per PDF, only after content
   changes. Count 0 stops creation and preserves old copies. Changing the folder
   affects later backups by default; optional migration copies/verifies first.
7. In Backup Management, inspect source paths and sizes, clean current/checked/all
   PDF groups, or select a snapshot for verified restoration to a separate PDF.
   Restoring does not overwrite the original PDF. Keep ordinary Save separate
   from the backup status.

Older ProperNotes v1-v4 files and unsupported or inconsistent editing data can be
read-only. A read-only banner explains the reason. Editing arbitrary handwriting
from other programs and migrating old ProperNotes data are outside this preview.
The former preview remains separate. This package is compiled but has not been
launched or device-tested by the build workflow. See KNOWN-LIMITATIONS.md.

## Traditional Chinese (zh-TC)

點「選文字」後在同一頁拖選，Ctrl+C 複製。0.3.3 在頁面上方顯示高亮、底線、
波浪線及刪除線按鈕，也顯示套用結果。可從審閱選單設定顏色與不透明度並收藏。
Ctrl+F 搜尋既有文字層，Enter 開始，可選大小寫、停止，點結果跳轉，上限 1,000 筆。
正文 Shift + 滾輪左右平移，Ctrl + 滾輪縮放。全螢幕縮圖的 Ctrl + 滾輪調整預覽尺寸。
0.3.4 可針對混合選區中有可靠座標的正文加標記，並顯示略過的字形數。複製保留整段文字。
若標記超過上限，請縮短選區。
點收藏欄右上角 × 可關閉，從「檢視」重新顯示。主視窗最小化或關閉時，浮動收藏欄也會隱藏。

`0.3.1-native-preview.1` 工作區源碼已接入. 編譯狀態見各構件 provenance,
應用尚未啟動、測試或實際驗收. 開啟文件新增
橫向標籤, 同一登記路徑啟用已有標籤, 最多 32 個. 各標籤保留未儲存內容與編輯/視角歷史;
關閉或退出逐一確認, 取消或儲存失敗保留標籤. 另存保護其他標籤文件, 包含未載入佔位項.
已載入背景文件保留獨立備份修訂及排程.

最近列表最多 100 筆, 可開啟/啟用、逐筆移除或清空, 不刪 PDF、備份或另存的 256 筆
閱讀/會話登記. 工作區中繼資料上限 4MiB, 約 650ms 合併寫入. 啟動恢復預設開啟,
按順序建立按需載入佔位標籤並記住活動標籤/閱讀狀態, 失敗可重試; 重新讀取目前磁碟檔案,
不從會話恢復未儲存墨跡、PDF 正文或撤銷棧. 上次未儲存標記會提示查備份.

小圖示收藏欄支援筆、橡皮、平移、框選/移動和文字框樣式快照, 可新增/編輯/複製/
刪除/拖排、折疊、左右停靠或浮動, 專案上限 128 預設. 編輯收藏不改工具預設或已選 PDF 物件, 點擊後才套用
後續操作. 多收藏欄及其他工具預設另列後續.

0.3.0: 筆直接使用所選工具. 滑鼠左鍵與單指預設瀏覽, 開啟「滑鼠與手跟隨工具」
後才跟隨所選工具; 兩指始終瀏覽. 「持續使用」取消後, 成功使用一次回前一常駐工具.
整筆/局部橡皮可選, 筆尾為臨時擦除. 框選後拖移; 文字框拖出後輸入, Enter換行,
Ctrl+Enter或點框外確認, Esc取消. Ctrl+Z/Y為內容歷史, 與視角返回/前進分開.
「展開頁面」進全螢幕縮圖, 滑杆或Ctrl+滾輪調預覽大小; 點頁跳轉, Esc回原視角.

保持完整程式目錄, 雙擊 `Start-ProperNotes.cmd`. 選擇「開啟 PDF」後, 用筆、
顏色及粗細工具手寫; 壓感取決於 Qt 收到的裝置資料, 滑鼠採固定壓力.
觸控或平移工具瀏覽, Ctrl + 滾輪縮放. 物件橡皮刪除本原型的整條筆跡,
撤銷/重做適用於本次開啟文件的整文件編輯歷史.

儲存或另存後, 標準批注外觀與可編輯的壓感資料同存在 PDF 內. 用本原型
重開可繼續新增和擦除筆跡. 舊版 v1-v4 或不支援的資料可能唯讀, 原因見
介面提示. 本包尚未經啟動及裝置驗收, 詳見 KNOWN-LIMITATIONS.md.

設定與備份: 透過「設定與本地備份」調整主題、側欄、筆色/粗細及備份規則.
預設程式根目錄 Backups, 5 分鐘、每 PDF 10 份; 0 停新增保留舊份.
可選遷移, 管理器支援目前/勾選/全部清理和校驗後另存恢復副本. 不代替普通儲存.

## Simplified Chinese (zh-SC)

点「选文字」后在同一页拖选，Ctrl+C 复制。0.3.3 在页面上方显示高亮、下划线、
波浪线及删除线按钮，也显示应用结果。可从审阅菜单设置颜色与不透明度并收藏。
Ctrl+F 搜索已有文字层，Enter 开始，可选大小写、停止，点结果跳转，上限 1,000 条。
正文 Shift + 滚轮左右平移，Ctrl + 滚轮缩放。全屏缩略图的 Ctrl + 滚轮调整预览尺寸。
0.3.4 可对混合选区中有可靠坐标的正文添加标记，并提示跳过的字形数。复制保留整段文字。
如果标记超过上限，请缩短选区。
点收藏栏右上角 × 可关闭，从「视图」重新显示。主窗口最小化或关闭时，浮动收藏栏也会隐藏。

`0.3.1-native-preview.1` 工作区源码已接入. 编译状态见各构件 provenance,
应用尚未启动、测试或实际验收. 打开文档新增
横向标签, 同一登记路径激活已有标签, 最多 32 个. 各标签保留未保存内容与编辑/视角历史;
关闭或退出逐一确认, 取消或保存失败保留标签. 另存保护其他标签文件, 包含未加载占位项.
已加载后台文档保留独立备份修订及调度.

最近列表最多 100 条, 可打开/激活、逐条移除或清空, 不删 PDF、备份或另存的 256 条
阅读/会话登记. 工作区元数据上限 4MiB, 约 650ms 合并写入. 启动恢复默认开启,
按顺序建立按需加载占位标签并记住活动标签/阅读状态, 失败可重试; 重新读取当前磁盘文件,
不从会话恢复未保存墨迹、PDF 正文或撤销栈. 上次未保存标记会提示查备份.

小图标收藏栏支持笔、橡皮、平移、框选/移动和文字框样式快照, 可新建/编辑/复制/
删除/拖排、折叠、左右停靠或浮动, 项目上限 128 预设. 编辑收藏不改工具默认或已选 PDF 对象, 点击后才应用
后续操作. 多收藏栏及其他工具预设另列后续.

0.3.0: 笔直接使用所选工具. 鼠标左键与单指默认浏览, 开启「鼠标与手跟随工具」
后才跟随所选工具; 两指始终浏览. 「持续使用」取消后, 成功使用一次回前一常驻工具.
整笔/局部橡皮可选, 笔尾为临时擦除. 框选后拖移; 文字框拖出后输入, Enter换行,
Ctrl+Enter或点框外确认, Esc取消. Ctrl+Z/Y为内容历史, 与视角返回/前进分开.
「展开页面」进全屏缩略图, 滑杆或Ctrl+滚轮调预览大小; 点页跳转, Esc回原视角.

保持完整程序目录, 双击 `Start-ProperNotes.cmd`. 选择「打开 PDF」后, 用笔、
颜色及粗细工具手写; 压感取决于 Qt 收到的设备数据, 鼠标采用固定压力.
触摸或平移工具浏览, Ctrl + 滚轮缩放. 对象橡皮删除本原型的整条笔迹,
撤销/重做适用于本次打开文档的整文档编辑历史.

保存或另存后, 标准批注外观与可编辑的压感数据同存在 PDF 内. 用本原型
重开可继续新增和擦除笔迹. 旧版 v1-v4 或不支持的数据可能只读, 原因见
界面提示. 本包尚未经过启动及设备验收, 详见 KNOWN-LIMITATIONS.md.

设置与备份: 通过「设置与本地备份」调整主题、侧栏、笔色/粗细及备份规则.
默认程序根目录 Backups, 5 分钟、每 PDF 10 份; 0 停新增保留旧份.
可选迁移, 管理器支持当前/勾选/全部清理和校验后另存恢复副本. 不代替普通保存.
