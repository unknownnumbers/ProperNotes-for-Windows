# Portable layout and build

## English (en)

This is a Windows x64 preview build. The native host compiled successfully in GitHub Actions; the application has not been launched, and functional tests, device acceptance and performance validation have not been performed. The package uses a self-contained .NET 10 host and an existing Evergreen WebView2 Runtime. Self-contained .NET does not include the browser engine; this preview includes no Fixed Runtime and does not automatically install it.

A completed package starts with `Start-ProperNotes.cmd`. Keep its entire directory together in a writable local location. The launcher checks its host/source manifest, uses an owned loopback service and opens the native window. Missing dependencies stop startup; the launcher does not install them. WebView2 profile data uses `UserData/WebView2`, diagnostics use `Logs`, and default PDF backups use `Backups` under the program root. Backups default to 5 minutes and 10 copies per PDF, with an adjustable location, interval and limit. A limit of 0 stops new backups and retains existing ones.

The Git repository does not contain `Node.exe`. For a source build, manually obtain and extract the official Node.js `24.22.1` Windows x64 ZIP listed below, and verify the ZIP, executable and original `LICENSE` fingerprints. Supply these existing files to `Export-ProperNotesSource.ps1` to create a fresh isolated build input, then use the portable builder there. The repository already contains the 201 reviewed `vendor` files needed for offline UI preparation. Ordinary application startup does not download or install Node.js or other dependencies. The shared input table and commands below apply to all three language sections.

Maintainers with the exact .NET SDK `10.0.403` pinned in `windows-host/global.json` and matching local NuGet packages can use `Build-ProperNotesPortable.ps1`. It uses offline sources and stops if prerequisites are missing. Alternatively, `-HostArtifactPath <extracted-artifact-directory>` assembles a package from a host artifact built from the exact same native sources; this mode does not need a local SDK. Each build creates a new output directory. Source assembly, compilation, tests and device acceptance must be reported separately. See [RELEASE-METADATA.md](RELEASE-METADATA.md) for explicit source revision inputs, file/license records, provenance, validation limits and SHA256SUMS.

The build input must retain the full source tree, `release-docs`, `licenses`, offline `vendor` resources and the matching Node.js runtime. `runtime/node.exe prepare.cjs --ui-only` assembles existing UI sources without refreshing dependencies. Dependency refresh is a separate explicit operation requiring an existing input directory and its original notices. A binary release also needs corresponding source, file/license manifests, provenance, SHA256SUMS and an honest validation report. Read the included `provenance.json`, `validation-range.json` and file/license records for the package's actual inputs and verification limits.

## Traditional Chinese (zh-TC)

本階段是 Windows x64 預覽構建. 原生宿主已在 GitHub Actions 編譯成功; 應用尚未啟動, 未執行功能測試, 裝置驗收或效能驗證. 便攜目錄包含自包含 .NET 10 宿主, 使用已有 Evergreen WebView2 Runtime. 自包含 .NET 不包含瀏覽器引擎; 本預覽不附 Fixed Runtime, 不自動安裝它.

完整包以 `Start-ProperNotes.cmd` 啟動, 整個目錄須放在可寫的本地位置. 啟動器核對宿主/原始碼清單, 建立自己持有的 loopback 服務並開啟原生視窗; 缺依賴即停止, 不自動安裝. WebView2 資料, 診斷和預設備份分別放在程式根目錄的 `UserData/WebView2`, `Logs`, `Backups`. 備份預設每 5 分鐘, 每份 PDF 保留 10 份, 位置, 間隔及數量可調; 數量為 0 時停止新增, 已有備份保留.

Git 倉庫不存放 `Node.exe`. 從原始碼構建時, 請手動取得並解壓下表的官方 Node.js `24.22.1` Windows x64 ZIP, 核對 ZIP, 執行檔及原始 `LICENSE` 的指紋. 將這些已有檔案交給 `Export-ProperNotesSource.ps1`, 產生全新的隔離構建輸入, 再在該目錄執行便攜構建器. 倉庫已包含離線組裝 UI 所需的 201 個已核對 `vendor` 檔案. 一般應用啟動不會下載或安裝 Node.js 或其他依賴. 下方技術表與命令供三種語言共同參考.

維護者可用 `windows-host/global.json` 固定的 .NET SDK `10.0.403` 和匹配的本地 NuGet 套件執行 `Build-ProperNotesPortable.ps1`. 構建只用離線來源, 缺精確 SDK 版本或其他前置條件即停止. 也可用 `-HostArtifactPath <已解壓構件目錄>` 導入與目前原生原始碼完全一致的宿主構件, 此模式不需本機 SDK. 每次產生新目錄, 原始碼組裝, 編譯, 測試及裝置驗收須分別記錄.

構建輸入須保留完整原始碼, `release-docs`, `licenses`, 離線 `vendor` 及匹配的 Node.js. `runtime/node.exe prepare.cjs --ui-only` 只組裝已有 UI, 不更新依賴. 更新依賴需另行明確提供已有輸入目錄與原通知. 二進位發布另須附對應原始碼, 逐檔/授權清單, 來源, SHA256SUMS 和實際驗證範圍. 實際輸入和核對邊界以包內 `provenance.json`, `validation-range.json` 及檔案/授權記錄為準.

