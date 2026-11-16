# Native prototype limits

English (en): This is the focused Qt/PDF4QT handwriting prototype. It implements a new single-PDF editing format; it is not the full previous feature set. Actual device response, palm rejection, file compatibility and recovery behavior require acceptance. Compilation alone does not establish them.

Traditional Chinese (zh-TC): 此版是聚焦 Qt/PDF4QT 手寫閉環的原型, 使用新的單 PDF 編輯格式, 尚未移植前版全部功能. 裝置手感、防誤觸、檔案相容及異常恢復仍須驗收, 編譯成功不代表已完成驗收.

Simplified Chinese (zh-SC): 此版是聚焦 Qt/PDF4QT 手写闭环的原型, 使用新的单 PDF 编辑格式, 尚未移植前版全部功能. 设备手感、防误触、文件兼容及异常恢复仍须验收, 编译成功不代表已经完成验收.

- Pen pressure is collected through Qt tablet events when the device/driver reports it. Mouse input is a constant-pressure fallback. Device and driver coverage is not asserted.
- The 0.3.0 source adds reading layouts, view history, page overview, session document undo/redo, partial erasure, selection/movement, basic text boxes and the confirmed input/single-use policies. Text selection/search/review markup, shapes, bookmarks, tabs/recent files/session restoration, full custom navigation/shortcuts and other previously confirmed first-batch tools remain to be migrated. This is not the complete first batch.
- Input and serialized output are currently capped at 512MiB. A 1GB-class document is a future design/acceptance target, not supported capacity claimed by this build. See LARGE-DOCUMENTS.md for the verified parser/serialization constraints and planned work.
- Recovery backups are full PDFs in the native backup catalog, not edits to the original file. Restore creates a separate PDF. Legacy web-preview backup catalogs are not imported. Atomic writes, source checks and verified copies do not guarantee recovery under every disk, power or filesystem failure.
- An interrupted migration can leave pending entries visible for inspection or retry. Source data is preserved until destination copies are verified and committed. A corrupt or foreign catalog is protected rather than silently repaired.
- The mouse/touch switch and persistent/single-use tools follow the user's explicit rules. This does not assert that every corresponding Drawboard branch or default has been independently verified. Device buttons beyond a reported eraser end are not mapped by guessing.
- Older ProperNotes v1-v4 data, unknown/inconsistent native data, protected document cases and nonstandard page units are not silently migrated or overwritten. The UI shows a read-only reason where applicable. Keep the previous preview for legacy editing.
- Pressure-shaped vector appearances and the original pressure samples are stored in the same PDF. Other readers can display the standard annotation appearance; they are not promised to preserve or edit all ProperNotes-specific pressure data after their own modifications.
- Saving checks source/target byte changes and uses a temporary atomic-file commit. This does not provide a cross-application compare-and-swap guarantee or guarantee recovery under every storage failure.
- No application's actual launch, PDF round trip, handwriting device behavior or automated test is claimed by the build recipe. Consult included provenance and validation records for the exact scope.
- New native v2 files with text boxes are protected as read-only by older native builds. This build reads native v1 ink with its original interpretation; the older web preview's v1-v4 catalog is a separate legacy format and remains protected from overwrite.
- Text boxes use plain Unicode text and a raster PDF appearance, not embedded font programs. The native editing overlay uses available system fonts and may fall back if the original family is missing. The saved appearance is retained for unchanged objects. Other readers may not preserve this program's editable metadata after their own edits.
- Editing and view histories are bounded in-memory session records; saving does not clear editing history, but closing/reopening does. Facing layouts pair (1,2), (3,4); this stage has no separate-cover option. Page-image cache budgets do not include all decoded PDF resources, canvas overlays or transient allocations, and are not a 1GB-document capacity claim.
