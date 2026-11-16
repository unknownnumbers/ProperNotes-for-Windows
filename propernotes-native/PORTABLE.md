# ProperNotes native preview - portable use

Original explanatory text: CC BY-SA 4.0. See COPYRIGHT.md.

## English (en)

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
