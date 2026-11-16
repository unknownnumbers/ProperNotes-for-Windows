# Native prototype limits

English (en): This is the focused Qt/PDF4QT handwriting prototype. It implements a new single-PDF editing format; it is not the full previous feature set. Actual device response, palm rejection, file compatibility and recovery behavior require acceptance. Compilation alone does not establish them.

Traditional Chinese (zh-TC): 此版是聚焦 Qt/PDF4QT 手寫閉環的原型, 使用新的單 PDF 編輯格式, 尚未移植前版全部功能. 裝置手感、防誤觸、檔案相容及異常恢復仍須驗收, 編譯成功不代表已完成驗收.

Simplified Chinese (zh-SC): 此版是聚焦 Qt/PDF4QT 手写闭环的原型, 使用新的单 PDF 编辑格式, 尚未移植前版全部功能. 设备手感、防误触、文件兼容及异常恢复仍须验收, 编译成功不代表已经完成验收.

- Pen pressure is collected through Qt tablet events when the device/driver reports it. Mouse input is a constant-pressure fallback. Device and driver coverage is not asserted.
- The prototype has pen, whole-stroke erasure, current-page undo, page navigation/zoom, native settings, preferences and native recovery-backup management. Text tools, page editing, continuous/facing layouts, tabs/session recovery, full ink/navigation settings, custom shortcuts and other previous-preview tools remain to be migrated.
- Input and serialized output are currently capped at 512MiB. A 1GB-class document is a future design/acceptance target, not supported capacity claimed by this build. See LARGE-DOCUMENTS.md for the verified parser/serialization constraints and planned work.
- Recovery backups are full PDFs in the native backup catalog, not edits to the original file. Restore creates a separate PDF. Legacy web-preview backup catalogs are not imported. Atomic writes, source checks and verified copies do not guarantee recovery under every disk, power or filesystem failure.
- An interrupted migration can leave a registered pending entry without its payload. The original source copy is preserved; clear the incomplete entry through Backup Management before retrying that migration. A corrupt or foreign catalog is protected rather than silently repaired.
- The device-mode switch and Tool Lock behavior from Drawboard remain unverified and are not implemented as guessed equivalents.
- Older ProperNotes v1-v4 data, unknown/inconsistent native data, protected document cases and nonstandard page units are not silently migrated or overwritten. The UI shows a read-only reason where applicable. Keep the previous preview for legacy editing.
- Pressure-shaped vector appearances and the original pressure samples are stored in the same PDF. Other readers can display the standard annotation appearance; they are not promised to preserve or edit all ProperNotes-specific pressure data after their own modifications.
- Saving checks source/target byte changes and uses a temporary atomic-file commit. This does not provide a cross-application compare-and-swap guarantee or guarantee recovery under every storage failure.
- No application's actual launch, PDF round trip, handwriting device behavior or automated test is claimed by the build recipe. Consult included provenance and validation records for the exact scope.
