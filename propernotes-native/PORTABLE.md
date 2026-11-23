# ProperNotes native preview - portable use

Original explanatory text: CC BY-SA 4.0. See COPYRIGHT.md.

## English (en)

### 0.3.13 resize from the palette edges

Unlock the floating palette with the left-rail pin button, then drag its top or
bottom edge directly to adjust the height. The large resize-icon rows have been
removed. Keep using the bottom movement handle to move or dock the palette.
Locking disables edge resizing, and a docked palette keeps its full side-strip
height. Existing PDF behavior is retained. Cloud compilation and packaging are
pending at source preparation time. No installation, application launch or test
is performed on the recipient's PC.

### 0.3.12 icons, docking and pen settings

The sidebar, favorite tools and bottom controls use consistently scaled vector
glyphs, with point-size badges positioned separately. High-DPI icon rendering
and spacing are corrected without changing the existing tool commands or saved
document format. Cloud compilation and packaging are pending at source
preparation time. The package is not installed, started or tested on the
recipient's PC. Follow its build provenance for the actual compilation result.

Unlock the palette using the left-rail toolbar-and-pin button. Drag its bottom
move handle toward the left or right reading edge and release when the docking
preview appears. The dock occupies layout space. Drag the unlocked handle away
from the edge to float again; its former floating size is retained. Pinning hides
the move/resize handles in either mode. The follow-tool switch uses a mouse icon;
Pan remains a hand. Settings > Pen and input explains the default pressure curve,
while a saved pen's pressure is edited from that preset's options. The Windows
settings button launches the system Pen & Windows Ink page only on user click.

### 0.3.11 compact native layout

Use the narrow left rail for page/annotation navigation, the mouse-and-touch
follow-tool switch, palette locking, full screen and the application settings gear.
The floating palette keeps Pan and Select above the scrollable favorite tools.
Use a wheel or one finger to scroll its middle area. Its bottom + stays visible
and opens shared tool properties, with Add to Toolbar for a saved preset and Use
for immediate activation. Select a preset and open its options to change it.

Unlock the palette from the left rail to show the movement handle below + and
the height handles at its edges. Drag the movement handle to reposition it or an
edge to make it taller or shorter. Locking hides these handles. The app remembers
the palette geometry and lock state. Fixed top and bottom controls remain available
when favorites overflow. Tool icons use the preset color and show applicable pt values.

View Back/Forward and Undo/Redo appear near the lower-left corner. Zoom and
Fit Width/Page/Height appear near the lower-right corner. Reveal a hidden group
from its corner. Related operations show their controls. Hover, keyboard focus,
touch interaction and open menus keep them visible. The idle hide delay defaults
to 3 seconds and can be set from 1 to 30 seconds in the categorized Settings window.

This revision is awaiting cloud compilation and portable assembly at source
preparation time. Build provenance records the actual result. Packaging does not
install, start or test the app on the recipient's PC. Older instructions below
also document earlier versions and their previous toolbar placements.

The left sidebar has **Pages** and **Annotations** tabs. Annotations lists this
program's editable objects, with current-page or whole-document scope and a kind
filter. Click an entry to navigate to it. Review marks are selected only when
**Include text review marks** is on. Find that setting in the Select/Move or
Eraser toolbar, or in Settings. The choice is saved.

Choose Line, Arrow, Rectangle or Ellipse from **Shapes** (形状). Drag to preview
and release to draw. Hold Shift to constrain lines and arrows to 45° increments,
or rectangles and ellipses to squares and circles. Save the current shape as a
favorite with its outline, fill and continuous or single-use setting. Use the
whole-object eraser to remove a shape by touching its outline or visible fill.

In **Select / Move** (框选 / 移动), select one or more strokes or shapes. Drag a
corner to scale the selection and its stroke widths together.
Drag the rotation handle to rotate about the selection center. Hold Shift to snap
to 15° increments. Use the pen, or enable **Mouse & Touch Follow Tool** to drag
these handles with a mouse or one finger.

