# ProperNotes for Windows

Developed through human and AI collaboration. Some AI-assisted code and text may not have received complete human review.

本專案由人類與 AI 協作開發. 部分 AI 協助產生的程式與文字可能尚未經完整人工審查.

本项目由人类与 AI 协作开发. 部分 AI 协助产生的代码与文字可能尚未经过完整人工审查.

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
