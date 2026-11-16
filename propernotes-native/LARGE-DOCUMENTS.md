# Large-document design and device compatibility

Original explanatory text: CC BY-SA 4.0. This is a design record, not a capacity benchmark.

## English (en)

The target is broad Windows pen-device compatibility and practical handling of large PDFs,
including 1GB-class documents as a future acceptance target. The current native preview
still has a 512MiB input/output safety limit. It has not been launched or benchmarked.
The current package is Windows x64; ARM64 and individual device/driver coverage remain
separate work. Real pressure requires the device and driver to report it through Qt.

### What the comparison establishes

SumatraPDF 3.6.1 reads small local files into memory, but keeps larger files open for
random-access reads. Its 32MiB threshold selects a reading strategy; it is not a maximum
PDF size. Its rendering cache also uses tiles. No universal 1GB capacity guarantee was
found in the official material examined. Particular issue reports are examples, not
benchmarks for every document. See the [file-stream policy](https://github.com/sumatrapdfreader/sumatrapdf/blob/3.6.1rel/src/EngineMupdf.cpp#L434)
and [render cache](https://github.com/sumatrapdfreader/sumatrapdf/blob/3.6.1rel/src/RenderCache.h#L11).
The [inspected development commit](https://github.com/sumatrapdfreader/sumatrapdf/blob/b1a8e22577c9e35064315d279fc33ba6b67c5bef/src/shared/EngineMupdf.cpp#L890)
retains this input strategy. Special repair/cache paths can still read a full buffer;
the ordinary file-stream path is not a blanket memory guarantee.

The pinned PDF4QT reader calls `readAll()` even for its device entry point, then creates
and parses the full object store. Changing only the application's input wrapper would
not make this parser lazy. See the [reader](https://github.com/JakubMelka/PDF4QT/blob/3e50f636551907db3f018e99748fe7daa56ff497/Pdf4QtLibCore/sources/pdfdocumentreader.cpp#L83)
and [object parsing](https://github.com/JakubMelka/PDF4QT/blob/3e50f636551907db3f018e99748fe7daa56ff497/Pdf4QtLibCore/sources/pdfdocumentreader.cpp#L571).

Our adapter currently retains the document model, creates a complete output buffer,
and reparses candidate bytes before committing a save. File size alone therefore does
not bound peak memory. Compressed images, object streams, page dimensions and handwriting
density all affect the cost. A tile cache bounds page bitmaps, not these other layers.
Full-PDF recovery backups also incur serialization and disk-space costs.

### Planned work, in order

| Step | Concrete work | Boundary |
| --- | --- | --- |
| Render budget | Render visible tiles first; use byte-budgeted tile/nearby-page caches, lower-resolution previews, cancellation and eviction | Does not remove the full input/object model |
| Save budget | Stream output to a same-volume temporary file; inspect I/O errors, validate the candidate and atomically commit after conflict checks | Verification still needs an explicit memory plan |
| Parser/object budget | Random-access source, lazy stream loading or disk spill, bounded object/font/image caches; evaluate changes at the PDF4QT object layer | Requires engine work, not merely replacing `readAll()` with a loop or mapping |
| Acceptance | Large scans, many-page vector PDFs, giant individual pages and object-stream-heavy files; measure open/navigation/ink/save/reopen, peak memory and cancellation | Run only after execution authorization; no current result is claimed |

PDF4QT exposes [device output](https://github.com/JakubMelka/PDF4QT/blob/3e50f636551907db3f018e99748fe7daa56ff497/Pdf4QtLibCore/sources/pdfdocumentwriter.cpp#L245).
Future streaming work must retain sticky write-error checks and complete validation;
the writer return value alone is insufficient evidence of a successful filesystem write.
Its [incremental interface](https://github.com/JakubMelka/PDF4QT/blob/3e50f636551907db3f018e99748fe7daa56ff497/Pdf4QtLibCore/sources/pdfdocumentwriter.cpp#L435)
also accepts and reparses the full original byte buffer, so it is not already a bounded-memory solution.

These are engineering proposals inferred from the verified code paths, not measured
results. No SumatraPDF or MuPDF code or dependency is introduced by this comparison.
Qt + PDF4QT remains the selected application architecture.

## Traditional Chinese (zh-TC)

方向為盡可能相容 Windows 筆裝置, 並把 1GB 級 PDF 納入後續設計與驗收目標.
目前仍有 512MiB 輸入/輸出防線, 尚未啟動、測試或量測容量. 本包為 x64,
ARM64、各筆/數位板/驅動及防誤觸另行驗收, 不由裝置名稱推定壓感一定可用.

SumatraPDF 的 32MiB 是小檔整讀/大檔檔案流的策略分界, 不是容量上限.
目前 PDF4QT 會整本讀取及解析, 本適配器儲存還有輸出緩衝及重讀驗證.
只加頁面/瓦片快取不能解決這些峰值. 後續依次處理可見瓦片與位元組預算、
暫存檔串流儲存、解析器/物件層延遲讀取與磁碟暫存, 再分樣本驗收.
上述為有來源依據的設計, 尚未實作為 1GB 容量保證; 對照來源見英文段落.

## Simplified Chinese (zh-SC)

方向为尽可能兼容 Windows 笔设备, 并把 1GB 级 PDF 纳入后续设计与验收目标.
目前仍有 512MiB 输入/输出防线, 尚未启动、测试或测量容量. 本包为 x64,
ARM64、各笔/数位板/驱动及防误触另行验收, 不由设备名称推定压感一定可用.

SumatraPDF 的 32MiB 是小文件整读/大文件流的策略分界, 不是容量上限.
目前 PDF4QT 会整本读取及解析, 本适配器保存还有输出缓冲及重读验证.
只加页面/瓦片缓存不能解决这些峰值. 后续依次处理可见瓦片与字节预算、
临时文件流式保存、解析器/对象层延迟读取与磁盘暂存, 再分样本验收.
上述为有来源依据的设计, 尚未实现为 1GB 容量保证; 对照来源见英文段落.