## Simplified Chinese (zh-SC)

本阶段是 Windows x64 预览构建. 原生宿主已在 GitHub Actions 编译成功; 应用尚未启动, 未执行功能测试, 设备验收或性能验证. 便携目录包含自包含 .NET 10 宿主, 使用已有 Evergreen WebView2 Runtime. 自包含 .NET 不包含浏览器引擎; 本预览不附 Fixed Runtime, 不自动安装它.

完整包以 `Start-ProperNotes.cmd` 启动, 整个目录须放在可写的本地位置. 启动器核对宿主/源码清单, 创建自己持有的 loopback 服务并打开原生窗口; 缺依赖即停止, 不自动安装. WebView2 数据, 诊断和默认备份分别放在程序根目录的 `UserData/WebView2`, `Logs`, `Backups`. 备份默认每 5 分钟, 每份 PDF 保留 10 份, 位置, 间隔及数量可调; 数量为 0 时停止新增, 已有备份保留.

Git 仓库不存放 `Node.exe`. 从源码构建时, 请手动取得并解压下表的官方 Node.js `24.22.1` Windows x64 ZIP, 核对 ZIP, 可执行文件及原始 `LICENSE` 的指纹. 将这些已有文件交给 `Export-ProperNotesSource.ps1`, 产生全新的隔离构建输入, 再在该目录执行便携构建器. 仓库已包含离线组装 UI 所需的 201 个已核对 `vendor` 文件. 普通应用启动不会下载或安装 Node.js 或其他依赖. 下方技术表与命令供三种语言共同参考.

维护者可用 `windows-host/global.json` 固定的 .NET SDK `10.0.403` 和匹配的本地 NuGet 包执行 `Build-ProperNotesPortable.ps1`. 构建只用离线来源, 缺精确 SDK 版本或其他前置条件即停止. 也可用 `-HostArtifactPath <已解压构件目录>` 导入与当前原生源码完全一致的宿主构件, 此模式不需本机 SDK. 每次产生新目录, 源码组装, 编译, 测试及设备验收须分别记录.

构建输入须保留完整源码, `release-docs`, `licenses`, 离线 `vendor` 及匹配的 Node.js. `runtime/node.exe prepare.cjs --ui-only` 只组装已有 UI, 不更新依赖. 更新依赖需另行明确提供已有输入目录与原通知. 二进制发布另须附对应源码, 逐文件/许可清单, 来源, SHA256SUMS 和实际验证范围. 实际输入和核对边界以包内 `provenance.json`, `validation-range.json` 及文件/许可记录为准.

## Build inputs and assembly

| Input | Recorded version or source | SHA256 or requirement |
| --- | --- | --- |
| Node.js Windows x64 ZIP | [Official Node.js 24.22.1 ZIP](https://nodejs.org/dist/v24.22.1/node-v24.22.1-win-x64.zip) | `d2046d28c2c3dcd211ac5145c0bf30e2047ef9dfb306693c8c6e2eccd8b95fa5` |
| Extracted `node.exe` | From that ZIP | `d702daa815eabbff763c7d3808e235cf031cf2ca70211b56847bc72f51e240e5` |
| Extracted `LICENSE` | Original complete notice from that ZIP | `ed34dd8e3f0a78dbaf00d0444ce8e285b015b765379c2e17880455f70370f8e9` |
| .NET SDK | `10.0.403` | Exact source pin in `windows-host/global.json`; roll-forward disabled, no prerelease |
| Microsoft.Web.WebView2 SDK | `1.0.4314.35` | Fixed project package version; actual package notices retained |
| .NET and WindowsDesktop runtime packages | `10.0.14` | Recorded from the successful host build; inspect the actual artifact records |
| Offline UI resources | 201 reviewed `vendor` files in the repository | Checked against the third-party inventory during export |

After manually downloading, checking and extracting the Node ZIP, use existing paths in this order. The export destination must not already exist. The host artifact must match the exported native sources, including `global.json`; its CI commit is separate from the full-project source commit.

```powershell
.\Export-ProperNotesSource.ps1 -OutputDirectory '<fresh-build-input>' -NodePath '<extracted-node-directory>\node.exe' -NodeLicensePath '<extracted-node-directory>\LICENSE'
Set-Location '<fresh-build-input>'
.\Build-ProperNotesPortable.ps1 -HostArtifactPath '<extracted-host-artifact-directory>' -SourceRepositoryUrl 'https://github.com/unknownnumbers/ProperNotes-for-Windows' -SourceCommit '<full-project-source-commit>'
```

The exporter supplies the verified Node executable as `runtime/node.exe` in the isolated input and retains its matching original notice. This is explicit maintainer input preparation, not a startup download or installation. Artifact-import mode does not require a local .NET SDK; compiling a new host locally requires the pinned SDK and existing offline packages.

## License scope

Original code uses [GPL-3.0-only](LICENSE). The Windows host contributions also carry the narrow [WebView2 additional permission](LICENSE-WEBVIEW2-EXCEPTION). Original explanatory documentation uses [CC BY-SA 4.0](LICENSE-DOCS). Third-party files retain their own terms. See [COPYRIGHT.md](COPYRIGHT.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