Select ink, shapes, text boxes or a group of them. Use **Cut** or **Copy** beside the selection and
**Paste Annotations** (粘贴批注) in the toolbar, or use Ctrl+X, Ctrl+C and Ctrl+V.
Paste into another page of the same open PDF to keep the selected annotations
editable. Paste selects the new objects near the center of the visible page.
Each completed transform, cut or paste can be undone.

In Settings, **Include text review marks in Select and Erase** is on by default.
It covers editable highlights, underlines, squiggly lines and strikeouts, which
stay attached to their text. In mixed selections, drag the dashed frame to move
ink, shapes and text boxes. Dotted outlines show the attached marks. Delete removes the
entire selection. For cut or copy, select movable annotations without review marks.
The eraser removes a whole review mark when enabled. Partial erase
preserves shapes and text boxes. Freehand highlighter strokes follow the same eraser rules
as pen strokes.

The floating favorites bar has a pin button. Pin it where you want it, and unlock
it before dragging it again. The position and pin state are restored on restart.

With **Mouse & Touch Follow Tool** off, use the mouse or one finger to pan,
drag in **Select Text** for manual selection, or drag in **Select / Move** to select and
move existing annotations. Turn on the switch to draw, erase or apply a review tool
automatically on release with the mouse or one finger. Toolbar, favorites and visible
style controls remain clickable.

For an existing pen or freehand highlighter stroke, use **Select / Move**
(框选 / 移动), select one stroke and click **Style** (样式) by the selection.
Apply color, width and opacity together. Ctrl+Z undoes the style change in one
step. Save the PDF and reopen it to keep editing the stroke.

Select **Freehand Highlighter** (自由荧光笔) and draw directly on the PDF page.
Image-only scans work too. Turn on **Mouse & Touch Follow Tool** to draw with
the left mouse button or one finger. Adjust color, width and opacity in the
style row, then use **Favorite Current Tool** to save the settings as a preset.
Use Ctrl+Z or Ctrl+Y to undo or redo. Save the PDF, then reopen it in this version
to select, move or erase the highlighter strokes.

Click **Select Text** (选文字), then drag over the PDF text on one page. In 0.3.3,
the strip above the page shows the four review buttons and what happened to the selection.
Use Ctrl+C to copy or click a review button to mark the selection. The review
menu lets you choose Highlight, Underline, Squiggly or StrikeOut, change color and
opacity, and save the tool as a favorite. Activate a tool under **Text Review →
Choose Review Tool** (正文审阅 → 选用审阅工具) or from the favorites bar, then drag
across text to mark it on release. With **Keep Tool Active**
(持续使用) off, one successful mark returns to the previous persistent tool. Cancelled
and unsuccessful selections keep the tool active. Set a favorite's saved mode in its editor.
Search with Ctrl+F, Enter to start, and click a result to jump.
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

Pen input uses the selected tool. Enable the mouse-and-touch switch to draw,
erase or create a text box with the left mouse button or one finger.
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

### 0.3.13 直接拖動工具框邊緣

先用左側圖釘解除固定，再直接拖動懸浮工具框的上、下邊緣調整高度。上下大型拉伸圖示已移除，底部移動把手仍用於移動及停靠。鎖定時不能拉伸，停靠時維持側欄整高，PDF 操作保留原有邏輯。源碼準備時仍待雲端編譯及打包，不在使用者 PC 安裝、啟動應用或執行測試。

### 0.3.12 圖示、停靠及筆設定

側邊欄、收藏工具與底角控件的向量圖示採統一尺寸，pt 角標獨立排版，並修正高 DPI 顯示和間距。工具命令及 PDF 儲存格式維持原有邏輯。源碼準備時仍待雲端編譯及打包，不在使用者 PC 安裝、啟動或測試，實際編譯結果以包內構建紀錄為準。

