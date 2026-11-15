'use strict';

// Only user-selected dialog paths become file capabilities. Construction is lazy:
// no dialog, process or filesystem access occurs until an explicit API operation.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const { createWorkspaceStore } = require('./workspace-store.cjs');

const MAX_FILE_BYTES = 640 * 1024 * 1024;
const MAX_DIALOG_OUTPUT = 512 * 1024;
const DEVICE = /^(?:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³]|CONIN\$|CONOUT\$)(?:\.|$)/i;
class NativeFileError extends Error {
  constructor(code, message, httpStatus = 409, cause) {
    super(message);this.name = 'NativeFileError';this.code = code;this.httpStatus = httpStatus;
    if (cause) this.cause = cause;
  }
}
function fail(code, message, status, cause) { throw new NativeFileError(code, message, status, cause); }
function fileHash(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function equalPath(first, second) { return first.toLowerCase() === second.toLowerCase(); }
function canonicalDisplay(value) {
  // realpath may use an extended spelling; accept that spelling only from the OS,
  // never from an API/dialog input. Device namespaces are not file capabilities.
  if (/^\\\\\?\\UNC\\/i.test(value)) return '\\\\' + value.slice(8);
  if (/^\\\\\?\\[a-z]:\\/i.test(value)) return value.slice(4);
  return value;
}
function selectedPath(value) {
  if (typeof value !== 'string' || !value || value.length > 32768 || Buffer.from(value, 'utf8').toString('utf8') !== value || /[\u0000-\u001f\u007f]/.test(value)
    || !path.isAbsolute(value) || /^\\\\[?.]\\/.test(value)) fail('UNSAFE_FILE_PATH', '请选择普通 PDF 文件路径。', 400);
  const resolved = path.resolve(value), root = path.parse(resolved).root;
  if (!/^[a-z]:\\$/i.test(root) && !/^\\\\[^\\/:<>"|?*]+\\[^\\/:<>"|?*]+\\$/.test(root)) fail('UNSAFE_FILE_PATH', '不支持此文件路径。', 400);
  for (const component of resolved.slice(root.length).split(path.sep).filter(Boolean)) {
    if (/[. ]$/.test(component) || /[:<>"|?*]/.test(component) || DEVICE.test(component)) fail('UNSAFE_FILE_PATH', '文件路径包含不支持的名称或数据流。', 400);
  }
  if (!path.basename(resolved).toLowerCase().endsWith('.pdf')) fail('INVALID_FILE_TYPE', '请选择扩展名为 PDF 的文件。', 400);
  return resolved;
}
async function lstatMaybe(file) {
  try { return await fs.lstat(file); }
  catch (error) { if (error.code === 'ENOENT') return null;throw error; }
}
async function safeParent(directory) {
  const root = path.parse(directory).root;let cursor = root;
  for (const component of ['', ...directory.slice(root.length).split(path.sep).filter(Boolean)]) {
    if (component) cursor = path.join(cursor, component);
    const stat = await fs.lstat(cursor);
    if (stat.isSymbolicLink() || !stat.isDirectory()) fail('UNSAFE_FILE_PATH', '文件路径包含链接、目录联接或非目录项。');
  }
  const real = canonicalDisplay(await fs.realpath(directory));
  // Validate the actual directory's components as well as the selection spelling.
  selectedPath(path.join(real, 'propernotes-path-check.pdf'));
  return real;
}
async function inspectTarget(value, allowMissing = false) {
  const requested = selectedPath(value), parent = await safeParent(path.dirname(requested));
  const target = path.join(parent, path.basename(requested)), stat = await lstatMaybe(target);
  if (!stat) {
    if (allowMissing) return { path: target, parent, exists: false, stat: null };
    fail('FILE_MISSING', '选择的 PDF 已不存在，请重新打开。', 404);
  }
  if (stat.isSymbolicLink() || !stat.isFile() || stat.nlink !== 1) fail('UNSAFE_FILE_PATH', '请选择独立的普通 PDF 文件，不能使用链接、目录或设备。');
  const real = selectedPath(canonicalDisplay(await fs.realpath(target)));
  if (!equalPath(path.dirname(real), parent)) fail('UNSAFE_FILE_PATH', '文件的实际目录与选择的目录不一致。');
  return { path: real, parent, exists: true, stat };
}
async function readSnapshot(target, { includeBytes = false, requirePDF = false } = {}) {
  if (!target.exists) return { exists: false, hash: null, bytes: null };
  const handle = await fs.open(target.path, 'r');
  try {
    const before = await handle.stat();
    if (!before.isFile() || before.nlink !== 1 || before.dev !== target.stat.dev || before.ino !== target.stat.ino) fail('FILE_CHANGED_ON_DISK', 'PDF 在读取前已改变，请重新选择。');
    if (before.size > MAX_FILE_BYTES) fail('FILE_TOO_LARGE', '当前文件超过 640 MiB 处理上限。', 413);
    const digest = crypto.createHash('sha256'), chunks = [];let total = 0, prefix = Buffer.alloc(0);
    while (true) {
      const buffer = Buffer.allocUnsafe(64 * 1024), { bytesRead } = await handle.read(buffer, 0, buffer.length, null);
      if (!bytesRead) break;
      const chunk = buffer.subarray(0, bytesRead);total += bytesRead;
      if (total > MAX_FILE_BYTES) fail('FILE_TOO_LARGE', '当前文件超过 640 MiB 处理上限。', 413);
      if (prefix.length < 5) prefix = Buffer.concat([prefix, chunk.subarray(0, 5 - prefix.length)]);
      digest.update(chunk);if (includeBytes) chunks.push(chunk);
    }
    const after = await handle.stat(), current = await inspectTarget(target.path, true);
    if (!current.exists || before.size !== total || after.size !== before.size || after.mtimeMs !== before.mtimeMs
      || after.ctimeMs !== before.ctimeMs || current.stat.dev !== before.dev || current.stat.ino !== before.ino
      || current.stat.size !== after.size || current.stat.mtimeMs !== after.mtimeMs || current.stat.ctimeMs !== after.ctimeMs
      || !equalPath(current.path, target.path)) fail('FILE_CHANGED_ON_DISK', 'PDF 在读取时被其他程序修改，请重新选择。');
    if (requirePDF && (total < 5 || prefix.toString('ascii') !== '%PDF-')) fail('INVALID_PDF', '选择的文件不是可读取的 PDF。', 400);
    return { exists: true, hash: digest.digest('hex'), bytes: includeBytes ? Buffer.concat(chunks, total) : null };
  } finally { await handle.close(); }
}
function suggestedName(value) {
  let name = typeof value === 'string' ? path.basename(value) : 'Document.pdf';
  name = Buffer.from(name, 'utf8').toString('utf8').replace(/[\u0000-\u001f\u007f<>:"/\\|?*]/g, '_').trim();
  if (!name) name = 'Document.pdf';
  if (name.length > 240) name = name.slice(0, 230) + '.pdf';
  if (!name.toLowerCase().endsWith('.pdf')) name += '.pdf';
  return Buffer.from(name, 'utf8').toString('utf8');
}

function createNativeFiles({ baseDir } = {}) {
  if (typeof baseDir !== 'string' || !path.isAbsolute(baseDir)) throw new TypeError('A local application directory is required.');
  const helper = path.join(baseDir, 'Select-ProperNotesDocument.ps1'), entries = new Map(), fileQueues = new Map();
  const workspace = createWorkspaceStore({ baseDir });
  let dialogTail = Promise.resolve();
  function available() {
    if (process.platform !== 'win32') fail('NATIVE_DIALOG_UNAVAILABLE', '当前环境没有可用的 Windows 原生文件对话框。', 503);
  }
  function enqueueFile(file, operation) {
    const key = file.toLowerCase(), previous = fileQueues.get(key) || Promise.resolve();
    const running = previous.catch(() => {}).then(operation), tail = running.catch(() => {});
    fileQueues.set(key, tail);
    return running.finally(() => { if (fileQueues.get(key) === tail) fileQueues.delete(key); });
  }
  async function showDialog(kind, filename) {
    available();
    const systemRoot = process.env.SystemRoot || process.env.WINDIR;
    if (typeof systemRoot !== 'string' || !path.isAbsolute(systemRoot)) fail('NATIVE_DIALOG_UNAVAILABLE', '找不到 Windows 原生文件对话框组件。', 503);
    const powershell = path.join(systemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
    try {
      const script = await fs.lstat(helper), binary = await fs.lstat(powershell);
      if (script.isSymbolicLink() || !script.isFile() || !binary.isFile()) throw new Error('Missing dialog helper');
    } catch (error) { fail('NATIVE_DIALOG_UNAVAILABLE', '原生文件对话框组件不可用，未尝试安装。', 503, error); }
    return new Promise((resolve, reject) => {
      let child, output = '', errors = '', settled = false;
      const finish = (error, value) => { if (settled) return;settled = true;if (error) reject(error);else resolve(value); };
      try { child = spawn(powershell, ['-NoProfile', '-NoLogo', '-STA', '-File', helper], { windowsHide: true, shell: false, cwd: baseDir, stdio: ['pipe', 'pipe', 'pipe'] }); }
      catch (error) { finish(new NativeFileError('NATIVE_DIALOG_UNAVAILABLE', '无法打开 Windows 文件对话框。', 503, error));return; }
      const oversized = () => { child.kill();finish(new NativeFileError('NATIVE_DIALOG_UNAVAILABLE', '文件对话框返回内容异常。', 503)); };
      child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');
      child.stdout.on('data', value => { output += value;if (Buffer.byteLength(output, 'utf8') > MAX_DIALOG_OUTPUT) oversized(); });
      child.stderr.on('data', value => { errors += value;if (Buffer.byteLength(errors, 'utf8') > MAX_DIALOG_OUTPUT) oversized(); });
      child.on('error', error => finish(new NativeFileError('NATIVE_DIALOG_UNAVAILABLE', '无法打开 Windows 文件对话框。', 503, error)));
      child.stdin.on('error', error => { child.kill();finish(new NativeFileError('NATIVE_DIALOG_UNAVAILABLE', '无法与 Windows 文件对话框通信。', 503, error)); });
      child.on('close', code => {
        if (settled) return;
        try {
          const value = JSON.parse(output.replace(/^\uFEFF/, '').trim());
          if (code !== 0 || value?.error || typeof value?.cancelled !== 'boolean' || (!value.cancelled && typeof value.path !== 'string')) throw new Error('Dialog unavailable');
          finish(null, value.cancelled ? null : value.path);
        } catch (error) { finish(new NativeFileError('NATIVE_DIALOG_UNAVAILABLE', 'Windows 文件对话框不可用；未读取或写入选定 PDF。', 503, error)); }
      });
      // Only JSON data goes to stdin; filenames never become PowerShell code or arguments.
      child.stdin.end(JSON.stringify({ kind, suggestedName: suggestedName(filename) }), 'utf8');
    });
  }
  function select(kind, filename) {
    const next = dialogTail.catch(() => {}).then(() => showDialog(kind, filename));
    dialogTail = next.catch(() => {});return next;
  }
  function identity(file, hash, token) {
    return { token, filename: path.basename(file), sourcePath: file, documentId: 'document-' + fileHash(Buffer.from(file.toLowerCase(), 'utf8')), hash };
  }
  async function remember(metadata) {
    try { await workspace.register(metadata); }
    catch (error) {
      // Opening the PDF or publishing its save already succeeded. Metadata
      // persistence failure must not change that result into an uncertain save.
      metadata.workspaceWarning = error?.message || 'PDF 操作成功，但最近文件和会话记录未能保存。';
    }
    return metadata;
  }
  async function verifyBaseline(file, baseline) {
    const current = await inspectTarget(file, true);
    if (current.exists !== baseline.exists || !equalPath(current.path, file)) fail('FILE_CHANGED_ON_DISK', '目标 PDF 已被其他程序创建、删除或替换，已停止覆盖。');
    if (current.exists) {
      const read = await readSnapshot(current);
      if (read.hash !== baseline.hash) fail('FILE_CHANGED_ON_DISK', 'PDF 在打开或选择后已被外部修改，已停止覆盖。请另存到不同文件或重新打开核对。');
    }
  }
  async function removeTemp(file, expected) {
    const current = await lstatMaybe(file);if (!current) return;
    if (current.isSymbolicLink() || !current.isFile() || current.dev !== expected.dev || current.ino !== expected.ino) fail('TEMP_FILE_CHANGED', '本次临时文件已被替换，未自动删除。');
    await fs.unlink(file);
  }
  async function writePDF(file, bytes, baseline) {
    await verifyBaseline(file, baseline);
    const parent = await safeParent(path.dirname(file)), temporary = path.join(parent, '.propernotes-save-' + crypto.randomUUID() + '.tmp');
    let handle, tempIdentity, committed = false;
    try {
      handle = await fs.open(temporary, 'wx', 0o600);tempIdentity = await handle.stat();
      await handle.writeFile(bytes);await handle.sync();await handle.close();handle = null;
      const checkedParent = await safeParent(parent);
      if (!equalPath(checkedParent, parent)) fail('FILE_CHANGED_ON_DISK', '保存目录在写入前已改变，原文件未覆盖。');
      await verifyBaseline(file, baseline);
      // The final same-directory rename is atomic publication, not an atomic
      // hash-compare-and-swap against other applications or another server process.
      await fs.rename(temporary, file);committed = true;
    } finally {
      if (handle) await handle.close().catch(() => {});
      if (!committed && tempIdentity) await removeTemp(temporary, tempIdentity);
    }
  }
  async function open() {
    available();const chosen = await select('open');if (chosen === null) return null;
    try {
      const target = await inspectTarget(chosen);
      return await enqueueFile(target.path, async () => {
        const current = await inspectTarget(target.path), snapshot = await readSnapshot(current, { includeBytes: true, requirePDF: true });
        const token = crypto.randomBytes(32).toString('base64url'), metadata = identity(current.path, snapshot.hash, token);
        entries.set(token, { path: current.path, hash: snapshot.hash });await remember(metadata);return { bytes: snapshot.bytes, metadata };
      });
    } catch (error) {
      if (error instanceof NativeFileError) throw error;
      fail('FILE_READ_FAILED', '无法读取选择的 PDF，请检查文件是否仍存在及是否具有读取权限。', 409, error);
    }
  }
  async function reopen(id) {
    available();const selected = await workspace.resolve(id);
    try {
      const target = await inspectTarget(selected);
      return await enqueueFile(target.path, async () => {
        const current = await inspectTarget(target.path), snapshot = await readSnapshot(current, { includeBytes: true, requirePDF: true });
        const token = crypto.randomBytes(32).toString('base64url'), metadata = identity(current.path, snapshot.hash, token);
        entries.set(token, { path: current.path, hash: snapshot.hash });await remember(metadata);return { bytes: snapshot.bytes, metadata };
      });
    } catch (error) {
      if (error instanceof NativeFileError) throw error;
      fail('FILE_READ_FAILED', '无法重新读取此 PDF；文件可能已移动或权限发生变化，请重新选择。', 409, error);
    }
  }
  async function save(input = {}) {
    available();
    if (!input || typeof input !== 'object' || Array.isArray(input) || typeof input.saveAs !== 'boolean'
      || typeof input.filename !== 'string' || (input.token !== null && input.token !== undefined && typeof input.token !== 'string')) fail('INVALID_FILE_METADATA', '保存文件信息无效。', 400);
    const excludeTokens = input.excludeTokens === undefined ? [] : input.excludeTokens;
    if (!Array.isArray(excludeTokens) || excludeTokens.length > 32 || excludeTokens.some(value => typeof value !== 'string' || !/^[A-Za-z0-9_-]{43}$/.test(value))) fail('INVALID_FILE_METADATA', '其他标签的文件标识无效或超过 32 项。', 400);
    const excludeDocumentIds = input.excludeDocumentIds === undefined ? [] : input.excludeDocumentIds;
    if (!Array.isArray(excludeDocumentIds) || excludeDocumentIds.length > 32 || excludeDocumentIds.some(value => typeof value !== 'string' || !/^document-[a-f\d]{64}$/.test(value))) fail('INVALID_FILE_METADATA', '其他标签的文档标识无效或超过 32 项。', 400);
    if (!(input.bytes instanceof Uint8Array) || input.bytes.byteLength < 5 || input.bytes.byteLength > MAX_FILE_BYTES) fail('FILE_TOO_LARGE', '保存的 PDF 数据无效或超过 640 MiB。', 413);
    const bytes = Buffer.isBuffer(input.bytes) ? input.bytes : Buffer.from(input.bytes);
    if (bytes.subarray(0, 5).toString('ascii') !== '%PDF-') fail('INVALID_PDF', '保存数据不是 PDF 文件。', 400);
    // Persisted native identities protect loaded tabs with expired service tokens
    // as well as unloaded session placeholders that have no live token yet.
    // Resolve failures happen before showing a dialog or writing any PDF bytes.
    const excludedPaths = excludeDocumentIds.length ? await workspace.resolveMany(excludeDocumentIds) : [];
    const known = typeof input.token === 'string' ? entries.get(input.token) : null;
    if (typeof input.token === 'string' && !input.saveAs && !known) fail('FILE_TOKEN_INVALID', '原文件授权已失效，请使用另存为重新选择文件。');
    let target, baseline;
    try {
      if (known && !input.saveAs) {
        target = { path: known.path };baseline = { exists: true, hash: known.hash };
      } else {
        // An expired token is allowed for Save As: the new dialog supplies fresh authorization.
        const chosen = await select('save', input.filename);if (chosen === null) return null;
        target = await inspectTarget(chosen, true);baseline = await readSnapshot(target);
        if (known && equalPath(known.path, target.path) && (!baseline.exists || baseline.hash !== known.hash)) fail('FILE_CHANGED_ON_DISK', '原 PDF 已被外部修改，不能通过另存到同一路径绕过核对。');
      }
      const digest = fileHash(bytes), selectedToken = known && equalPath(known.path, target.path) ? input.token : crypto.randomBytes(32).toString('base64url');
      const metadata = identity(target.path, digest, selectedToken);
      return await enqueueFile(target.path, async () => {
        // Tokens only identify existing capabilities. An unknown token grants
        // no access and cannot supply a path. Avoid replacing another live tab's
        // source before that tab has had a chance to save its own changes.
        if (excludedPaths.some(file => equalPath(file, target.path)) || excludeTokens.some(token => { const other = entries.get(token);return other && equalPath(other.path, target.path); })) fail('TARGET_OPEN_IN_ANOTHER_TAB', '目标 PDF 已在另一个标签中打开，请切换到该标签保存，或另存到其他文件。');
        await writePDF(target.path, bytes, baseline);
        entries.set(selectedToken, { path: target.path, hash: digest });await remember(metadata);return metadata;
      });
    } catch (error) {
      if (error instanceof NativeFileError) throw error;
      fail('FILE_SAVE_FAILED', 'PDF 未确认保存成功；请保留当前窗口，并检查文件权限、占用或可用空间。', 409, error);
    }
  }
  return Object.freeze({ open, reopen, save, getWorkspace: workspace.get, updateWorkspace: workspace.update });
}

module.exports = { createNativeFiles, NativeFileError, MAX_FILE_BYTES };
