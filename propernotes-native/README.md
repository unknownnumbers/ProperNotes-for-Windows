# ProperNotes for Windows - native Qt prototype

Developed through human and AI collaboration. Some AI-assisted material has not received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協作內容尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协作内容尚未经过完整人工审查.

## English (en)

This separate native prototype uses Qt 6.11.3 and PDF4QT 1.7.0.0. Its scope is opening ordinary PDFs, pressure-aware handwriting through Qt tablet events, stroke erasure and undo, saving one PDF, and recovering editable strokes after reopening. Mouse input provides a constant-pressure fallback. PDF rendering and saving are serialized on a worker thread; the live ink preview is separate.

The toolbar exposes explicit prototype Pen, Eraser and Pan tools. It does not claim the unverified Drawboard device-mode switch or Tool Lock behavior. Device, palm-rejection, latency and PDF compatibility acceptance remain unperformed. This is a focused prototype, not a port of every feature from the previous preview.

Native edit data is versioned inside the PDF together with standard Ink annotations and pressure-shaped vector appearances. No adjacent project file is required. Older ProperNotes v1-v4 editing data and unknown or inconsistent edit data are protected from overwrite and may be opened read-only. Full legacy migration is outside this prototype.

Use `Start-ProperNotes.cmd` only in a complete compiled portable package. This program does not need WebView2, Node.js or a local Qt SDK. Build prerequisites and downloads are restricted to the GitHub-hosted build recipe under `cloud`; normal startup never builds or installs them. Consult packaged provenance and validation records for actual compilation and acceptance status. The application and tests are not launched by the recipe.

Original code is GPL-3.0-only; original explanatory documentation is CC BY-SA 4.0. Third-party terms, source and notices are preserved separately. See COPYRIGHT.md and the packaged licenses.

## Traditional Chinese (zh-TC)

此獨立原生原型使用 Qt 6.11.3 與 PDF4QT 1.7.0.0, 範圍為開啟普通 PDF、透過 Qt 平板事件手寫及接收壓感、整筆擦除和撤銷、儲存單一 PDF, 以及重開恢復可編輯筆跡. 滑鼠提供固定壓力回退. PDF 渲染及儲存於工作執行緒串行處理, 即時筆跡預覽分開.

工具欄提供明確的原型筆、橡皮及平移工具, 不宣稱已復刻未核實的 Drawboard 裝置模式或 Tool Lock. 裝置、防誤觸、延遲和 PDF 相容性尚未驗收, 亦未移植前版全部功能.

原生編輯資料與標準 Ink 批注及壓感向量外觀一起存在 PDF 中, 不需要旁邊的工程檔. 舊版 v1-v4、未知或不一致的編輯資料禁止覆蓋, 可按限制唯讀開啟; 完整遷移另行安排.

完整便攜包以 `Start-ProperNotes.cmd` 啟動, 不需要 WebView2、Node.js 或本機 Qt SDK. 依賴下載及構建只在 GitHub 託管流程執行, 普通啟動不構建或安裝. 實際編譯及驗收範圍見包內紀錄; 構建流程不啟動應用或測試. 原創程式 GPL-3.0-only, 原創說明 CC BY-SA 4.0, 第三方保留原條款.

## Simplified Chinese (zh-SC)

此独立原生原型使用 Qt 6.11.3 与 PDF4QT 1.7.0.0, 范围为打开普通 PDF、通过 Qt 平板事件手写及接收压感、整笔擦除和撤销、保存单一 PDF, 以及重开恢复可编辑笔迹. 鼠标提供固定压力回退. PDF 渲染及保存于工作线程串行处理, 实时笔迹预览分开.

工具栏提供明确的原型笔、橡皮及平移工具, 不宣称已经复刻未核实的 Drawboard 设备模式或 Tool Lock. 设备、防误触、延迟和 PDF 兼容性尚未验收, 亦未移植前版全部功能.

原生编辑数据与标准 Ink 批注及压感矢量外观一起存在 PDF 中, 不需要旁边的工程文件. 旧版 v1-v4、未知或不一致的编辑数据禁止覆盖, 可按限制只读打开; 完整迁移另行安排.

完整便携包以 `Start-ProperNotes.cmd` 启动, 不需要 WebView2、Node.js 或本机 Qt SDK. 依赖下载及构建只在 GitHub 托管流程执行, 普通启动不构建或安装. 实际编译及验收范围见包内记录; 构建流程不启动应用或测试. 原创代码 GPL-3.0-only, 原创说明 CC BY-SA 4.0, 第三方保留原条款.
