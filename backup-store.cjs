'use strict';

// Lazy local backup storage. Importing or constructing this module performs no I/O.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { TextDecoder } = require('node:util');

const SETTINGS_FORMAT = 'propernotes-backup-settings';
const MANIFEST_FORMAT = 'propernotes-backup-manifest';
const VERSION = 1;
// Matches the editable outer-PDF limit (256 MiB source * 2.5), not a RAM guarantee.
const MAX_PDF_BYTES = 640 * 1024 * 1024;
const MAX_SETTINGS_BYTES = 1024 * 1024;
const MAX_MANIFEST_BYTES = 16 * 1024 * 1024;
const MAX_ROOTS = 128;
const MAX_DOCUMENTS = 10000;
const ID = /^[A-Za-z0-9_-]{1,160}$/;
const SNAPSHOT_ID = /^snap-[a-f\d]{8}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{12}$/;
const HASH = /^[a-f\d]{64}$/;
const MANIFEST_NAME = '.propernotes-backups.json';
const LOCK_NAME = '.propernotes-backup.lock';
const OWNED_PDF = /^propernotes-[a-f\d]{24}-snap-[a-f\d-]{36}\.pdf$/i;
const RESERVED_DIRECTORIES = new Set(['vendor', 'runtime', 'licenses', 'node_modules', 'tests', 'qa', '.git', '.codex']);
const decoder = new TextDecoder('utf-8', { fatal: true });

