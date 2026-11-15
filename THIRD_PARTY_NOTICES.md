# Third-party notices and verified distribution inputs

## Overview - en / zh-TC / zh-SC

English (en): This inventory records third-party distribution inputs and retained notices. Original ProperNotes code is offered under GPL-3.0-only and original explanatory documentation under CC BY-SA 4.0, within the scope of COPYRIGHT.md. Third-party terms remain separate. Vendor, selected official Node and all native host files have recorded origins. Final portable-package and corresponding-source reconciliation remains pending.

Traditional Chinese (zh-TC): 本清單記錄第三方發布輸入與保留的聲明. ProperNotes 原創程式採 GPL-3.0-only, 原創說明文字採 CC BY-SA 4.0, 範圍見 COPYRIGHT.md. 第三方條款維持獨立. vendor 輸入, 選定的官方 Node 及全部原生宿主檔案均已記錄來源. 最終便攜包及對應原始碼核對仍待完成.

Simplified Chinese (zh-SC): 本清单记录第三方发布输入与保留的声明. ProperNotes 原创代码采用 GPL-3.0-only, 原创说明文字采用 CC BY-SA 4.0, 范围见 COPYRIGHT.md. 第三方条款保持独立. vendor 输入, 选定的官方 Node 及全部原生宿主文件均已记录来源. 最终便携包及对应源码核对仍待完成.

Recorded: 2026-11-16. Paths identify intended release paths. [third-party-inventory.json](third-party-inventory.json) records archive URLs, registry integrity values, SHA256 fingerprints, release-input mappings, source comparisons and file-specific license rules. For entries with a different workspace input, `sourcePath` records that input. No dependency or application was installed or executed for this verification.

## Verified inputs

| Component | Version | Verification and retained terms |
| --- | --- | --- |
| PDF.js | 5.6.205 | All 199 existing vendor files, including all 10 resource notice files, match the official npm archive. Core Apache-2.0; asset-specific licenses are listed below. |
| pdf-lib | 1.17.1 | Bundle and main MIT notice match the official npm archive. All 53 mapped dependency source entries match the official dependency archives identified by its published lockfile and source-map paths. |
| Lucide | 1.8.0 | Bundle and complete original notice match the official npm archive. Retain ISC and MIT terms for the listed Feather-derived icons. |
| Node.js | 24.22.1, Windows x64 | The selected official archive matches Node's published HTTPS SHA256 list. Its extracted executable has a Valid Windows Authenticode signature from OpenJS Foundation. Retain its complete official LICENSE, including all bundled third-party terms. |
| .NETCore App Runtime | 10.0.14, Windows x64 | All 185 redistributed files match the verified official NuGet archive. Retain MIT and the complete package third-party notices. |
| WindowsDesktop App Runtime | 10.0.14, Windows x64 | All 81 redistributed files match the verified official NuGet archive and original MIT license. |
| WebView2 SDK | 1.0.4314.35 | All 6 redistributed SDK files match the verified official NuGet archive. Retain original Microsoft and SDK third-party BSD notices. |

The three main npm archives and five embedded dependency archives were downloaded from `registry.npmjs.org`; each archive's SHA512 matches its exact version's HTTPS registry integrity metadata. Node's archive is from `nodejs.org`. These checks establish matches to the recorded published inputs. They do not reproduce the original builds or give a blanket legal or supply-chain assurance. The Node checksum list's PGP signature was not verified.

The pinned WebView2 SDK, .NETCore runtime, WindowsDesktop runtime and AppHost NuGet archives were also checked against SHA512 metadata from the official NuGet HTTPS catalog. Their unchanged original legal files are retained. NuGet repository package signatures were not independently verified. Actual cloud-published third-party files match the recorded official inputs.

