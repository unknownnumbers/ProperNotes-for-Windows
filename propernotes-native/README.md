# ProperNotes for Windows - native Qt prototype

Developed through human and AI collaboration. Some AI-assisted material has not received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協作內容尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协作内容尚未经过完整人工审查.

## English (en)

The 0.3.0 source adds four reading layouts (single/facing, paged/continuous), fit
width/page/height, independent view back/forward, full-screen resizable page previews,
document-wide session undo/redo, whole/partial erasure, selection/movement and editable
plain-text boxes. The pen has pressure enable/sensitivity, color, width and independent
opacity. Creation/erasure tools have remembered persistent/single-use modes; successful
single use returns to the previous persistent tool. Flipping a supported pen to its
eraser end temporarily uses the chosen eraser mode and preserves the selected tool.

The mouse-and-touch annotation switch starts off: pen input uses its selected tool,
while mouse-left/single-finger input pans. Turning it on lets mouse-left/single-finger
input use that tool. Two fingers always browse, and pen contact suppresses canvas touch.
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

Native edit data is versioned inside the PDF with standard Ink/FreeText annotations and compatible appearances. No adjacent project file is required. The older web preview's v1-v4 editing data, and unknown/inconsistent data, are protected from overwrite and may be opened read-only. Full web-legacy migration is outside this prototype; native v1 ink is supported.

The 0.2.1 preview adds native settings for system/light/dark theme, page-sidebar visibility/width and the current pen style. Preferences are stored separately from PDF content in `propernotes-native-settings.json` beside the program. Corrupt, future or externally changed settings are not silently overwritten. Local recovery backups default to `Backups` beside the program, every 5 minutes after content changes, retaining 10 copies per PDF. Count 0 stops new backups and preserves existing copies. Registered old directories remain manageable after a directory change; migration is optional. The manager supports current/selected/all PDF cleanup and verified restoration to a separate PDF. Backup success never marks the current document as saved. Legacy web-preview backup catalogs are not imported by this native catalog.

Large-document capacity is not yet established. The current input/output guard remains 512MiB; [the design record](LARGE-DOCUMENTS.md) explains why page caching alone cannot establish 1GB support.

Use `Start-ProperNotes.cmd` only in a complete compiled portable package. This program does not need WebView2, Node.js or a local Qt SDK. Build prerequisites and downloads are restricted to the GitHub-hosted build recipe under `cloud`; normal startup never builds or installs them. Consult packaged provenance and validation records for actual compilation and acceptance status. The application and tests are not launched by the recipe.

Original code is GPL-3.0-only; original explanatory documentation is CC BY-SA 4.0. Third-party terms, source and notices are preserved separately. See COPYRIGHT.md and the packaged licenses.

## Traditional Chinese (zh-TC)

0.3.0 原生源碼增加單/雙頁及連續/分頁閱讀、適寬/適頁/適高、獨立視角歷史、
真正全螢幕且可調尺寸的縮圖、整文件會話撤銷/重做、整筆/局部擦除、框選移動及
純文字框. 筆有壓感開關/靈敏度、顏色/粗細/透明度, 工具持續/單次設定可記憶.
滑鼠與觸摸跟隨工具開關預設關閉: 筆保持所選工具, 滑鼠左鍵/單指瀏覽; 開啟後
兩者也使用所選工具. 兩指始終瀏覽, 筆接觸時抑制畫布觸摸. Ctrl+滾輪分別調整
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

0.3.0 原生源码增加单/双页及连续/分页阅读、适宽/适页/适高、独立视角历史、
真正全屏且可调尺寸的缩略图、整文档会话撤销/重做、整笔/局部擦除、框选移动及
纯文字框. 笔有压感开关/灵敏度、颜色/粗细/透明度, 工具持续/单次设置可记忆.
鼠标与触摸跟随工具开关默认关闭: 笔保持所选工具, 鼠标左键/单指浏览; 开启后
两者也使用所选工具. 两指始终浏览, 笔接触时抑制画布触摸. Ctrl+滚轮分别调整
正文倍率或全屏缩略图尺寸. 这是本项目确认规则, 不宣称全部 Drawboard 逻辑已核实.
原生数据 v2 保存墨迹及文字框, 旧原生 v1 墨迹可编辑. 文字外观含 alpha 位图,
Unicode 原文仍可编辑; 不打包字体, 缺字体时编辑覆盖层可能回退. 历史不写入 PDF.
目前只有源码/构建交付流程, 启动、设备、文件往返和异常恢复仍待验收.

此独立原生原型使用 Qt 6.11.3 与 PDF4QT 1.7.0.0, 提供上述阅读/基础批注范围及单 PDF 保存重开编辑. 启用批注后的鼠标/触摸采用固定压力. PDF 渲染及保存于工作线程串行处理, 实时覆盖层分开.

工具栏提供笔、橡皮、平移、框选/移动与文字框, 输入及单次模式按用户确认规则实现. 设备、防误触、延迟和 PDF 兼容性尚未验收, 亦未移植前版全部功能.

原生编辑数据与标准批注外观一起存在 PDF 中, 不需要旁边的工程文件. 旧 Web 版 v1-v4、未知或不一致的数据禁止覆盖; 完整旧 Web 格式迁移另行安排. 原生 v1 墨迹可编辑.

0.2.1 增加原生设置及偏好记忆: 跟随系统/浅/深主题、页面侧栏显隐/宽度和当前笔色/粗细. 设置文件为程序旁的 `propernotes-native-settings.json`, 不含 PDF 正文; 损坏、未来版本或外部改动不静默覆盖. 本地备份默认程序根目录 `Backups`, 每 5 分钟内容变化才新增, 每 PDF 保留 10 份; 数量 0 停新增并保留旧份. 目录变更默认不迁移, 旧登记目录仍可管理, 可另选迁移. 管理器可清理当前/勾选/全部 PDF 备份, 校验后另存恢复副本. 备份不替代普通保存, 旧 Web 版目录格式暂不导入. 目前仍限制 512MiB, 1GB 级改造见 LARGE-DOCUMENTS.md, 尚未验收.

完整便携包以 `Start-ProperNotes.cmd` 启动, 不需要 WebView2、Node.js 或本机 Qt SDK. 依赖下载及构建只在 GitHub 托管流程执行, 普通启动不构建或安装. 实际编译及验收范围见包内记录; 构建流程不启动应用或测试. 原创代码 GPL-3.0-only, 原创说明 CC BY-SA 4.0, 第三方保留原条款.