class BackupStoreError extends Error {
  constructor(code, message, httpStatus = 409, cause) {
    super(message);this.name = 'BackupStoreError';this.code = code;this.httpStatus = httpStatus;
    if (cause) this.cause = cause;
  }
}
function fail(code, message, status, cause) { throw new BackupStoreError(code, message, status, cause); }
function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function hash(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function clone(value) { return JSON.parse(JSON.stringify(value)); }
function comparable(file) { return process.platform === 'win32' ? file.toLowerCase() : file; }
function samePath(first, second) { return comparable(path.resolve(first)) === comparable(path.resolve(second)); }
function contains(directory, file) {
  const relative = path.relative(directory, file);
  return relative === '' || (relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative));
}
function validId(value, label) {
  if (typeof value !== 'string' || !ID.test(value)) fail('INVALID_ID', label + '无效。', 400);
  return value;
}
function directoryValue(value) {
  if (typeof value !== 'string' || !value || value.includes('\0') || !path.isAbsolute(value) || /^\\\\[?.]\\/.test(value.replaceAll('/', '\\'))) fail('INVALID_DIRECTORY', '请选择普通绝对路径作为备份目录。', 400);
  if (process.platform === 'win32' && !/^[a-z]:[\\/]/i.test(value)) fail('NETWORK_DIRECTORY_UNSUPPORTED', '当前备份只支持本地驱动器目录，暂不支持 UNC 或设备命名路径。', 400);
  const remainder = /^[a-z]:[\\/]/i.test(value) ? value.slice(2) : value;
  if (remainder.split(/[\\/]/).filter(Boolean).some(part => /[ .]$/.test(part) || /[:<>"|?*\u0000-\u001f\u007f]/.test(part) || /^(?:con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³]|conin\$|conout\$|clock\$)$/i.test(part.split('.')[0].trimEnd()))) fail('INVALID_DIRECTORY', '备份路径不能包含设备名、数据流、尾点、尾空格或特殊路径别名。', 400);
  return path.resolve(value);
}
function plainRealPath(value) {
  if (process.platform === 'win32') {
    if (value.toLowerCase().startsWith('\\\\?\\unc\\')) value = '\\\\' + value.slice(8);
    else if (value.startsWith('\\\\?\\')) value = value.slice(4);
  }
  return directoryValue(value);
}
function intervalValue(value) {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0.1 || value > 1440) fail('INVALID_SETTINGS', '备份间隔须在 0.1 到 1440 分钟之间。', 400);
  return value;
}
function countValue(value) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 1000) fail('INVALID_SETTINGS', '每个 PDF 的保留数量须为 0 到 1000 的整数。', 400);
  return value;
}
function metadataName(value) {
  if (typeof value !== 'string' || !value.trim() || value.length > 260 || /[\u0000-\u001f\u007f\\/]/.test(value)) fail('INVALID_METADATA', 'PDF 文件名无效。', 400);
  return value;
}
function metadataPath(value) {
  if (value === undefined || value === null || value === '') return null;
  if (typeof value !== 'string' || value.length > 32768 || /[\u0000-\u001f\u007f]/.test(value)) fail('INVALID_METADATA', 'PDF 来源路径无效。', 400);
  return value;
}
function revisionValue(value) {
  if (value === undefined) return undefined;
  if (Number.isSafeInteger(value) && value >= 0) return value;
  if (typeof value === 'string' && value.length <= 160 && !/[\u0000-\u001f\u007f]/.test(value)) return value;
  fail('INVALID_METADATA', '备份修改版本无效。', 400);
}
function pdfBytes(value) {
  if (!(value instanceof Uint8Array)) fail('INVALID_PDF', '缺少 PDF 备份数据。', 400);
  if (value.byteLength < 5 || value.byteLength > MAX_PDF_BYTES) fail('PDF_SIZE', 'PDF 备份须不超过 640 MiB。', 413);
  const bytes = Buffer.from(value);
  if (bytes.subarray(0, 5).toString('ascii') !== '%PDF-') fail('INVALID_PDF', '备份数据不是 PDF 文件。', 400);
  return bytes;
}
async function statMaybe(file) {
  try { return await fs.lstat(file); } catch (error) { if (error.code === 'ENOENT') return null;throw error; }
}
async function safeDirectory(directory, create = false) {
  const resolved = path.resolve(directory), parsed = path.parse(resolved);
  let cursor = parsed.root;
  const parts = resolved.slice(parsed.root.length).split(path.sep).filter(Boolean);
  for (const part of ['', ...parts]) {
    if (part) cursor = path.join(cursor, part);
    let stat = await statMaybe(cursor);
    if (!stat) {
      if (!create) return false;
      try { await fs.mkdir(cursor); } catch (error) { if (error.code !== 'EEXIST') throw error; }
      stat = await fs.lstat(cursor);
    }
    if (stat.isSymbolicLink() || !stat.isDirectory()) fail('UNSAFE_DIRECTORY', '备份路径包含符号链接、目录联接或非目录项，已停止操作。', 409);
  }
  return true;
}
async function canonicalDirectory(value) {
  const directory = directoryValue(value);
  await safeDirectory(directory, false);
  let cursor = directory;const missing = [];
  while (!await statMaybe(cursor)) {
    const parent = path.dirname(cursor);
    if (parent === cursor) fail('INVALID_DIRECTORY', '备份路径的磁盘或共享根不存在。', 400);
    missing.unshift(path.basename(cursor));cursor = parent;
  }
  const stat = await fs.lstat(cursor);
  if (stat.isSymbolicLink() || !stat.isDirectory()) fail('UNSAFE_DIRECTORY', '备份目录包含非目录或重解析链接。', 409);
  const canonical = path.resolve(plainRealPath(await fs.realpath(cursor)), ...missing);
  await safeDirectory(canonical, false);
  return canonical;
}
async function directoryIdentity(directory, { optional = false } = {}) {
  if (optional) {
    const entry = await statMaybe(directory);
    if (!entry || entry.isSymbolicLink() || !entry.isDirectory()) return null;
  }
  if (!await safeDirectory(directory, false)) return null;
  const stat = await fs.stat(directory, { bigint: true });
  return stat.dev > 0n && stat.ino > 0n ? stat.dev.toString() + ':' + stat.ino.toString() : null;
}
async function safeFile(file, { allowMissing = false, limit = MAX_PDF_BYTES } = {}) {
  if (!await safeDirectory(path.dirname(file))) {
    if (allowMissing) return null;
    fail('FILE_MISSING', '备份目录不存在，已有数据未改动。', 404);
  }
  const stat = await statMaybe(file);
  if (!stat) {
    if (allowMissing) return null;
    fail('FILE_MISSING', '备份文件不存在。', 404);
  }
  if (stat.isSymbolicLink() || !stat.isFile() || stat.nlink !== 1) fail('UNSAFE_FILE', '目标不是独立普通文件，已停止操作以保留原数据。', 409);
  if (stat.size > limit) fail('FILE_SIZE', '备份数据或索引超过当前处理范围。', 413);
  return stat;
}
async function readSmall(file, limit) {
  const stat = await safeFile(file, { allowMissing: true, limit });
  if (!stat) return null;
  const bytes = await fs.readFile(file);
  if (bytes.length > limit) fail('FILE_SIZE', '备份索引超过当前处理范围。', 413);
  return bytes;
}
function parseJSON(bytes, label) {
  try {
    const value = JSON.parse(decoder.decode(bytes));
    if (!object(value)) throw new Error('Expected object');
    return value;
  } catch (error) { fail('CORRUPT_INDEX', label + '无法读取；已保留原文件，未重置。', 409, error); }
}
function ownedPath(directory, documentId, snapshotId) {
  validId(documentId, '文档标识');
  if (!SNAPSHOT_ID.test(snapshotId)) fail('INVALID_ID', '备份标识无效。', 400);
  const basename = 'propernotes-' + hash(Buffer.from(documentId)).slice(0, 24) + '-' + snapshotId + '.pdf';
  const result = path.resolve(directory, basename);
  if (!contains(directory, result) || path.dirname(result) !== path.resolve(directory)) fail('UNSAFE_PATH', '备份路径越界，已停止操作。', 409);
  return result;
}
async function removeTemporary(file, identity) {
  await safeDirectory(path.dirname(file), false);
  const stat = await statMaybe(file);
  if (!stat) return;
  if (stat.isSymbolicLink() || !stat.isFile() || stat.dev !== identity.dev || stat.ino !== identity.ino) fail('UNSAFE_FILE', '临时文件已被替换，已保留该文件。', 409);
  await fs.unlink(file);
}
async function atomicFile(file, bytes, { expected = null, createOnly = false } = {}) {
  await safeDirectory(path.dirname(file), false);
  const temporary = path.join(path.dirname(file), '.propernotes-' + crypto.randomUUID() + '.tmp');
  let handle, identity;
  try {
    handle = await fs.open(temporary, 'wx', 0o600);identity = await handle.stat();
    await handle.writeFile(bytes);await handle.sync();await handle.close();handle = null;
    await safeDirectory(path.dirname(file), false);
    if (createOnly) {
      // link() publishes without replacing an existing path. The temporary link
      // is removed before the new PDF is registered in its manifest.
      try { await fs.link(temporary, file); }
      catch (error) { fail('ATOMIC_WRITE_FAILED', '无法安全提交备份文件，原有文件未被覆盖。请检查备份目录。', 409, error); }
    } else {
      const current = await readSmall(file, Math.max(MAX_MANIFEST_BYTES, bytes.length));
      if ((current === null) !== (expected === null) || (current && !current.equals(expected))) fail('INDEX_CHANGED', '备份配置或索引已被其他程序修改，已停止覆盖。', 409);
      await fs.rename(temporary, file);
      identity = null;
    }
  } finally {
    if (handle) await handle.close().catch(() => {});
    if (identity) await removeTemporary(temporary, identity);
  }
}
function encodeJSON(value, maximum) {
  const bytes = Buffer.from(JSON.stringify(value, null, 2) + '\n');
  if (bytes.length > maximum) fail('INDEX_SIZE', '备份配置或索引超过当前处理范围，原记录未覆盖。', 413);
  return bytes;
}
async function acquireLock(directory) {
  const file = path.join(directory, LOCK_NAME);
  let handle;
  try { handle = await fs.open(file, 'wx', 0o600); }
  catch (error) {
    if (error.code === 'EEXIST') fail('STORE_BUSY', '备份存储正被占用或留有未完成操作的锁文件；已有数据未改动。', 409);
    throw error;
  }
  let identity;
  try { identity = await handle.stat(); }
  catch (error) { await handle.close().catch(() => {});throw error; }
  return async () => { await handle.close();await removeTemporary(file, identity); };
}

