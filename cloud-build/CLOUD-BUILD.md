# Build the ProperNotes Windows host with GitHub Actions

English (en): This recipe compiles the Windows host on a GitHub-hosted Windows runner in a public or private repository. No successful build or runtime acceptance has been recorded.

Traditional Chinese (zh-TC): 此流程在公開或私有倉庫的 GitHub 託管 Windows runner 編譯視窗宿主. 尚未取得成功編譯或實機驗收結果.

Simplified Chinese (zh-SC): 此流程在公开或私有仓库的 GitHub 托管 Windows runner 编译窗口宿主. 尚未取得成功编译或实机验收结果.

## Scope and prerequisites

The artifact contains the self-contained `win-x64` host and dependency notices. It is not the complete ProperNotes portable application. The local web application, Node runtime, PDF resources, and optional WebView2 engine are assembled separately.

The repository needs suitable permissions, Actions policy, and runner allowance. Both public and private repositories are supported. Repository access and workflow execution are separate prerequisites; this guide does not establish that a run has occurred.

The recipe uses `windows-2025` and selects a preinstalled stable .NET 10 SDK. It stops if none is available; it does not install an SDK. Project packages are restored from the official NuGet source into a temporary runner directory. The WebView2 SDK is pinned to `1.0.4314.35`.

## Repository layout

Copy the templates and host sources into this layout:

```text
.github/workflows/windows-portable.yml   # from cloud-build/windows-portable.yml
.gitattributes                          # from cloud-build/source-bytes.gitattributes
cloud-build/Build-HostArtifact.ps1
windows-host/ProperNotes.Host.csproj
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

`Build-HostArtifact.ps1` has no parameters and accepts only GitHub-hosted Windows runners. It pins the selected .NET 10 SDK, copies inputs to a temporary directory, restores packages, and publishes self-contained Release `win-x64` output without single-file publishing or trimming.

Packaging checks cover required files, x64 PE headers, a matching Loader, unchanged sources, and dependency notices. Required project notices are checked before compilation and copied byte for byte into the artifact. No application, server, or tests are launched. Artifacts are retained for 7 days and follow repository access permissions. The workflow does not create a Release or alter repository settings.

## Artifact contract

```text
host-artifact.json
host/                        # complete self-contained publish output
licenses/host-runtime/       # retained WebView2 SDK and .NET runtime notices
licenses/project/            # GPL, documentation license, and copyright scope
```

Keep the entire `host` directory. The artifact does not include the WebView2 engine. The complete application uses an existing Evergreen Runtime or a separately supplied, complete Fixed Version Runtime at `runtime\WebView2`.

`licenses/project` retains `LICENSE`, `LICENSE-WEBVIEW2-EXCEPTION`, `LICENSE-DOCS`, `COPYRIGHT.md`, the third-party inventory documents, and `licenses/CC-BY-SA-4.0.txt`. The nested path preserves links from the project notices. The inventory describes the full project, not additional components shipped in this host artifact. Actual host dependency notices remain under `licenses/host-runtime`.

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

No successful build is recorded. Compilation, packaging, repository access, and runner availability remain unconfirmed. Runtime behavior, pen input, save/close integration, and performance require separate acceptance. Self-contained .NET output still requires WebView2 Runtime and applicable redistribution/license review.