左側「工具欄與圖釘」解除固定後，從底部把手拖到閱讀區左右邊緣，出現預覽後釋放即可停靠。停靠欄會留出版面空間；解鎖後拖離邊緣可恢復懸浮及原尺寸。跟隨工具使用滑鼠圖示，平移保留手形。「設定 → 筆與輸入」說明預設壓感，收藏筆的壓感在該預設選項中修改。Windows 筆設定入口僅在點擊後開啟系統頁。

### 0.3.11 精簡原生版面

左側窄邊欄提供頁面／批注入口、滑鼠與手跟隨工具、工具框固定、全螢幕及完整軟體設定。浮動工具框頂部固定平移和框選，中間收藏區可用滾輪或單指上下捲動，底部「＋」始終可用。同一工具與選項窗口提供「加入工具欄」及「使用」，選中收藏預設後可開啟其選項，圖示反映顏色及適用的 pt 數值。

從左側解除固定後，「＋」下方出現移動把手，上下沿出現高度把手，可移動、拉長或縮短工具框。鎖定後隱藏這些把手，位置、高度和鎖定狀態會保留。收藏過多時，頂部及底部控件維持可見。

左下顯示視角返回／前進及復原／重做，右下提供縮放及適寬／適頁／適高。隱藏後可從對應底角喚出，相關操作也會顯示控件。懸停、鍵盤焦點、觸控操作及展開選單期間保持顯示，空閒隱藏預設 3 秒，可在分類設定調整為 1 至 30 秒。

此修訂在源碼準備時仍待雲端編譯與打包，實際結果以構件 provenance 為準，不在使用者 PC 安裝、啟動或測試應用。下方保留舊版的功能說明及當時控件位置。

左側欄的「页面／批注」可切換縮圖及本應用的批注列表。可按目前頁面或整份 PDF、批注類型篩選，點選條目即可定位。選取正文標記會遵循「包含正文标记」開關，該開關可從框選或橡皮的工具列及設定頁操作，選擇會儲存。

從「形状」選擇直線、箭頭、矩形或橢圓，拖曳時預覽，放開後建立。按住 Shift 可將直線和箭頭約束為 45° 的倍數，將矩形和橢圓約束為正方形和正圓。線條、填充和持續或單次模式可收藏。整筆橡皮碰到邊線或可見填充即可移除整個圖形。

在「框选 / 移动」中選取筆跡或圖形，拖曳角點可等比縮放並同步調整線寬，拖曳旋轉把手可繞選區中心旋轉，按住 Shift 可每 15° 吸附。可用手寫筆拖曳，滑鼠或單指拖曳需開啟「鼠标与手跟随工具」。

選取筆跡、圖形或文字方塊，點選區旁的「剪切」「复制」，再於同一份 PDF 的目標頁點工具列上的「粘贴批注」。也支援 Ctrl+X、Ctrl+C、Ctrl+V。貼上後會選取目前頁面可見區域中央附近的副本。變換、剪下和貼上各可復原。浮動收藏欄可按圖釘固定，重啟後恢復位置和固定狀態。

設定中的「框选和橡皮作用于正文审阅标记」預設開啟，適用於可編輯的高亮、底線、波浪線和刪除線。混選時，拖曳虛線框會移動筆跡、圖形和文字方塊，點線輪廓的審閱標記仍附在原文字上。「删除」會移除整個選區。剪貼時請單獨選取可移動的批注。開啟設定時，橡皮碰到審閱標記會移除整項。局部擦除會保留圖形和文字方塊，自由螢光筆跡沿用普通筆跡的擦除規則。

關閉「鼠标与手跟随工具」時，可用滑鼠左鍵或單指平移、在「选文字」模式手動選字，或在「框选 / 移动」模式選取和移動既有批注。若要用滑鼠或單指繪製、擦除或拖選後自動加審閱標記，請開啟開關。工具列、收藏欄及選區旁的「样式」按鈕可直接點擊。

