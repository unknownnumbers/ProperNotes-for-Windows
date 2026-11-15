# Build the ProperNotes Windows host with GitHub Actions

English (en): This recipe compiles the Windows host on a GitHub-hosted Windows runner in a public or private repository. The Windows x64 host build succeeded. The application has not been launched, tested or accepted on devices.

Traditional Chinese (zh-TC): 此流程在公開或私有倉庫的 GitHub 託管 Windows runner 編譯視窗宿主. Windows x64 宿主構建已成功. 應用尚未啟動, 測試或完成裝置驗收.

Simplified Chinese (zh-SC): 此流程在公开或私有仓库的 GitHub 托管 Windows runner 编译窗口宿主. Windows x64 宿主构建已成功. 应用尚未启动, 测试或完成设备验收.

## Scope and prerequisites

The artifact contains the self-contained `win-x64` host and dependency notices. The complete portable application adds the local web application, Node runtime and PDF resources. This preview uses an existing Evergreen WebView2 Runtime, bundles no Fixed Runtime, and does not automatically install dependencies.

The repository needs suitable permissions, Actions policy, and runner allowance. Both public and private repositories are supported. The recorded successful host run is [43749561762](https://github.com/unknownnumbers/ProperNotes-for-Windows/actions/runs/43749561762), using host-source commit `bf8992f80b9874f76e849d9e9da9af20f1282a64`, .NET SDK `10.0.403`, and .NET/WindowsDesktop runtime packages `10.0.14`. This host-source commit is separate from the complete application's source revision recorded during portable assembly.

The recipe uses `windows-2025` and requires the exact .NET SDK `10.0.403` pinned in `windows-host/global.json`, with roll-forward disabled and prereleases excluded. It stops if that version is not preinstalled; it does not install an SDK. Project packages are restored from the official NuGet source into a temporary runner directory. The WebView2 SDK is pinned to `1.0.4314.35`.

## Repository layout

Copy the templates and host sources into this layout:

```text
.github/workflows/windows-portable.yml   # from cloud-build/windows-portable.yml
.gitattributes                          # from cloud-build/source-bytes.gitattributes
cloud-build/Build-HostArtifact.ps1
windows-host/ProperNotes.Host.csproj
windows-host/global.json
windows-host/app.manifest
windows-host/*.cs
LICENSE
LICENSE-WEBVIEW2-EXCEPTION
LICENSE-DOCS
release-docs/COPYRIGHT.md
release-docs/THIRD_PARTY_NOTICES.md
release-docs/third-party-inventory.json
licenses/CC-BY-SA-4.0.txt
```

Preserve host subdirectory and `.json` input paths. `bin`, `obj`, and `.git` are excluded from the source inventory. Runtime data, PDFs, backups, profiles, and logs are not build inputs.

Source fingerprints cover original bytes. Include `.gitattributes` with `/windows-host/** -text` to prevent checkout line-ending conversion. The workflow also disables `core.autocrlf` on its runner.

## Workflow behavior

The workflow supports manual dispatch and pushes to `propernotes-cloud-build` that change the workflow, `.gitattributes`, host sources, build script, or listed license inputs. It grants `contents: read`, checks out the host/build directories and those notice files, and does not persist checkout credentials.

The action revisions are pinned:

- [actions/checkout](https://github.com/actions/checkout/commit/11d5960a326750d5838078e36cf38b85af677262)
- [actions/upload-artifact](https://github.com/actions/upload-artifact/commit/ea165f8d65b6e75b540449e92b4886f43607fa02)

`Build-HostArtifact.ps1` has no parameters and accepts only GitHub-hosted Windows runners. It reads the source-controlled SDK pin, requires that exact preinstalled version, and copies the same `global.json` to the temporary build root. It copies inputs, restores packages, and publishes self-contained Release `win-x64` output without single-file publishing or trimming.

Packaging checks cover required files, x64 PE headers, a matching Loader, unchanged sources, and dependency notices. Required project notices are checked before compilation and copied byte for byte into the artifact. No application, server, or tests are launched. Artifacts are retained for 7 days and follow repository access permissions. The workflow does not create a Release or alter repository settings.

## Artifact contract

```text
host-artifact.json
host/                        # complete self-contained publish output
licenses/host-runtime/       # retained WebView2 SDK and .NET runtime notices
licenses/project/            # GPL, documentation license, and copyright scope
```

Keep the entire `host` directory. The artifact does not include the WebView2 engine. This preview expects an existing Evergreen Runtime and contains no Fixed Runtime.

`licenses/project` retains `LICENSE`, `LICENSE-WEBVIEW2-EXCEPTION`, `LICENSE-DOCS`, `COPYRIGHT.md`, the third-party inventory documents, and `licenses/CC-BY-SA-4.0.txt`. The nested path preserves links from the project notices. The inventory describes the full project, not additional components shipped in this host artifact. Actual host dependency notices remain under `licenses/host-runtime`.

Original code uses [GPL-3.0-only](../LICENSE), with the narrow [WebView2 additional permission](../LICENSE-WEBVIEW2-EXCEPTION) for Windows host contributions. Original explanatory documentation uses [CC BY-SA 4.0](../LICENSE-DOCS). The additional permission does not change Microsoft component terms.

`host-artifact.json` records:

| Field | Content |
| --- | --- |
| `schemaVersion` | `1` |
| `app` | `propernotes-windows-host` |
| `rid` | `win-x64` |
| `webView2SdkVersion` | `1.0.4314.35` |
| `sourceHash` | Aggregate SHA-256 of host build inputs |
| `sources` | Host source relative paths, SHA-256 values, and sizes |
| `files` | Artifact file relative paths, SHA-256 values, and sizes; excludes the manifest itself |
| `builtAtUtc`, `sdkVersion`, `sourceCommit` | Build timestamp, selected SDK, and GitHub commit |

Source inputs are `.cs`, `.csproj`, `.manifest`, and `.json` files under `windows-host`. Paths are sorted using Ordinal ordering. The aggregate hash feeds each UTF-8 relative path, a NUL byte, and the file's original bytes into SHA-256. This algorithm matches the portable assembly and launcher checks. The artifact records source metadata but does not contain a source archive.

Before assembly, match the source hash, source inventory, complete artifact file list, and target architecture. Hashes detect mismatches; they do not establish publisher identity or replace a signature. A public release also requires its corresponding source and the applicable project and third-party notices.

## Known limits

The recorded run confirms host compilation and packaging only. No application, server or functional tests were run; device acceptance, runtime behavior, pen input, save/close integration and performance remain unverified. Consult the portable package's `provenance.json`, `validation-range.json` and file/license records for its actual assembly inputs and verification scope. Self-contained .NET output still requires an existing WebView2 Runtime and the applicable redistribution notices.
