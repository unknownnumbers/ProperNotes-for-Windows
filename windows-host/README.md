# ProperNotes for Windows host

English (en): A .NET 10 WinForms window embeds the local ProperNotes interface with WebView2. The Windows x64 host compiled successfully in GitHub Actions. The application has not been launched, tested or accepted on devices.

Traditional Chinese (zh-TC): .NET 10 WinForms 視窗透過 WebView2 顯示本機 ProperNotes 介面. Windows x64 宿主已在 GitHub Actions 編譯成功. 應用尚未啟動, 測試或完成裝置驗收.

Simplified Chinese (zh-SC): .NET 10 WinForms 窗口通过 WebView2 显示本机 ProperNotes 界面. Windows x64 宿主已在 GitHub Actions 编译成功. 应用尚未启动, 测试或完成设备验收.

## Architecture and package layout

`ProperNotes.Host.csproj` targets `net10.0-windows` and pins `Microsoft.Web.WebView2` to `1.0.4314.35`. The publish target is a self-contained `win-x64` directory with single-file publishing and trimming disabled. Keep the entire output in `<package-root>\host`; the EXE alone is insufficient. Self-contained .NET publishing does not include the WebView2 Runtime.

`global.json` pins .NET SDK `10.0.403`, with roll-forward disabled and prereleases excluded. Builds require that exact SDK to be present; build scripts do not install it.

The package launcher starts the loopback service. Before navigating, the host verifies its package identity and source revision through `__inkpdf_health` and `build-info.json`. Instances are separated by Windows user and package root; repeated launches focus the existing window through a current-user-only named pipe. Document tabs and service lifetime are managed outside the host.

The preview uses an existing Evergreen Runtime, bundles no Fixed Runtime, and does not automatically install dependencies. The host's runtime selection supports:

1. Use the complete Fixed Version Runtime at `<package-root>\runtime\WebView2` when that directory exists. An incomplete directory causes an error.
2. Otherwise use an existing Evergreen WebView2 Runtime.

The host does not download or install a runtime, use Edge Stable as its engine, alter ACLs, or request elevation. Fixed Runtime redistribution terms and platform requirements must be checked separately. See Microsoft's [runtime distribution documentation](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution).

## Command line

The normal entry point for a complete package is `Start-ProperNotes.cmd`. The host accepts these modes:

```text
host\ProperNotes.Host.exe --url http://127.0.0.1:<port>/ --instance <64hex> --revision <64hex> --root <absolute-package-root> --user-data <absolute-package-root>\UserData\WebView2
host\ProperNotes.Host.exe --check-runtime --root <absolute-package-root>
host\ProperNotes.Host.exe --activate --root <absolute-package-root>
```

The URL must use HTTP, `127.0.0.1`, port 1024-65535, and `/` without a query or fragment. Identity and revision are 64 lowercase hexadecimal characters. The host must reside in the package's `host` directory. The root must contain `server.cjs` and `Start-ProperNotes.cmd`; UNC roots are rejected.

`--check-runtime` checks the existing SDK Loader and runtime without creating a WebView2 browser environment or starting the service. `--activate` only attempts to focus an existing window. These modes accept only `--root`.

Exit codes: `0` success, `2` no window to activate, `3` activation failed, `10` invalid configuration, `20` unavailable WebView2 Loader/runtime, and `21` initialization failed.

## Local state and navigation

WebView2 data is stored in `<package-root>\UserData\WebView2`. Diagnostics are written to `<package-root>\Logs\host.log`; appending stops when the existing file exceeds 4 MiB. The package directory must be writable. Move the whole package only while it is closed; running files and configuration are not designed for replacement in place.

WebView2 configuration changes affect only the host process environment. The host selects the production channel, clears inherited browser/debugging arguments, and checks the selected data folder after environment creation. Initialization can already create data; this check cannot guarantee isolation from every external policy override.

Extra windows, downloads, permission requests, remote resources, and navigation away from the local interface are blocked. Browser accelerators, zoom, pinch zoom, and swipe navigation are disabled; text editing shortcuts and context menus remain available. Fullscreen restores the prior window state on exit. PDF access uses the application's existing file bridge.

## Web bridge protocol 1

Messages use the WebView2 `chrome.webview` channel. The host validates the local page origin and accepts protocol version 1 only. The following table describes JSON fields; `nonce` is 64 lowercase hexadecimal characters and `requestId` is 32.

| Direction | Message fields |
| --- | --- |
| App to host | `type: propernotes-app-hello`, `protocol: 1` |
| Host to app | `type: propernotes-host-init`, `protocol: 1`, `nonce`, `instance`, `revision` |
| App to host | `type: propernotes-app-ready`, `protocol: 1`, `nonce`, `instance`, `revision` |
| Host to app | `type: propernotes-close-request`, `protocol: 1`, `nonce`, `requestId` |
| App to host | `type: propernotes-close-result`, `protocol: 1`, `nonce`, `requestId`, `allow`, optional `message`, and `expiresAt` when allowed |

Input stays disabled until readiness matches the host binding. Startup has a 30-second readiness timer. Closing before first readiness cancels startup without a document-close handshake.

After readiness, closing checks document tabs, saves, and pending input. Rejection or a 15-second timeout retains the window. Approval requires the matching request ID and a future Unix-millisecond expiry within 9 seconds; the application issues an 8-second approval and protects input while waiting.

After a main browser/renderer failure or close timeout, a repeated close offers force-close confirmation with No selected by default. Confirmation can lose unsaved input and closes only the host window. Helper-process failures are logged. An unresponsive renderer receives a new readiness handshake without reloading the editing model.

## Build and validation status

The [recorded cloud build](https://github.com/unknownnumbers/ProperNotes-for-Windows/actions/runs/43749561762) compiled host-source commit `bf8992f80b9874f76e849d9e9da9af20f1282a64` with .NET SDK `10.0.403` and .NET/WindowsDesktop runtimes `10.0.14`. This is the host build identity, not the complete application's source revision. See the [cloud build guide](../cloud-build/CLOUD-BUILD.md) and the portable package's provenance and validation records.

Compilation and packaging succeeded; the application and server have not been launched and no functional or device tests have been run. Runtime behavior and performance remain unverified.

Pending validation includes runtime presence and architecture, single-instance activation, startup cancellation, service identity errors, DPI and pen/touch input, text shortcuts, fullscreen restoration, multi-document close and save flows, process failures, and moved or read-only package directories. Runtime licensing and redistribution must also match the actual files distributed.

The host's original contributions use [GPL-3.0-only](../LICENSE) with the narrow [WebView2 additional permission](../LICENSE-WEBVIEW2-EXCEPTION). Original explanatory documentation uses [CC BY-SA 4.0](../LICENSE-DOCS). Microsoft and other third-party components keep their own terms.
