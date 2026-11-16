# ProperNotes for Windows - native Qt prototype

Developed through human and AI collaboration. Some AI-assisted material has not received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協作內容尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协作内容尚未经过完整人工审查.

## English (en)

This separate native prototype uses Qt 6.11.3 and PDF4QT 1.7.0.0. Its scope is opening ordinary PDFs, pressure-aware handwriting through Qt tablet events, stroke erasure and undo, saving one PDF, and recovering editable strokes after reopening. Mouse input provides a constant-pressure fallback. PDF rendering and saving are serialized on a worker thread; the live ink preview is separate.

The toolbar exposes explicit prototype Pen, Eraser and Pan tools. It does not claim the unverified Drawboard device-mode switch or Tool Lock behavior. Device, palm-rejection, latency and PDF compatibility acceptance remain unperformed. This is a focused prototype, not a port of every feature from the previous preview.

Native edit data is versioned inside the PDF together with standard Ink annotations and pressure-shaped vector appearances. No adjacent project file is required. Older ProperNotes v1-v4 editing data and unknown or inconsistent edit data are protected from overwrite and may be opened read-only. Full legacy migration is outside this prototype.

The 0.2.1 preview adds native settings for system/light/dark theme, page-sidebar visibility/width and the current pen style. Preferences are stored separately from PDF content in `propernotes-native-settings.json` beside the program. Corrupt, future or externally changed settings are not silently overwritten. Local recovery backups default to `Backups` beside the program, every 5 minutes after content changes, retaining 10 copies per PDF. Count 0 stops new backups and preserves existing copies. Registered old directories remain manageable after a directory change; migration is optional. The manager supports current/selected/all PDF cleanup and verified restoration to a separate PDF. Backup success never marks the current document as saved. Legacy web-preview backup catalogs are not imported by this native catalog.

Large-document capacity is not yet established. The current input/output guard remains 512MiB; [the design record](LARGE-DOCUMENTS.md) explains why page caching alone cannot establish 1GB support.

Use `Start-ProperNotes.cmd` only in a complete compiled portable package. This program does not need WebView2, Node.js or a local Qt SDK. Build prerequisites and downloads are restricted to the GitHub-hosted build recipe under `cloud`; normal startup never builds or installs them. Consult packaged provenance and validation records for actual compilation and acceptance status. The application and tests are not launched by the recipe.

Original code is GPL-3.0-only; original explanatory documentation is CC BY-SA 4.0. Third-party terms, source and notices are preserved separately. See COPYRIGHT.md and the packaged licenses.

## Traditional Chinese (zh-TC)

此獨立原生原型使用 Qt 6.11.3 與 PDF4QT 1.7.0.0, 範圍為開啟普通 PDF、透過 Qt 平板事件手寫及接收壓感、整筆擦除和撤銷、儲存單一 PDF, 以及重開恢復可編輯筆跡. 滑鼠提供固定壓力回退. PDF 渲染及儲存於工作執行緒串行處理, 即時筆跡預覽分開.

工具欄提供明確的原型筆、橡皮及平移工具, 不宣稱已復刻未核實的 Drawboard 裝置模式或 Tool Lock. 裝置、防誤觸、延遲和 PDF 相容性尚未驗收, 亦未移植前版全部功能.

原生編輯資料與標準 Ink 批注及壓感向量外觀一起存在 PDF 中, 不需要旁邊的工程檔. 舊版 v1-v4、未知或不一致的編輯資料禁止覆蓋, 可按限制唯讀開啟; 完整遷移另行安排.

0.2.1 增加原生設定及偏好記憶: 跟隨系統/淺/深主題、頁面側欄顯隱/寬度和目前筆色/粗細. 設定檔為程式旁的 `propernotes-native-settings.json`, 不含 PDF 正文; 損壞、未來版本或外部改動不靜默覆蓋. 本地備份預設程式根目錄 `Backups`, 每 5 分鐘內容變動才新增, 每 PDF 保留 10 份; 數量 0 停新增並保留舊份. 目錄變更預設不遷移, 舊登記目錄仍可管理, 可另選遷移. 管理器可清理目前/勾選/全部 PDF 備份, 校驗後另存恢復副本. 備份不替代普通儲存, 舊 Web 版目錄格式暫不匯入. 目前仍限制 512MiB, 1GB 級改造見 LARGE-DOCUMENTS.md, 尚未驗收.

完整便攜包以 `Start-ProperNotes.cmd` 啟動, 不需要 WebView2、Node.js 或本機 Qt SDK. 依賴下載及構建只在 GitHub 託管流程執行, 普通啟動不構建或安裝. 實際編譯及驗收範圍見包內紀錄; 構建流程不啟動應用或測試. 原創程式 GPL-3.0-only, 原創說明 CC BY-SA 4.0, 第三方保留原條款.

## Simplified Chinese (zh-SC)

此独立原生原型使用 Qt 6.11.3 与 PDF4QT 1.7.0.0, 范围为打开普通 PDF、通过 Qt 平板事件手写及接收压感、整笔擦除和撤销、保存单一 PDF, 以及重开恢复可编辑笔迹. 鼠标提供固定压力回退. PDF 渲染及保存于工作线程串行处理, 实时笔迹预览分开.

工具栏提供明确的原型笔、橡皮及平移工具, 不宣称已经复刻未核实的 Drawboard 设备模式或 Tool Lock. 设备、防误触、延迟和 PDF 兼容性尚未验收, 亦未移植前版全部功能.

原生编辑数据与标准 Ink 批注及压感矢量外观一起存在 PDF 中, 不需要旁边的工程文件. 旧版 v1-v4、未知或不一致的编辑数据禁止覆盖, 可按限制只读打开; 完整迁移另行安排.

0.2.1 增加原生设置及偏好记忆: 跟随系统/浅/深主题、页面侧栏显隐/宽度和当前笔色/粗细. 设置文件为程序旁的 `propernotes-native-settings.json`, 不含 PDF 正文; 损坏、未来版本或外部改动不静默覆盖. 本地备份默认程序根目录 `Backups`, 每 5 分钟内容变化才新增, 每 PDF 保留 10 份; 数量 0 停新增并保留旧份. 目录变更默认不迁移, 旧登记目录仍可管理, 可另选迁移. 管理器可清理当前/勾选/全部 PDF 备份, 校验后另存恢复副本. 备份不替代普通保存, 旧 Web 版目录格式暂不导入. 目前仍限制 512MiB, 1GB 级改造见 LARGE-DOCUMENTS.md, 尚未验收.

完整便携包以 `Start-ProperNotes.cmd` 启动, 不需要 WebView2、Node.js 或本机 Qt SDK. 依赖下载及构建只在 GitHub 托管流程执行, 普通启动不构建或安装. 实际编译及验收范围见包内记录; 构建流程不启动应用或测试. 原创代码 GPL-3.0-only, 原创说明 CC BY-SA 4.0, 第三方保留原条款.