Official version records: [PDF.js](https://registry.npmjs.org/pdfjs-dist/5.6.205), [pdf-lib](https://registry.npmjs.org/pdf-lib/1.17.1), [Lucide](https://registry.npmjs.org/lucide/1.8.0), [Node SHA256 list](https://nodejs.org/dist/v24.22.1/SHASUMS256.txt).

## Selected Node release input

For the intended release, `release-inputs/node-v24.22.1-win-x64/node.exe` maps to `runtime/node.exe`, and its accompanying `LICENSE` maps to `licenses/node.txt`. The exact hashes are recorded below and in `releaseInputMappings` in the JSON inventory. The earlier workspace runtime and its earlier notice are preserved as source material, but are not the selected distribution input.

- `runtime/node.exe`: SHA256 `d702daa815eabbff763c7d3808e235cf031cf2ca70211b56847bc72f51e240e5`, 93915464 bytes.
- `licenses/node.txt`: SHA256 `ed34dd8e3f0a78dbaf00d0444ce8e285b015b765379c2e17880455f70370f8e9`, 160555 bytes.
- Official archive: SHA256 `d2046d28c2c3dcd211ac5145c0bf30e2047ef9dfb306693c8c6e2eccd8b95fa5`, [node-v24.22.1-win-x64.zip](https://nodejs.org/dist/v24.22.1/node-v24.22.1-win-x64.zip).

The Node notice is a combined distribution notice. Its first MIT section does not replace the terms of every bundled third-party component.

## Retained notice files

Keep all files below and relevant license comments in vendor source files. The summaries identify the notices and do not replace their full text. Hashes for the Node notice describe the selected release input, not the earlier workspace copy.

| Release path | Observed terms | SHA256 |
| --- | --- | --- |
| `licenses/pdfjs-dist.txt` | Apache-2.0 | `0d542e0c8804e39aa7f37eb00da5a762149dc682d7829451287e11b938e94594` |
| `licenses/pdf-lib.txt` | MIT | `f2c9fc00fdb66eb99ac156ba52d734af66d8d309f65753ae809ad34ee2883bcb` |
| `licenses/lucide.txt` | ISC plus MIT for the listed Feather-derived icons | `b495047bd93a9b06913511076f504daba17d5bbeb3e0650f3bb53a4220329c57` |
| `licenses/node.txt` | Node MIT plus bundled third-party notices; preserve the full file | `ed34dd8e3f0a78dbaf00d0444ce8e285b015b765379c2e17880455f70370f8e9` |
| `vendor/pdfjs/cmaps/LICENSE` | Adobe CMaps BSD-style terms | `aa92ab5a472974865a96fd4a4e9c13bb41bf6fe1b309cb6b8da48bc9e19839a2` |
| `vendor/pdfjs/iccs/LICENSE` | CC0-1.0 | `286e4fd7b447330b2c88e23890e3cd0a9d38cb398d4a59cb247f578ccbda3213` |
| `vendor/pdfjs/standard_fonts/LICENSE_FOXIT` | PDFium BSD-style terms | `b578cdd2345840ada550bd12519533812320d5f1d21cf4c1c7e1b1b0a31c98b7` |
| `vendor/pdfjs/standard_fonts/LICENSE_LIBERATION` | SIL OFL-1.1 with reserved font names | `93fed46019c38bbe566b479d22148e2e8a1e85ada614accb0211c37b2c61c19b` |
| `vendor/pdfjs/wasm/LICENSE_JBIG2` | PDFium BSD-style notice followed by Apache-2.0 text | `9e66b7f1b934a28b37f3bc4dac97915de1674271e79a0a88182a18ed9731b4d1` |
| `vendor/pdfjs/wasm/LICENSE_OPENJPEG` | BSD-2-Clause with additional contributor and patent-rights text | `a6af136f3e15038a666b61f376612a07d9a4e48cb7c01adbf3e33b3f14ab49b6` |
| `vendor/pdfjs/wasm/LICENSE_PDFJS_JBIG2` | Apache-2.0 notice | `aad3cce09842e00e9e11ad5e8fef8cc02fbc3a3768fe2f007443b9cee37aaee5` |
| `vendor/pdfjs/wasm/LICENSE_PDFJS_OPENJPEG` | BSD-2-Clause | `717fc62da03292dbb4dd0c8280bd4ce7bb8550dcf31d772bc93455fb50313425` |
| `vendor/pdfjs/wasm/LICENSE_PDFJS_QCMS` | BSD-2-Clause | `eb5104ca33552be007857a28351bc408f379ddd4bfacab1226b09c6d1e9fd7c4` |
| `vendor/pdfjs/wasm/LICENSE_QCMS` | MIT-style permission and disclaimer | `36d847ae882f6574ebc72f56a4f354e4f104fde4a584373496482e97d52d31bc` |
| `licenses/pdf-lib-dependencies/standard-fonts-LICENSE.md` | MIT | `45cc2bb9957ed43f5c7f5eb1912f9f670730394d614079510477cae626194649` |
| `licenses/pdf-lib-dependencies/upng-LICENSE` | MIT | `c1fb8861eca211bf5aea574358aba7039396c0c1824c2a1477cc126d398efd89` |
| `licenses/pdf-lib-dependencies/pako-LICENSE` | MIT portion of pako MIT AND Zlib license declaration | `a04665b3b2de56c66730c1f720f528175739e4104f79073614aa611da1e85539` |
| `licenses/pdf-lib-dependencies/pako-zlib-NOTICE.txt` | Zlib source notice | `170d7d544d380b38d5f76ab5eda29475b44ef537f4564163d6d9bece85c51c13` |
| `licenses/pdf-lib-dependencies/Microsoft-helper-NOTICE.txt` | Microsoft Apache-2.0 helper notice | `52e32a35732611c0e5e84370861c47fa61630d18654e9a26739af2760bf5575c` |
| `licenses/pdf-lib-dependencies/Apache-2.0.txt` | Complete Apache-2.0 terms and conditions | `0d542e0c8804e39aa7f37eb00da5a762149dc682d7829451287e11b938e94594` |
| `licenses/pdf-lib-dependencies/tslib-LICENSE.txt` | Apache-2.0 | `a7d00bfd54525bc694b6e32f64c7ebcf5e6b7ae3657be5cc12767bce74654a47` |
| `licenses/pdf-lib-dependencies/tslib-CopyrightNotice.txt` | Original Microsoft Apache-2.0 copyright notice | `19246466cb51e46c12b22e43b4d2703ac981cb4295d061d6e20609bd5845510b` |
| `licenses/host-runtime/microsoft.web.webview2/1.0.4314.35/LICENSE.txt` | Microsoft BSD-3-Clause-style terms | `0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55` |
| `licenses/host-runtime/microsoft.web.webview2/1.0.4314.35/NOTICE.txt` | Official third-party notices, including ANTLR3 and StringTemplate4 BSD-3-Clause | `106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90` |
| `licenses/host-runtime/microsoft.netcore.app.runtime.win-x64/10.0.14/LICENSE.TXT` | MIT, .NET Foundation and Contributors | `d7a68596ab69b06f51ca278a6545148e4269a9381c26d597c13df5d88e08cf5b` |
| `licenses/host-runtime/microsoft.netcore.app.runtime.win-x64/10.0.14/THIRD-PARTY-NOTICES.TXT` | Original .NET bundled third-party notices | `6d15e10a101c6bfff2ab4429ed061bf76c456fc4b23ad6b03e0d0f8377148a21` |
| `licenses/host-runtime/microsoft.windowsdesktop.app.runtime.win-x64/10.0.14/LICENSE` | MIT, .NET Foundation and Contributors | `a89886665765362eb77e0f8e26602c924520041d1711b2eedc136434fe4d01ab` |
| `licenses/host-runtime/microsoft.netcore.app.host.win-x64/10.0.14/LICENSE.TXT` | MIT, .NET Foundation and Contributors | `d7a68596ab69b06f51ca278a6545148e4269a9381c26d597c13df5d88e08cf5b` |
| `licenses/host-runtime/microsoft.netcore.app.host.win-x64/10.0.14/THIRD-PARTY-NOTICES.TXT` | Original .NET bundled third-party notices | `6d15e10a101c6bfff2ab4429ed061bf76c456fc4b23ad6b03e0d0f8377148a21` |

The Liberation notice names Arimo, Tinos, Cousine, and Liberation as reserved font names. Its OFL terms apply to the font software, not to the application's own code or documents created using the fonts.

## pdf-lib embedded dependencies

The verified official pdf-lib archive contains the [published v1.17.1 package metadata](https://raw.githubusercontent.com/Hopding/pdf-lib/v1.17.1/package.json), lockfile and source map. Their dependency paths and versions were compared with official dependency archives:

| Dependency | Published input versions | Source and notice evidence |
| --- | --- | --- |
| `@pdf-lib/standard-fonts` | 1.0.0 | All 3 mapped source entries and the copied original MIT license match the verified official archive. |
| `@pdf-lib/upng` | 1.0.1 | Its mapped source entry and the copied original MIT license match the verified official archive. |
| `pako` | 1.0.10 for nested inputs; 1.0.11 for the direct input | All 48 mapped source entries match the selected official archives. Retain the original MIT file and the verbatim zlib source notice. |
| `tslib` | 1.11.1 | The mapped helper source matches the verified official archive exactly. Its original Apache-2.0 LICENSE.txt and CopyrightNotice.txt are retained, together with the helper notice already present in the bundle. |

Identical fragments can exist in more than one package version. The version context above comes from the published lockfile and source-map paths, together with the exact source matches. The source map was not reconstructed by running the build. A newer installed `tslib` input was not substituted for the bundled helper's Apache-2.0 notices.

The three original MIT notice files were initially copied unchanged from existing build inputs and then matched to the verified official archives. The zlib and Microsoft helper notices were extracted verbatim from the source map and matched to the verified official source entries. `Apache-2.0.txt` remains an unchanged copy of the existing official PDF.js Apache terms; the additional `tslib-LICENSE.txt` and `tslib-CopyrightNotice.txt` are unchanged files from the official tslib 1.11.1 archive.

## Native Windows host

The verified [cloud build](https://github.com/unknownnumbers/ProperNotes-for-Windows/actions/runs/43751071075) uses SDK 10.0.403 and self-contained Windows x64 runtime 10.0.14 at source commit `f1961fcc6334ec9d227da24a7c703a08e584167c`. All 8 source entries in its host manifest match the recorded current source files. The artifact archive SHA256 is `6e50efb232556b0c2b65363c6acb35355e166f0da2ce99e6ca01cf513b61c235`.

There are 276 files under `host/`: 272 match verified official third-party package files exactly, and 4 are the project assembly, generated apphost and generated project JSON files. All 270 third-party PE files have Valid Microsoft Authenticode signatures. The ProperNotes executable and assembly are unsigned. XML and JSON files are not PE files. These were static checks; no application or test was run.

- .NET: The exact [Core runtime](https://www.nuget.org/packages/Microsoft.NETCore.App.Runtime.win-x64/10.0.14), [Desktop runtime](https://www.nuget.org/packages/Microsoft.WindowsDesktop.App.Runtime.win-x64/10.0.14) and [AppHost](https://www.nuget.org/packages/Microsoft.NETCore.App.Host.win-x64/10.0.14) package versions were verified. The actual redistributed files and notices are mapped in the JSON inventory. Retain complete Core/AppHost third-party notices alongside their MIT texts and the original Desktop MIT license. Do not infer one license for every host file from its folder name.
- WebView2 SDK: The [official NuGet package for 1.0.4314.35](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4314.35) and its catalog SHA512 were checked. The original LICENSE.txt contains Microsoft BSD-3-Clause-style terms; original NOTICE.txt includes the ANTLR3 and StringTemplate4 BSD notices. Both are retained unchanged under `licenses/host-runtime/microsoft.web.webview2/1.0.4314.35/`. All 6 cloud SDK files match the official package.
- WebView2 Runtime: This is a [separate platform component](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/platform-components). The selected package relies on an existing Evergreen Runtime and does not select a Fixed Version engine payload. No engine copyright or redistribution permission is inferred from the SDK license.

Original ProperNotes Windows host contributions also carry the scoped GPLv3 section 7 permission in [LICENSE-WEBVIEW2-EXCEPTION](LICENSE-WEBVIEW2-EXCEPTION). This additional permission does not modify GPLv3's legal text, extend to unrelated ProperNotes contributions, or change Microsoft and other third-party terms. The JSON inventory distinguishes this original-host scope from SDK and runtime components.

The generated `ProperNotes.Host.exe` includes the .NET AppHost template. Its `.text`, `.rdata`, `.pdata` and `.reloc` sections match the official template exactly. Static comparison identifies a 64-byte managed-entry placeholder changed to `ProperNotes.Host.dll`, project resources added and the PE subsystem changed to Windows GUI. Both the template's original notices and the original-host scope apply. This is source-origin evidence, not a rerun of the build.

## File-specific scope and remaining work

The JSON inventory's `fileRules` use case-sensitive canonical `/` relative paths. Exact path rules take precedence; otherwise the longest matching prefix applies. Explicit first-party source provenance has its own scope. Unclassified vendor, runtime or host files are `NOASSERTION`, never implicitly GPL.

License documents have their own scope. The retained GNU GPLv3 text permits verbatim redistribution and prohibits changes to that legal text. Creative Commons makes its legal code and accompanying notices available under CC0, as stated in the retained text and the [official CC licensing policy](https://creativecommons.org/policies/). Thus `licenses/CC-BY-SA-4.0.txt` is classified as CC0 legal text, while original ProperNotes explanatory documentation uses CC BY-SA 4.0. Creative Commons trademark rights remain separate.

Files under `licenses/project/` are build-time notice snapshots carried by the cloud artifact. The root inventory and notices describe the later verified release inputs; the snapshot is not a substitute for this final inventory.

Before publication, reconcile the final portable package, selected Node input, current notices and complete corresponding-source export against these recorded origins. Input verification is complete for the recorded host and vendor files; package reconciliation remains pending. The project's established scopes are documented separately in [COPYRIGHT.md](COPYRIGHT.md), [LICENSE](LICENSE), [LICENSE-DOCS](LICENSE-DOCS) and the scoped additional permission; they do not relicense third-party material.