function createBackupStore({ baseDir } = {}) {
  let base = directoryValue(baseDir), settingsFile = path.join(base, 'propernotes-settings.json');
  let defaultDirectory = path.join(base, 'Backups'), initialization = null;
  const portableId = 'root-' + crypto.randomUUID().replaceAll('-', '');
  let tail = Promise.resolve();
  async function initializeBase() {
    initialization ||= (async () => {
      if (!await safeDirectory(base, false)) fail('INVALID_DIRECTORY', '程序目录不存在，未创建备份存储。', 409);
      base = await canonicalDirectory(base);settingsFile = path.join(base, 'propernotes-settings.json');
      const requested = path.join(base, 'Backups'), canonical = await canonicalDirectory(requested);
      if (!samePath(requested, canonical)) fail('UNSAFE_DIRECTORY', '默认 Backups 目录被解析到其他路径，已停止操作。', 409);
      defaultDirectory = canonical;
    })();
    try { await initialization; } catch (error) { initialization = null;throw error; }
  }
  function rootFor(directory, settings) {
    if (samePath(directory, defaultDirectory)) return { ...(settings?.roots.find(root => root.portable) || { rootId: portableId }), directory, portable: true };
    return { rootId: 'root-' + hash(Buffer.from(comparable(directory))).slice(0, 32), directory, portable: false };
  }
  async function checkDirectory(value) {
    await initializeBase();
    const directory = await canonicalDirectory(value);
    if (contains(directory, base)) fail('INVALID_DIRECTORY', '备份目录不能是程序根目录或其上级目录。', 400);
    if (contains(base, directory)) {
      const first = path.relative(base, directory).split(path.sep)[0].toLowerCase();
      if (RESERVED_DIRECTORIES.has(first)) fail('INVALID_DIRECTORY', '备份目录不能占用程序资源目录。', 400);
    }
    const identity = await directoryIdentity(directory);
    if (identity) {
      for (let ancestor = base;; ancestor = path.dirname(ancestor)) {
        if (identity === await directoryIdentity(ancestor)) fail('INVALID_DIRECTORY', '备份路径指向程序目录或其上级目录的别名，已停止操作。', 400);
        if (path.dirname(ancestor) === ancestor) break;
      }
      const protectedIdentities = new Set();
      for (const name of RESERVED_DIRECTORIES) {
        const protectedIdentity = await directoryIdentity(path.join(base, name), { optional: true });
        if (protectedIdentity) protectedIdentities.add(protectedIdentity);
      }
      for (let ancestor = directory;; ancestor = path.dirname(ancestor)) {
        const ancestorIdentity = await directoryIdentity(ancestor);
        if (ancestorIdentity && protectedIdentities.has(ancestorIdentity)) fail('INVALID_DIRECTORY', '备份路径指向程序资源目录的别名，已停止操作。', 400);
        if (path.dirname(ancestor) === ancestor) break;
      }
    }
    return directory;
  }
  function defaultSettings() {
    const root = rootFor(defaultDirectory);
    return { format: SETTINGS_FORMAT, version: VERSION, intervalMinutes: 5, maxPerDocument: 10, directory: defaultDirectory, activeRootId: root.rootId, roots: [root] };
  }
  async function settingsRecord({ validatePortable = true } = {}) {
    await initializeBase();
    if (!samePath(defaultDirectory, await canonicalDirectory(defaultDirectory))) fail('DIRECTORY_CHANGED', '默认备份目录的实际路径已改变，已停止操作。', 409);
    const raw = await readSmall(settingsFile, MAX_SETTINGS_BYTES);
    if (!raw) {
      if (validatePortable && await safeDirectory(defaultDirectory, false)) {
        if (await statMaybe(path.join(defaultDirectory, MANIFEST_NAME)) || (await fs.readdir(defaultDirectory)).some(name => OWNED_PDF.test(name))) fail('SETTINGS_MISSING', '检测到已有默认备份，但配置文件缺失；已停止初始化以保留原有备份设置和文件。', 409);
      }
      return { value: defaultSettings(), raw: null };
    }
    const value = parseJSON(raw, '备份配置');
    try {
      if (value.format !== SETTINGS_FORMAT || value.version !== VERSION || !Array.isArray(value.roots) || !value.roots.length || value.roots.length > MAX_ROOTS) throw new Error('Unsupported settings');
      intervalValue(value.intervalMinutes);countValue(value.maxPerDocument);
      if (value.activeRootId !== undefined) validId(value.activeRootId, '当前目录标识');
      const storedDirectory = directoryValue(value.directory);
      const storedActive = value.activeRootId ?? value.roots.find(root => object(root) && typeof root.directory === 'string' && samePath(root.directory, storedDirectory))?.rootId;
      const ids = new Set(), directories = new Set(), physicalDirectories = new Set(), registeredPaths = new Map();let portableCount = 0;
      for (const root of value.roots) {
        if (!object(root)) throw new Error('Invalid root');
        validId(root.rootId, '目录标识');
        if (root.portable !== undefined && typeof root.portable !== 'boolean') throw new Error('Invalid portable flag');
        const registered = directoryValue(root.directory);
        registeredPaths.set(root.rootId, registered);
        if (root.portable === true) {
          if (++portableCount > 1) throw new Error('Duplicate portable root');
          root.directory = await checkDirectory(defaultDirectory);
        } else {
          root.directory = await checkDirectory(registered);
          if (!samePath(registered, root.directory) || root.rootId !== 'root-' + hash(Buffer.from(comparable(root.directory))).slice(0, 32)) throw new Error('Root alias changed');
          if (samePath(root.directory, defaultDirectory)) { root.portable = true;if (++portableCount > 1) throw new Error('Duplicate portable root'); }
        }
        if (ids.has(root.rootId) || directories.has(comparable(root.directory))) throw new Error('Invalid root identity');
        const physical = await directoryIdentity(root.directory);
        if (physical && physicalDirectories.has(physical)) throw new Error('Duplicate physical root');
        if (physical) physicalDirectories.add(physical);
        if (root.portable && validatePortable) {
          const manifestBytes = await readSmall(path.join(root.directory, MANIFEST_NAME), MAX_MANIFEST_BYTES);
          if (manifestBytes) {
            const manifest = parseJSON(manifestBytes, '默认备份索引');
            if (manifest.format !== MANIFEST_FORMAT || manifest.version !== VERSION || manifest.rootId !== root.rootId) throw new Error('Portable manifest identity mismatch');
          }
        }
        ids.add(root.rootId);directories.add(comparable(root.directory));
      }
      const active = value.roots.find(root => root.rootId === storedActive);
      if (!active) throw new Error('Active root missing');
      if (!samePath(storedDirectory, registeredPaths.get(active.rootId))) throw new Error('Stored active directory mismatch');
      if (!active.portable && !samePath(storedDirectory, active.directory)) throw new Error('Active directory mismatch');
      value.directory = active.directory;value.activeRootId = active.rootId;
    } catch (error) { fail('CORRUPT_SETTINGS', '备份配置结构无效或版本不支持；已保留原文件，未重置。', 409, error); }
    return { value, raw };
  }
  function publicSettings(settings) {
    return { intervalMinutes: settings.intervalMinutes, maxPerDocument: settings.maxPerDocument, directory: settings.directory, defaultDirectory,
      rootId: settings.activeRootId, roots: clone(settings.roots), capabilities: { migrateExisting: true } };
  }
  async function writeSettings(record, value) {
    const bytes = encodeJSON(value, MAX_SETTINGS_BYTES);
    await atomicFile(settingsFile, bytes, { expected: record.raw });record.value = value;record.raw = bytes;
  }
  function emptyManifest(root) { return { format: MANIFEST_FORMAT, version: VERSION, rootId: root.rootId, documents: [] }; }
  async function manifestRecord(root, { allowOrphans = false } = {}) {
    const file = path.join(root.directory, MANIFEST_NAME);
    if (!samePath(root.directory, await canonicalDirectory(root.directory))) fail('DIRECTORY_CHANGED', '已登记备份目录的实际路径发生变化，已停止操作。', 409);
    if (!await safeDirectory(root.directory)) return { root, file, raw: null, value: emptyManifest(root), orphans: [] };
    const raw = await readSmall(file, MAX_MANIFEST_BYTES);
    if (!raw) {
      if ((await fs.readdir(root.directory)).some(name => OWNED_PDF.test(name))) fail('MANIFEST_MISSING', '目录内存在 ProperNotes 备份，但索引缺失；已停止操作并保留文件。', 409);
      return { root, file, raw: null, value: emptyManifest(root), orphans: [] };
    }
    const value = parseJSON(raw, '备份索引');
    try {
      if (value.format !== MANIFEST_FORMAT || value.version !== VERSION || value.rootId !== root.rootId || !Array.isArray(value.documents) || value.documents.length > MAX_DOCUMENTS) throw new Error('Invalid manifest');
      const documents = new Set(), snapshots = new Set();
      for (const document of value.documents) {
        if (!object(document)) throw new Error('Invalid document');
        validId(document.documentId, '文档标识');metadataName(document.filename);metadataPath(document.sourcePath);
        if (documents.has(document.documentId) || !Array.isArray(document.snapshots) || document.snapshots.length > 100000) throw new Error('Invalid document entries');
        documents.add(document.documentId);
        for (const snapshot of document.snapshots) {
          const allowedSnapshotKeys = new Set(['id', 'createdAt', 'sequence', 'size', 'hash', 'revision', 'pendingDelete', 'replacement', 'filename', 'sourcePath']);
          if (!object(snapshot) || typeof snapshot.id !== 'string' || !SNAPSHOT_ID.test(snapshot.id) || snapshots.has(snapshot.id)
            || typeof snapshot.createdAt !== 'string' || !Number.isFinite(Date.parse(snapshot.createdAt)) || new Date(snapshot.createdAt).toISOString() !== snapshot.createdAt
            || !Number.isSafeInteger(snapshot.size) || snapshot.size < 5 || snapshot.size > MAX_PDF_BYTES || typeof snapshot.hash !== 'string' || !HASH.test(snapshot.hash)
            || (snapshot.pendingDelete !== undefined && typeof snapshot.pendingDelete !== 'boolean')
            || (snapshot.sequence !== undefined && (!Number.isSafeInteger(snapshot.sequence) || snapshot.sequence < 0))
            || Object.keys(snapshot).some(key => !allowedSnapshotKeys.has(key))) throw new Error('Invalid snapshot');
          if (snapshot.filename !== undefined) metadataName(snapshot.filename);
          if (snapshot.sourcePath !== undefined) metadataPath(snapshot.sourcePath);
          if (snapshot.replacement !== undefined) {
            const replacement = snapshot.replacement;
            if (!snapshot.pendingDelete || !object(replacement) || Object.keys(replacement).some(key => !['rootId', 'documentId', 'snapshotId'].includes(key))
              || typeof replacement.rootId !== 'string' || !ID.test(replacement.rootId) || replacement.rootId === root.rootId || replacement.documentId !== document.documentId || replacement.snapshotId !== snapshot.id) throw new Error('Invalid replacement reference');
          }
          revisionValue(snapshot.revision);snapshots.add(snapshot.id);ownedPath(root.directory, document.documentId, snapshot.id);
        }
      }
    } catch (error) { fail('CORRUPT_MANIFEST', '备份索引结构或所有权标记无效；已保留原文件，未重置。', 409, error); }
    const indexed = new Set(value.documents.flatMap(document => document.snapshots.map(snapshot => comparable(path.basename(ownedPath(root.directory, document.documentId, snapshot.id))))));
    const orphans = (await fs.readdir(root.directory)).filter(name => OWNED_PDF.test(name) && !indexed.has(comparable(name)));
    if (orphans.length && !allowOrphans) fail('ORPHAN_BACKUP', '备份目录存在未登记文件，已保留并停止操作，请核对目录 ' + root.directory + '：' + orphans.slice(0, 3).join('、') + (orphans.length > 3 ? ' 等' : ''), 409);
    return { root, file, raw, value, orphans };
  }
  async function writeManifest(record) {
    const bytes = encodeJSON(record.value, MAX_MANIFEST_BYTES);
    await atomicFile(record.file, bytes, { expected: record.raw });record.raw = bytes;
  }
  async function verifySnapshot(record, document, snapshot, allowMissing = false) {
    const file = ownedPath(record.root.directory, document.documentId, snapshot.id);
    const stat = await safeFile(file, { allowMissing, limit: MAX_PDF_BYTES });
    if (!stat) return null;
    if (stat.size !== snapshot.size) fail('SNAPSHOT_CHANGED', '备份文件大小已改变，已停止操作以保留文件。', 409);
    const bytes = await fs.readFile(file);
    if (bytes.length !== snapshot.size || bytes.subarray(0, 5).toString('ascii') !== '%PDF-' || hash(bytes) !== snapshot.hash) fail('SNAPSHOT_CHANGED', '备份文件校验失败，已保留文件，未恢复或删除。', 409);
    return { file, bytes, stat };
  }
  async function unlinkVerified(verified) {
    await safeDirectory(path.dirname(verified.file), false);
    const current = await safeFile(verified.file, { allowMissing: true, limit: MAX_PDF_BYTES });
    if (!current) return;
    if (current.dev !== verified.stat.dev || current.ino !== verified.stat.ino || current.size !== verified.stat.size || current.mtimeMs !== verified.stat.mtimeMs || current.ctimeMs !== verified.stat.ctimeMs) fail('SNAPSHOT_CHANGED', '备份文件在核对后发生变化，已停止删除并保留文件。', 409);
    await fs.unlink(verified.file);
  }
  async function finishPending(record) {
    const pending = [];
    for (const document of record.value.documents) for (const snapshot of document.snapshots) if (snapshot.pendingDelete) pending.push({ document, snapshot });
    if (!pending.length) return;
    for (const { document, snapshot } of pending) {
      if (snapshot.replacement) {
        const settings = (await settingsRecord()).value, root = settings.roots.find(item => item.rootId === snapshot.replacement.rootId);
        if (!root) fail('MIGRATION_COPY_MISSING', '迁移目标目录已失去登记，旧备份已保留。', 409);
        const replacementRecord = await manifestRecord(root), replacementDocument = replacementRecord.value.documents.find(item => item.documentId === document.documentId);
        const replacementSnapshot = replacementDocument?.snapshots.find(item => item.id === snapshot.id);
        if (!replacementSnapshot || replacementSnapshot.pendingDelete || replacementSnapshot.hash !== snapshot.hash || replacementSnapshot.size !== snapshot.size) fail('MIGRATION_COPY_MISSING', '迁移目标副本无法可靠对应，旧备份已保留。', 409);
        await verifySnapshot(replacementRecord, replacementDocument, replacementSnapshot);
      }
      const verified = await verifySnapshot(record, document, snapshot, true);
      if (verified) await unlinkVerified(verified);
    }
    for (const document of record.value.documents) document.snapshots = document.snapshots.filter(snapshot => !snapshot.pendingDelete);
    record.value.documents = record.value.documents.filter(document => document.snapshots.length);
    await writeManifest(record);
  }
  async function removeSnapshots(record, selected, { replacementRootId } = {}) {
    if (!selected.length) return { removedCount: 0, removedBytes: 0 };
    let removedBytes = 0;
    for (const item of selected) if (await verifySnapshot(record, item.document, item.snapshot, true)) removedBytes += item.snapshot.size;
    for (const item of selected) {
      item.snapshot.pendingDelete = true;
      if (replacementRootId) item.snapshot.replacement = { rootId: replacementRootId, documentId: item.document.documentId, snapshotId: item.snapshot.id };
      else delete item.snapshot.replacement;
    }
    // A durable deletion intention lets a later mutation finish safely after an
    // interrupted unlink/index commit, without treating unexpected missing data as success.
    await writeManifest(record);
    await finishPending(record);
    return { removedCount: selected.length, removedBytes };
  }
  function enqueue(operation) {
    const pending = tail.then(operation);tail = pending.catch(() => {});return pending;
  }
  function mutation(operation, { createCurrent = false, prepareMigration = false, migrationTarget } = {}) {
    return enqueue(async () => {
      await initializeBase();
      const releases = [];let operationError = null;
      try {
        releases.push(await acquireLock(base));
        const settings = await settingsRecord();
        if (createCurrent && settings.value.maxPerDocument > 0) {
          await safeDirectory(settings.value.directory, true);
          if (!samePath(settings.value.directory, await canonicalDirectory(settings.value.directory))) fail('DIRECTORY_CHANGED', '备份目录的实际路径已改变，已停止操作。', 409);
        }
        const lockRoots = [...settings.value.roots];
        if (prepareMigration) {
          const directory = await checkDirectory(migrationTarget ?? settings.value.directory);
          await safeDirectory(directory, true);
          if (!samePath(directory, await canonicalDirectory(directory))) fail('DIRECTORY_CHANGED', '迁移目标实际路径已改变，已停止操作。', 409);
          if (!lockRoots.some(root => samePath(root.directory, directory))) lockRoots.push(rootFor(directory, settings.value));
        }
        const roots = lockRoots.sort((a, b) => comparable(a.directory).localeCompare(comparable(b.directory)));
        const physicalLocks = new Set(), baseIdentity = await directoryIdentity(base);
        if (baseIdentity) physicalLocks.add(baseIdentity);
        for (const root of roots) if (await safeDirectory(root.directory)) {
          const physical = await directoryIdentity(root.directory);
          if (physical && physicalLocks.has(physical)) fail('DUPLICATE_DIRECTORY', '备份目录实际指向同一位置，已停止操作，未等待或移除现有锁。', 409);
          if (physical) physicalLocks.add(physical);
          releases.push(await acquireLock(root.directory));
        }
        return await operation(settings);
      } catch (error) {
        operationError = error;
        if (error instanceof BackupStoreError) throw error;
        fail('BACKUP_IO', '备份存储操作失败，未确认成功；请保留现有文件并检查目录权限或可用空间。', 409, error);
      } finally {
        let releaseError = null;
        for (const release of releases.reverse()) {
          try { await release(); } catch (error) { releaseError ||= error; }
        }
        if (releaseError && !operationError) fail('LOCK_RELEASE_FAILED', '备份操作已结束，但存储锁未能清理；请检查目录，暂不重复执行。', 409, releaseError);
      }
    });
  }
  async function recordsFor(settings, { allowOrphanRootId } = {}) { return Promise.all(settings.roots.map(root => manifestRecord(root, { allowOrphans: root.rootId === allowOrphanRootId }))); }
  function snapshotOrder(snapshot) { return snapshot.sequence ?? Date.parse(snapshot.createdAt); }
  function compareHistory(first, second) { return snapshotOrder(first.snapshot) - snapshotOrder(second.snapshot) || first.snapshot.id.localeCompare(second.snapshot.id); }
  function versionHistory(records, documentId, preferredRootId) {
    const versions = new Map();
    for (const record of records) for (const document of record.value.documents) if (document.documentId === documentId) for (const snapshot of document.snapshots) {
      const copy = { record, document, snapshot }, previous = versions.get(snapshot.id);
      if (previous) {
        if (previous.snapshot.hash !== snapshot.hash || previous.snapshot.size !== snapshot.size) fail('SNAPSHOT_CONFLICT', '相同备份标识对应了不同内容，已停止轮替并保留所有副本。', 409);
        previous.copies.push(copy);
        if (record.root.rootId === preferredRootId) Object.assign(previous, copy);
      } else versions.set(snapshot.id, { ...copy, copies: [copy] });
    }
    return [...versions.values()].sort(compareHistory);
  }
  async function enforceRetention(records, maximum, onlyDocumentId, preferredRootId) {
    if (maximum === 0) return;
    const ids = onlyDocumentId ? new Set([onlyDocumentId]) : new Set(records.flatMap(record => record.value.documents.map(document => document.documentId)));
    for (const documentId of ids) {
      const history = versionHistory(records, documentId, preferredRootId);
      const cutoff = Math.max(0, history.length - maximum);
      if (!cutoff) continue;
      // A valid index entry alone cannot justify discarding the older recoverable
      // versions. Verify every retained physical copy before recording any new
      // deletion intention for this document; a missing/corrupt survivor stops
      // rotation and preserves all its older versions.
      try {
        for (const version of history.slice(cutoff)) for (const copy of version.copies) await verifySnapshot(copy.record, copy.document, copy.snapshot);
      } catch (error) {
        fail('RETENTION_SURVIVOR_INVALID', '拟保留的备份缺失或校验失败，已停止此 PDF 的数量轮替并保留旧备份。', 409, error);
      }
      const remove = history.slice(0, cutoff).flatMap(item => item.copies);
      for (const record of records) await removeSnapshots(record, remove.filter(item => item.record === record));
    }
  }
  async function rollbackUnindexedSnapshot(record, document, snapshot) {
    const current = await manifestRecord(record.root, { allowOrphans: true });
    const indexed = current.value.documents.find(item => item.documentId === document.documentId)?.snapshots.find(item => item.id === snapshot.id);
    if (indexed) return false; // An ambiguous I/O failure may already have committed the index.
    const verified = await verifySnapshot(record, document, snapshot, true);
    if (verified) await unlinkVerified(verified);
    return true;
  }
  async function migrateRecords(settingsRecord, next) {
    const targetId = next.roots.find(root => samePath(root.directory, next.directory)).rootId;
    const records = await recordsFor(next, { allowOrphanRootId: targetId }), target = records.find(record => record.root.rootId === targetId);
    const sources = records.filter(record => record !== target);
    for (const record of records) for (const document of record.value.documents) if (document.snapshots.some(snapshot => snapshot.pendingDelete)) fail('MIGRATION_PENDING_CLEANUP', '仍有未完成的备份清理，请先处理这些记录再迁移；本次目录未切换。', 409);
    const merged = clone(target.value), documents = new Map(merged.documents.map(document => [document.documentId, document]));
    const snapshots = new Map(), copyTasks = [], sourceEntries = new Map();
    for (const document of merged.documents) for (const snapshot of document.snapshots) {
      await verifySnapshot(target, document, snapshot);
      snapshots.set(snapshot.id, { document, snapshot });
    }
    // Complete source/target preflight before publishing any copied PDF or changing active settings.
    for (const record of sources) {
      const entries = [];sourceEntries.set(record, entries);
      for (const sourceDocument of record.value.documents) for (const sourceSnapshot of sourceDocument.snapshots) {
        await verifySnapshot(record, sourceDocument, sourceSnapshot);
        entries.push({ document: sourceDocument, snapshot: sourceSnapshot });
        const existing = snapshots.get(sourceSnapshot.id);
        if (existing) {
          if (existing.document.documentId !== sourceDocument.documentId || existing.snapshot.hash !== sourceSnapshot.hash || existing.snapshot.size !== sourceSnapshot.size) fail('MIGRATION_CONFLICT', '目标目录的相同备份标识对应不同文档或内容，已停止迁移，原文件未覆盖。', 409);
          continue;
        }
        let document = documents.get(sourceDocument.documentId);
        if (!document) {
          if (documents.size >= MAX_DOCUMENTS) fail('DOCUMENT_LIMIT', '合并后的备份文档数量超过当前范围，未切换目录。', 409);
          document = { documentId: sourceDocument.documentId, filename: sourceDocument.filename, sourcePath: sourceDocument.sourcePath ?? null, snapshots: [] };
          documents.set(document.documentId, document);merged.documents.push(document);
        }
        const snapshot = { ...clone(sourceSnapshot), filename: sourceSnapshot.filename ?? sourceDocument.filename,
          sourcePath: Object.prototype.hasOwnProperty.call(sourceSnapshot, 'sourcePath') ? sourceSnapshot.sourcePath : sourceDocument.sourcePath ?? null };
        if (document.snapshots.length >= 100000) fail('INDEX_SIZE', '合并后的单文档备份数量超过当前范围，未切换目录。', 409);
        document.snapshots.push(snapshot);snapshots.set(snapshot.id, { document, snapshot });
        // A prior interrupted copy may have left this exact, unindexed destination.
        // Reuse it only after checking size, PDF header and its full hash; never replace it.
        const existingFile = await verifySnapshot(target, document, snapshot, true);
        copyTasks.push({ record, sourceDocument, sourceSnapshot, document, snapshot, reuse: !!existingFile });
      }
    }
    const destinationNames = new Set(merged.documents.flatMap(document => document.snapshots.map(snapshot => comparable(path.basename(ownedPath(target.root.directory, document.documentId, snapshot.id))))));
    if (target.orphans.some(name => !destinationNames.has(comparable(name)))) fail('ORPHAN_BACKUP', '迁移目标含有无法对应源索引的未登记备份，已保留文件并停止迁移。', 409);
    for (const document of merged.documents) {
      document.snapshots.sort((a, b) => snapshotOrder(a) - snapshotOrder(b) || a.id.localeCompare(b.id));
      const latest = document.snapshots.at(-1);
      if (latest?.filename) document.filename = latest.filename;
      if (latest && Object.prototype.hasOwnProperty.call(latest, 'sourcePath')) document.sourcePath = latest.sourcePath;
    }
    encodeJSON(merged, MAX_MANIFEST_BYTES); // Check manifest capacity before copying.
    if (target.raw === null) await writeManifest(target); // Persist ownership for safe retries.
    let copiedCount = 0, reusedCount = 0;
    for (const task of copyTasks) {
      if (task.reuse) reusedCount++;
      else {
        const source = await verifySnapshot(task.record, task.sourceDocument, task.sourceSnapshot);
        await atomicFile(ownedPath(target.root.directory, task.document.documentId, task.snapshot.id), source.bytes, { createOnly: true });
        copiedCount++;
      }
      await verifySnapshot(target, task.document, task.snapshot);
    }
    for (const document of merged.documents) for (const snapshot of document.snapshots) await verifySnapshot(target, document, snapshot);
    // Indexed copies are committed first. Any later settings failure leaves all
    // source indexes intact, so retrying can verify/reuse the copies without data loss.
    target.value = merged;await writeManifest(target);
    await writeSettings(settingsRecord, next);
    const warnings = [];let removedCount = 0;
    for (const record of sources) {
      try { removedCount += (await removeSnapshots(record, sourceEntries.get(record), { replacementRootId: target.root.rootId })).removedCount; }
      catch (error) { warnings.push({ rootId: record.root.rootId, code: error.code || 'CLEANUP_FAILED', message: '目标副本已保留，但此旧目录清理未完成：' + error.message }); }
    }
    if (next.maxPerDocument > 0) {
      try {
        const afterMigration = await recordsFor(next);
        for (const record of afterMigration) await finishPending(record);
        await enforceRetention(afterMigration, next.maxPerDocument, undefined, next.activeRootId);
      } catch (error) { warnings.push({ code: error.code || 'RETENTION_FAILED', message: '迁移副本已保留，但保留数量轮替未完成：' + error.message }); }
    }
    return { ...publicSettings(next), warnings, migration: { status: warnings.length ? 'completed-with-warnings' : 'complete', copiedCount, reusedCount, removedCount } };
  }
  async function isPrivatePath(file) {
    if (typeof file !== 'string' || file.includes('\0')) return true;
    await initializeBase();
    let resolved = directoryValue(path.resolve(file));
    const stat = await statMaybe(resolved);
    if (stat?.isSymbolicLink()) return true;
    if (stat) resolved = plainRealPath(await fs.realpath(resolved));
    else resolved = path.join(await canonicalDirectory(path.dirname(resolved)), path.basename(resolved));
    const basename = path.basename(resolved);
    if (samePath(resolved, settingsFile) || basename === MANIFEST_NAME || OWNED_PDF.test(basename) || (contains(base, resolved) && (basename === LOCK_NAME || /^\.propernotes-[a-f\d-]+\.tmp$/.test(basename)))) return true;
    if (contains(defaultDirectory, resolved)) return true;
    const { value } = await settingsRecord({ validatePortable: false });
    return value.roots.some(root => contains(root.directory, resolved));
  }
  async function getSettings() { return enqueue(async () => publicSettings((await settingsRecord()).value)); }
  async function updateSettings(patch = {}) {
    if (!object(patch)) fail('INVALID_SETTINGS', '备份设置无效。', 400);
    if (patch.migrateExisting !== undefined && typeof patch.migrateExisting !== 'boolean') fail('INVALID_SETTINGS', '备份迁移选项无效。', 400);
    const requestedDirectory = patch.directory === undefined ? undefined : await checkDirectory(patch.directory);
    return mutation(async record => {
      const next = clone(record.value);
      if (patch.intervalMinutes !== undefined) next.intervalMinutes = intervalValue(patch.intervalMinutes);
      if (patch.maxPerDocument !== undefined) next.maxPerDocument = countValue(patch.maxPerDocument);
      if (requestedDirectory !== undefined) {
        const directory = requestedDirectory;
        await safeDirectory(directory, false);
        if (!next.roots.some(root => samePath(root.directory, directory))) {
          if (next.roots.length >= MAX_ROOTS) fail('ROOT_LIMIT', '登记的备份目录过多，原设置未改变。', 409);
          next.roots.push(rootFor(directory, next));
        }
        next.directory = directory;next.activeRootId = next.roots.find(root => samePath(root.directory, directory)).rootId;
      }
      if (patch.migrateExisting === true) return migrateRecords(record, next);
      const records = await recordsFor(next); // Refuse an unreadable/foreign existing manifest before committing settings.
      await writeSettings(record, next);
      if (next.maxPerDocument > 0) {
        try {
          for (const item of records) await finishPending(item);
          await enforceRetention(records, next.maxPerDocument, undefined, next.activeRootId);
        } catch (error) {
          // Configuration has already committed. Return it so clients adopt the
          // actual interval/directory/limit without mistaking cleanup for success.
          return { ...publicSettings(next), warnings: [{ code: error.code || 'RETENTION_FAILED', message: '设置已保存，但已有备份清理未完成：' + (error.message || '请保留现有备份并核对备份目录。') }] };
        }
      }
      return publicSettings(next);
    }, { prepareMigration: patch.migrateExisting === true, migrationTarget: requestedDirectory });
  }
  async function list() {
    return enqueue(async () => {
      const settings = (await settingsRecord()).value, records = await recordsFor(settings), documents = [];
      for (const record of records) for (const document of record.value.documents) {
        const snapshots = [];
        for (const snapshot of document.snapshots) {
          const file = ownedPath(record.root.directory, document.documentId, snapshot.id);
          const stat = await safeFile(file, { allowMissing: true, limit: MAX_PDF_BYTES });
          snapshots.push({ ...clone(snapshot), available: !!stat && stat.size === snapshot.size });
        }
        documents.push({ rootId: record.root.rootId, documentId: document.documentId, filename: document.filename, sourcePath: document.sourcePath ?? null,
          count: snapshots.length, totalBytes: snapshots.filter(snapshot => snapshot.available).reduce((sum, snapshot) => sum + snapshot.size, 0), snapshots });
      }
      return { roots: clone(settings.roots), documents };
    });
  }
  async function createSnapshot(input = {}) {
    if (!object(input)) fail('INVALID_METADATA', '备份请求无效。', 400);
    const documentId = validId(input.documentId, '文档标识'), filename = metadataName(input.filename), sourcePath = metadataPath(input.sourcePath);
    const bytes = pdfBytes(input.bytes), digest = hash(bytes), revision = revisionValue(input.revision);
    return mutation(async settingsRecord => {
      const settings = settingsRecord.value, root = settings.roots.find(item => samePath(item.directory, settings.directory));
      if (settings.maxPerDocument === 0) return { created: false, reason: 'disabled', rootId: root.rootId, documentId };
      const records = await recordsFor(settings);
      for (const record of records) await finishPending(record);
      const history = versionHistory(records, documentId, root.rootId);
      const latest = history.at(-1);
      if (latest && latest.snapshot.hash === digest) {
        await verifySnapshot(latest.record, latest.document, latest.snapshot);
        await enforceRetention(records, settings.maxPerDocument, documentId, root.rootId);
        return { created: false, reason: 'unchanged', rootId: latest.record.root.rootId, documentId, snapshot: clone(latest.snapshot) };
      }
      if (settingsRecord.raw === null) await writeSettings(settingsRecord, settings);
      const record = records.find(item => item.root.rootId === root.rootId);
      if (record.raw === null) await writeManifest(record);
      let document = record.value.documents.find(item => item.documentId === documentId);
      if (!document) {
        if (record.value.documents.length >= MAX_DOCUMENTS) fail('DOCUMENT_LIMIT', '备份索引文档数量超过当前范围。', 409);
        document = { documentId, filename, sourcePath, snapshots: [] };record.value.documents.push(document);
      }
      document.filename = filename;document.sourcePath = sourcePath;
      const sequence = Math.max(Date.now(), history.reduce((maximum, item) => Math.max(maximum, snapshotOrder(item.snapshot)), 0) + 1);
      if (!Number.isSafeInteger(sequence)) fail('INDEX_SIZE', '备份顺序标记超过当前范围，原记录未覆盖。', 409);
      const snapshot = { id: 'snap-' + crypto.randomUUID(), createdAt: new Date().toISOString(), sequence, size: bytes.length, hash: digest, filename, sourcePath };
      if (revision !== undefined) snapshot.revision = revision;
      await atomicFile(ownedPath(root.directory, documentId, snapshot.id), bytes, { createOnly: true });
      document.snapshots.push(snapshot);
      try { await writeManifest(record); }
      catch (error) {
        try { await rollbackUnindexedSnapshot(record, document, snapshot); }
        catch (rollbackError) { error.message += ' 新备份文件可能已保留，需核对：' + rollbackError.message; }
        throw error;
      }
      history.push({ record, document, snapshot, copies: [{ record, document, snapshot }] });
      history.sort(compareHistory);
      await enforceRetention(records, settings.maxPerDocument, documentId, root.rootId);
      return { created: true, rootId: root.rootId, documentId, snapshot: clone(snapshot) };
    }, { createCurrent: true });
  }
  async function readSnapshot({ rootId, documentId, snapshotId } = {}) {
    validId(rootId, '目录标识');validId(documentId, '文档标识');
    if (typeof snapshotId !== 'string' || !SNAPSHOT_ID.test(snapshotId)) fail('INVALID_ID', '备份标识无效。', 400);
    return enqueue(async () => {
      const settings = (await settingsRecord()).value, root = settings.roots.find(item => item.rootId === rootId);
      if (!root) fail('ROOT_NOT_FOUND', '备份目录未登记。', 404);
      const record = await manifestRecord(root), document = record.value.documents.find(item => item.documentId === documentId);
      const snapshot = document?.snapshots.find(item => item.id === snapshotId);
      if (!snapshot) fail('SNAPSHOT_NOT_FOUND', '未找到此 PDF 的备份。', 404);
      const verified = await verifySnapshot(record, document, snapshot);
      return { bytes: verified.bytes, metadata: { ...clone(snapshot), rootId, documentId, snapshotId, filename: snapshot.filename ?? document.filename,
        sourcePath: Object.prototype.hasOwnProperty.call(snapshot, 'sourcePath') ? snapshot.sourcePath : document.sourcePath ?? null } };
    });
  }
  async function cleanup({ rootId, documentIds } = {}) {
    validId(rootId, '目录标识');
    if (!Array.isArray(documentIds) || documentIds.length > MAX_DOCUMENTS) fail('INVALID_ID', '请明确选择要清理的 PDF 标识。', 400);
    const ids = new Set(documentIds.map(id => validId(id, '文档标识')));
    return mutation(async settings => {
      const root = settings.value.roots.find(item => item.rootId === rootId);
      if (!root) fail('ROOT_NOT_FOUND', '备份目录未登记。', 404);
      const record = await manifestRecord(root), selected = [];
      for (const document of record.value.documents) if (ids.has(document.documentId)) for (const snapshot of document.snapshots) selected.push({ document, snapshot });
      return removeSnapshots(record, selected);
    });
  }
  return Object.freeze({ getSettings, updateSettings, changeSettings: updateSettings, list, listBackups: list, createSnapshot, readSnapshot, cleanup, isPrivatePath });
}

module.exports = { createBackupStore, BackupStoreError, MAX_PDF_BYTES };