點「選文字」後在同一頁拖選，Ctrl+C 複製。0.3.3 在頁面上方顯示高亮、底線、
波浪線及刪除線按鈕，也顯示套用結果。從「正文审阅」開啟「选用审阅工具」，也可用「收藏当前审阅工具」加入收藏欄。啟用後拖選正文，放開時套用。關閉「持续使用」後，成功標記一次即回到上一個持續工具。取消或未成功時保留目前工具。編輯收藏可儲存其持續或單次模式。
Ctrl+F 搜尋既有文字層，Enter 開始，可選大小寫、停止，點結果跳轉，上限 1,000 筆。
正文 Shift + 滾輪左右平移，Ctrl + 滾輪縮放。全螢幕縮圖的 Ctrl + 滾輪調整預覽尺寸。
用「框选 / 移动」選取一條普通筆跡或自由螢光筆跡，再點選區旁的「样式」。套用顏色、粗細和不透明度後，可按 Ctrl+Z 一次復原。儲存並重開 PDF 後仍可編輯該筆跡。

選「自由荧光笔」可直接在 PDF 頁面上劃線，掃描頁也能使用。要用滑鼠左鍵或單指劃線，先開啟「鼠标与手跟随工具」。在屬性列調整顏色、粗細和不透明度，再用「收藏当前工具」存成預設。Ctrl+Z/Ctrl+Y 復原或重做。儲存 PDF 後用本版重開，可選取、移動或擦除螢光筆跡。

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

筆直接使用所選工具。開啟「鼠标与手跟随工具」後，可用滑鼠左鍵或單指繪製、擦除或新增文字框。
兩指可瀏覽。取消「持续使用」後，成功使用一次就回前一個持續工具。
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

### 0.3.13 直接拖动工具框边缘

先用左侧图钉解除固定，再直接拖动悬浮工具框的上、下边缘调整高度。上下大型拉伸图标已移除，底部移动把手仍用于移动和停靠。锁定时不能拉伸，停靠时保持侧栏整高，PDF 操作保留原有逻辑。源码准备时仍待云端编译和打包，不在用户 PC 安装、启动应用或运行测试。

### 0.3.12 图标、停靠及笔设置

侧边栏、收藏工具和底角控件的矢量图标采用统一尺寸，pt 角标独立排版，并修正高 DPI 显示和间距。工具命令及 PDF 保存格式维持原有逻辑。源码准备时仍待云端编译和打包，不在用户 PC 安装、启动或测试，实际编译结果以包内构建记录为准。

左侧「工具栏与图钉」解除固定后，从底部把手拖到阅读区左右边缘，出现预览后释放即可停靠。停靠栏会留出版面空间；解锁后拖离边缘可恢复悬浮和原尺寸。跟随工具使用鼠标图标，平移保留手形。「设置 → 笔与输入」说明默认压感，收藏笔的压感在该预设选项中修改。Windows 笔设置入口仅在点击后打开系统页。

### 0.3.11 紧凑原生布局

左侧窄边栏提供页面／批注入口、鼠标与手跟随工具、工具框固定、全屏及完整软件设置。浮动工具框顶部固定平移和框选，中间收藏区可用滚轮或单指上下滚动，底部「＋」始终可用。同一工具与选项窗口提供「加入工具栏」和「使用」，选中收藏预设后可打开其选项，图标反映颜色和适用的 pt 数值。

从左侧解除固定后，「＋」下方出现移动把手，上下沿出现高度把手，可移动、拉长或缩短工具框。锁定后隐藏这些把手，位置、高度和锁定状态会保留。收藏过多时，顶部和底部控件维持可见。

左下显示视角返回／前进及撤销／重做，右下提供缩放和适宽／适页／适高。隐藏后可从对应底角唤出，相关操作也会显示控件。悬停、键盘焦点、触控操作和展开菜单期间保持显示，空闲隐藏默认 3 秒，可在分类设置调整为 1 至 30 秒。

