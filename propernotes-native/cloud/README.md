# Native Windows cloud build

English (en): This build compiles a Windows x64 Qt/PDF4QT preview and creates portable
and matching-source ZIPs. It does not launch the application or run application tests.
Dependency installation is confined to a GitHub-hosted Windows runner.

Traditional Chinese (zh-TC): 此流程在 GitHub 託管的 Windows 雲端機器編譯原生 x64
預覽版, 產出便攜程式與對應來源 ZIP. 不啟動應用程式, 不執行應用測試.

Simplified Chinese (zh-SC): 此流程在 GitHub 托管的 Windows 云端机器编译原生 x64
预览版, 产出便携程序与对应来源 ZIP. 不启动应用程序, 不执行应用测试.

## Locked inputs

`dependencies.lock.json` records PDF4QT 1.7.0.0 at commit
`3e50f636551907db3f018e99748fe7daa56ff497`, Qt 6.11.3 MSVC 2022 x64, and vcpkg
baseline `84297877a7c1a155661ea562462f1b72fab080d1`. Qt is installed separately;
the vcpkg manifest does not install another Qt. The PDF4QT general overlay ports
come from that same PDF4QT commit.

The manifest is derived from PDF4QT's manifest, retaining its Windows core dependencies
and omitting Tesseract and the Linux-only TBB dependency. OCR, models, upstream apps,
tests, WiX, installers, WebView2, and Node.js are outside this build.

PDF4QT uses its supported `PDF4QT_BUILD_ONLY_CORE_LIBRARY=ON` option. The application
links `Pdf4QtLibCore` and Qt Widgets, while owning its canvas and pressure-ink layer.
The target is `ProperNotesNative`, with output `ProperNotes.Native.exe`.

## Workflow

Place `native-windows.yml` at `.github/workflows/propernotes-native-windows.yml` in
the repository. It runs on source changes to `propernotes-native-build` and supports
manual dispatch. Workflow contents permission is read-only. The Qt installer action,
aqtinstall source, checkout action, and artifact action are pinned to commits.

The Qt action installs `qtbase`, `qtsvg`, and build-only `qttools` binaries; it also
retrieves matching `qtbase` and `qtsvg` source. The source archive includes those
modules, their third-party source, and license texts. No Qt tools are deployed with
the application.

`Build-Native.ps1` requires a fresh work/output directory and a hosted-runner environment.
It fetches exact upstream commits, bootstraps vcpkg on the runner, installs the core
dependency manifest, configures CMake using Visual Studio 2022 x64, and builds only
`ProperNotesNative` in Release configuration. Explicit switches turn off OCR, tests,
and upstream installation/deployment steps. It never invokes `ctest` or an app executable.

The entry point expects the root CMake project to accept `PDF4QT_SOURCE_DIR`, add the
upstream core subdirectory, and define `ProperNotesNative`. It passes the vcpkg toolchain,
installed directory, exact triplet, and Qt prefix explicitly. `VCPKG_MANIFEST_INSTALL=OFF`
prevents CMake from installing a second dependency graph during configuration.
`VCPKG_TOOLCHAIN` is set internally by the toolchain after initialization; the build
does not predefine it, because that variable is also its early-return guard.

The workflow caches dependency binary archives and their extracted/patched source
together. The cache key includes the dependency lock, manifest, source-cache implementation,
and Windows image family. vcpkg additionally checks its compiler/toolchain ABI when
choosing binary archives. A restored source cache must pass its file SHA-256 inventory
before binary-cache reads are enabled; otherwise dependencies are built from source.
After successful dependency installation, source is copied back into the shared cache
before application compilation. An `always()` save step can preserve those completed
dependencies even when a later application compile fails. Unchanged caches are not saved
again. Cache restore/save actions are pinned; cache hits do not omit the source ZIP inputs.

## Portable output and source

`Deploy-Portable.ps1` copies the application, PDF4QT core DLL, and release vcpkg DLLs.
It runs Qt's deployment tool, then copies the image's app-local release CRT DLLs.
It does not run a runtime installer. Windows 10/11 x64 system libraries remain OS
prerequisites; no external browser runtime is required.

Deployment uses the Qt 6.11.3 tool's documented/source-defined `--no-patchqt`,
`--no-opengl-sw`, and `--no-system-d3d-compiler` switches. `qt.conf` supplies relative
paths. This keeps official Qt DLL input bytes intact and avoids optional software
OpenGL/D3D compiler copies for this QWidget/QPainter application. Static PE-import
inspection still checks direct imports against deployed DLLs or the operating system.

The portable root contains `ProperNotes.cmd`, the EXE and DLLs, Qt plugins, licenses,
build provenance, a static PE-import inventory, and SHA-256 file hashes. The CMD is
provided for the recipient to launch manually. Packaging does not execute it.

Both archives include `file-license-index.json`. It inventories every outer file and,
for source input ZIPs, each nested ZIP entry with SHA-256, component, and license/source
evidence. Runtime DLLs are matched against actual build, Qt, installed-vcpkg, and CRT
input hashes; vcpkg ownership comes from the actual installed file lists. Qt is not
classified as MIT, and its separately licensed bundled components remain relevant.
`NOASSERTION` explicitly marks a license conclusion not established by the recorded
evidence. It is not a permission grant or a claim that notices are unnecessary.
The index and hash manifest omit their own cyclic hashes; external ZIP hashes cover them.

The companion source ZIP contains:

- The exact native project snapshot, PDF4QT snapshot, and vcpkg port/version snapshot.
- Matching Qt source for deployed Qt modules, including their third-party source.
- Extracted/patched dependency source left in vcpkg `buildtrees/*/src` by this build.
- Actual installed vcpkg SPDX metadata and status, notices, dependency lock, and provenance.

The script deliberately retains extracted dependency sources and does not distribute
the runner's downloaded executable build tools. To rebuild, extract the native project
snapshot, publish that source and its workflow to a build branch, and dispatch the
workflow. It obtains the same locked library inputs on a fresh runner. Compiler/SDK
availability is a build prerequisite. The `windows-2022` image changes over time;
image identity is recorded, and byte-for-byte reproducibility is not claimed.

The native project must contain `LICENSE`, `LICENSE-DOCS`, and `COPYRIGHT.md`.
Original code is GPL-3.0-only and original documentation is CC-BY-SA-4.0. PDF4QT's MIT
license, embedded font/CMap terms, Qt terms, actual dependency notices, and Microsoft
runtime terms remain applicable to their respective components. The upstream notice
and Qt attribution folders are supersets of source components, not a claim that every
listed optional component is shipped. Review actual artifact contents before release.

A successful build confirms compilation and file assembly. It does not establish
Surface/tablet behavior, handwriting quality, PDF round-trip correctness, performance,
or application acceptance; these require separately authorized execution and review.

## Primary build references

- [PDF4QT core build options](https://github.com/JakubMelka/PDF4QT/blob/v1.7.0.0/CMakeLists.txt)
- [PDF4QT Windows dependency versions](https://github.com/JakubMelka/PDF4QT/blob/v1.7.0.0/.github/workflows/WindowsInstall.yml)
- [PDF4QT dependency manifest](https://github.com/JakubMelka/PDF4QT/blob/v1.7.0.0/vcpkg.json)
- [Qt deployment tool](https://doc.qt.io/qt-6/windows-deployment.html)
- [Microsoft runtime redistribution](https://learn.microsoft.com/visualstudio/releases/2022/redistribution)