此修订在源码准备时仍待云端编译和打包，实际结果以构件 provenance 为准，不在用户 PC 安装、启动或测试应用。下方保留旧版的功能说明和当时控件位置。

左侧栏的「页面／批注」可切换缩略图和本应用的批注列表。可按当前页或整份 PDF、批注类型筛选，点击条目即可定位。选中正文标记会遵循「包含正文标记」开关，该开关可从框选或橡皮的工具栏及设置页操作，选择会保存。

从「形状」选择直线、箭头、矩形或椭圆，拖动时预览，松开后创建。按住 Shift 可将直线和箭头约束为 45° 的倍数，将矩形和椭圆约束为正方形和正圆。线条、填充和持续或单次模式可收藏。整笔橡皮碰到边线或可见填充即可删除整个形状。

在「框选 / 移动」中选中笔迹或形状，拖动角点可等比缩放并同步调整线宽，拖动旋转手柄可绕选区中心旋转，按住 Shift 可每 15° 吸附。可用手写笔拖动，鼠标或单指拖动需打开「鼠标与手跟随工具」。

选中笔迹、形状或文字框，点选区旁的「剪切」「复制」，再到同一份 PDF 的目标页点工具栏上的「粘贴批注」。也支持 Ctrl+X、Ctrl+C、Ctrl+V。粘贴后会选中当前页可见区域中央附近的副本。变换、剪切和粘贴各可撤销。浮动收藏栏可点图钉固定，重启后恢复位置和固定状态。

设置中的「框选和橡皮作用于正文审阅标记」默认开启，适用于可编辑的高亮、下划线、波浪线和删除线。混选时，拖动虚线框会移动笔迹、形状和文字框，点线轮廓的审阅标记仍附在原文字上。「删除」会移除整个选区。剪贴时请单独选中可移动的批注。开启设置时，橡皮碰到审阅标记会移除整项。局部擦除会保留形状和文字框，自由荧光笔迹沿用普通笔迹的擦除规则。

关闭「鼠标与手跟随工具」时，可用鼠标左键或单指平移、在「选文字」模式手动选字，或在「框选 / 移动」模式框选和移动已有批注。要用鼠标或单指画、擦，或在拖选后自动加审阅标记，请打开开关。工具栏、收藏栏和选区旁的「样式」按钮可以直接点击。

点「选文字」后在同一页拖选，Ctrl+C 复制。0.3.3 在页面上方显示高亮、下划线、
波浪线及删除线按钮，也显示应用结果。从「正文审阅」打开「选用审阅工具」，也可用「收藏当前审阅工具」加入收藏栏。启用后拖选正文，松开时应用。关闭「持续使用」后，成功标记一次就回到上一个持续工具。取消或未成功时保留当前工具。编辑收藏可保存其持续或单次模式。
Ctrl+F 搜索已有文字层，Enter 开始，可选大小写、停止，点结果跳转，上限 1,000 条。
正文 Shift + 滚轮左右平移，Ctrl + 滚轮缩放。全屏缩略图的 Ctrl + 滚轮调整预览尺寸。
用「框选 / 移动」选中一条普通笔迹或自由荧光笔迹，再点选区旁的「样式」。应用颜色、粗细和不透明度后，可按 Ctrl+Z 一次撤销。保存并重开 PDF 后仍可编辑该笔迹。

选「自由荧光笔」可直接在 PDF 页面上划线，扫描页也能使用。要用鼠标左键或单指划线，先打开「鼠标与手跟随工具」。在属性栏调颜色、粗细和不透明度，再用「收藏当前工具」存成预设。Ctrl+Z/Ctrl+Y 撤销或重做。保存 PDF 后用本版重开，可选取、移动或擦除荧光笔迹。

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

笔直接使用所选工具。打开「鼠标与手跟随工具」后，可用鼠标左键或单指画、擦或新建文字框。
两指可浏览。取消「持续使用」后，成功使用一次就回上一个持续工具。
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
