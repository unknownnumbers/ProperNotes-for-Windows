'use strict';

// Metadata only. Paths enter this registry through a successful native PDF
// operation, never through a browser-supplied path. No I/O occurs on import.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const MAX_BYTES = 4 * 1024 * 1024, MAX_RECORDS = 256, MAX_RECENTS = 100, MAX_TABS = 32;
const ID = /^document-[a-f\d]{64}$/;
const LAYOUTS = ['single-continuous', 'double-continuous', 'single-page', 'double-page'];
const MODES = ['fit', 'whole', 'height', 'custom'];
class WorkspaceError extends Error {
  constructor(code, message, httpStatus = 409) { super(message);this.name = 'WorkspaceError';this.code = code;this.httpStatus = httpStatus; }
}
function fail(code, message, status) { throw new WorkspaceError(code, message, status); }
function plain(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function copy(value) { return JSON.parse(JSON.stringify(value)); }
function idFor(file) { return 'document-' + crypto.createHash('sha256').update(file.toLowerCase(), 'utf8').digest('hex'); }
function finite(value, minimum, maximum) { return typeof value === 'number' && Number.isFinite(value) && value >= minimum && value <= maximum; }
function validPath(value) {
  if (typeof value !== 'string' || !value || value.length > 32768 || !path.isAbsolute(value)
    || /[\u0000-\u001f\u007f]/.test(value) || /^\\\\[?.]\\/.test(value)
    || !value.toLowerCase().endsWith('.pdf') || path.resolve(value) !== value) return false;
  const root = path.parse(value).root;
  if (!/^[a-z]:\\$/i.test(root) && !/^\\\\[^\\/:<>"|?*]+\\[^\\/:<>"|?*]+\\$/.test(root)) return false;
  return value.slice(root.length).split(path.sep).filter(Boolean).every(part => !/[. ]$/.test(part)
    && !/[:<>"|?*]/.test(part) && !/^(?:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³]|CONIN\$|CONOUT\$)(?:\.|$)/i.test(part));
}
function readingValue(value) {
  if (!plain(value) || !Number.isInteger(value.pageIndex) || value.pageIndex < 0 || value.pageIndex >= 10000
    || !finite(value.zoom, .1, 4) || !MODES.includes(value.zoomMode) || !LAYOUTS.includes(value.layout)
    || !finite(value.scrollLeft, 0, 16000000) || !finite(value.scrollTop, 0, 16000000)) fail('INVALID_WORKSPACE', '阅读位置数据无效。', 400);
  let anchor = null;
  if (value.anchor !== null && value.anchor !== undefined) {
    const item = value.anchor;
    if (!plain(item) || !Number.isInteger(item.index) || item.index < 0 || item.index >= 10000
      || !finite(item.x, -16000000, 16000000) || !finite(item.y, -16000000, 16000000)) fail('INVALID_WORKSPACE', '阅读锚点数据无效。', 400);
    anchor = { index: item.index, x: item.x, y: item.y };
  }
  return { pageIndex: value.pageIndex, zoom: value.zoom, zoomMode: value.zoomMode, layout: value.layout,
    anchor, scrollLeft: value.scrollLeft, scrollTop: value.scrollTop };
}
function empty() { return { version: 1, restoreSession: true, records: [], recents: [], session: { tabs: [], activeId: null } }; }
function sessionValue(value, records) {
  if (!plain(value) || !Array.isArray(value.tabs) || value.tabs.length > MAX_TABS) fail('INVALID_WORKSPACE', '会话标签数据无效或超过 32 个。', 400);
  const ids = new Set(), tabs = value.tabs.map(item => {
    if (!plain(item) || !ID.test(item.id) || !records.has(item.id) || ids.has(item.id)
      || (item.dirty !== undefined && typeof item.dirty !== 'boolean')) fail('INVALID_WORKSPACE', '会话包含未知或重复文档。', 400);
    ids.add(item.id);return { id: item.id, dirty: item.dirty === true };
  });
  if (value.activeId !== null && (!ID.test(value.activeId) || !ids.has(value.activeId))) fail('INVALID_WORKSPACE', '活动标签标识无效。', 400);
  return { tabs, activeId: value.activeId };
}
function normalize(value) {
  if (!plain(value) || value.version !== 1 || typeof value.restoreSession !== 'boolean' || !Array.isArray(value.records)
    || value.records.length > MAX_RECORDS || !Array.isArray(value.recents) || value.recents.length > MAX_RECENTS) fail('CORRUPT_WORKSPACE', '最近文件和会话记录损坏，已保留原文件并停止覆盖。');
  const ids = new Set(), records = value.records.map(item => {
    if (!plain(item) || !validPath(item.sourcePath) || item.id !== idFor(item.sourcePath) || ids.has(item.id)
      || typeof item.filename !== 'string' || item.filename !== path.basename(item.sourcePath)
      || !Number.isSafeInteger(item.lastOpened) || item.lastOpened < 0) fail('CORRUPT_WORKSPACE', '最近文件和会话记录损坏，已保留原文件并停止覆盖。');
    ids.add(item.id);return { id: item.id, filename: item.filename, sourcePath: item.sourcePath, lastOpened: item.lastOpened,
      ...(item.reading === undefined ? {} : { reading: readingValue(item.reading) }) };
  });
  const seen = new Set();
  for (const id of value.recents) { if (!ID.test(id) || !ids.has(id) || seen.has(id)) fail('CORRUPT_WORKSPACE', '最近文件记录包含无效标识，原文件未覆盖。');seen.add(id); }
  return { version: 1, restoreSession: value.restoreSession, records, recents: [...value.recents], session: sessionValue(value.session, ids) };
}
function createWorkspaceStore({ baseDir } = {}) {
  if (typeof baseDir !== 'string' || !path.isAbsolute(baseDir)) throw new TypeError('An absolute application directory is required.');
  const base = path.resolve(baseDir), file = path.join(base, 'propernotes-workspace.json'), lock = path.join(base, '.propernotes-workspace.lock');
  let tail = Promise.resolve();
  async function safeBase() {
    const root = path.parse(base).root;let current = root;
    for (const part of ['', ...base.slice(root.length).split(path.sep).filter(Boolean)]) {
      if (part) current = path.join(current, part);
      const stat = await fs.lstat(current);if (stat.isSymbolicLink() || !stat.isDirectory()) fail('UNSAFE_WORKSPACE', '程序目录包含链接或非目录项，未写入会话记录。');
    }
  }
  async function readBytes() {
    let stat;
    try { stat = await fs.lstat(file); } catch (error) { if (error.code === 'ENOENT') return null;throw error; }
    if (stat.isSymbolicLink() || !stat.isFile() || stat.nlink !== 1 || stat.size > MAX_BYTES) fail('UNSAFE_WORKSPACE', '会话记录不是独立普通文件或超过 4 MiB，已停止覆盖。');
    const handle = await fs.open(file, 'r');
    try {
      const actual = await handle.stat();if (actual.dev !== stat.dev || actual.ino !== stat.ino || actual.size > MAX_BYTES) fail('WORKSPACE_CHANGED', '会话记录在读取前发生变化，请重试。');
      const bytes = await handle.readFile();if (bytes.length > MAX_BYTES) fail('UNSAFE_WORKSPACE', '会话记录超过 4 MiB，已停止覆盖。');return bytes;
    } finally { await handle.close(); }
  }
  async function load() {
    const raw = await readBytes();if (raw === null) return { value: empty(), raw };
    let value;
    try { value = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(raw)); }
    catch { fail('CORRUPT_WORKSPACE', '最近文件和会话记录无法读取，已保留原文件并停止覆盖。'); }
    if (plain(value) && Number.isInteger(value.version) && value.version > 1) fail('FUTURE_WORKSPACE', '会话记录来自更新版本，已保留原文件并停止覆盖。');
    return { value: normalize(value), raw };
  }
  async function removeOwned(target, identity) {
    const stat = await fs.lstat(target).catch(error => { if (error.code === 'ENOENT') return null;throw error; });
    if (!stat) return;
    if (stat.isSymbolicLink() || !stat.isFile() || stat.dev !== identity.dev || stat.ino !== identity.ino) fail('WORKSPACE_CHANGED', '本次会话临时文件被替换，未自动删除。');
    await fs.unlink(target);
  }
  async function commit(value, expected) {
    const bytes = Buffer.from(JSON.stringify(value, null, 2) + '\n', 'utf8');if (bytes.length > MAX_BYTES) fail('WORKSPACE_LIMIT', '会话记录超过 4 MiB，原文件未覆盖。', 413);
    const temporary = path.join(base, '.propernotes-workspace-' + crypto.randomUUID() + '.tmp');let handle, identity;
    try {
      handle = await fs.open(temporary, 'wx', 0o600);identity = await handle.stat();await handle.writeFile(bytes);await handle.sync();await handle.close();handle = null;
      await safeBase();const current = await readBytes();
      if ((current === null) !== (expected === null) || (current && !current.equals(expected))) fail('WORKSPACE_CHANGED', '会话记录被其他程序修改，已停止覆盖。');
      await fs.rename(temporary, file);identity = null;
    } finally { if (handle) await handle.close().catch(() => {});if (identity) await removeOwned(temporary, identity); }
  }
  function serial(operation) { const next = tail.catch(() => {}).then(operation);tail = next.catch(() => {});return next; }
  function snapshot(value, status = 'ready', message = '') {
    const records = new Map(value.records.map(item => [item.id, item]));
    return copy({ restoreSession: value.restoreSession, recents: value.recents.map(id => records.get(id)), records: value.records, session: value.session,
      // Session-only records let a cleared recent list still restore its open tabs.
      documents: value.session.tabs.map(item => records.get(item.id)), canWrite: status === 'ready', status, message });
  }
  async function transaction(operation) {
    await safeBase();let handle, identity;
    try { handle = await fs.open(lock, 'wx', 0o600);identity = await handle.stat(); }
    catch (error) { if (error.code === 'EEXIST') fail('WORKSPACE_BUSY', '会话记录正被占用或留有未完成操作的锁文件，已有记录未改动。');throw error; }
    try { const { value, raw } = await load();const result = await operation(value);await commit(value, raw);return result === undefined ? snapshot(value) : result; }
    finally { await handle.close();await removeOwned(lock, identity); }
  }
  return Object.freeze({
    get: () => serial(async () => {
      try { await safeBase();return snapshot((await load()).value); }
      catch (error) { return snapshot(empty(), 'unavailable', error instanceof WorkspaceError ? error.message : '无法读取最近文件和会话记录，原文件未改动。'); }
    }),
    register: metadata => serial(() => transaction(value => {
      if (!plain(metadata) || !validPath(metadata.sourcePath) || metadata.documentId !== idFor(metadata.sourcePath)
        || metadata.filename !== path.basename(metadata.sourcePath)) fail('INVALID_WORKSPACE', '本地文档信息不能注册为最近文件。', 400);
      let record = value.records.find(item => item.id === metadata.documentId);
      if (!record) { record = { id: metadata.documentId, sourcePath: metadata.sourcePath, filename: metadata.filename, lastOpened: 0 };value.records.push(record); }
      record.lastOpened = Date.now();value.recents = [record.id, ...value.recents.filter(id => id !== record.id)].slice(0, MAX_RECENTS);
      const protectedIds = new Set([...value.recents, ...value.session.tabs.map(item => item.id)]);
      value.records.sort((a, b) => b.lastOpened - a.lastOpened);
      while (value.records.length > MAX_RECORDS) { const index = value.records.findLastIndex(item => !protectedIds.has(item.id));if (index < 0) fail('WORKSPACE_LIMIT', '已登记文档达到当前上限，原记录未覆盖。');value.records.splice(index, 1); }
    })),
    resolve: id => serial(async () => {
      if (typeof id !== 'string' || !ID.test(id)) fail('INVALID_WORKSPACE', '最近文档标识无效。', 400);
      await safeBase();const { value } = await load(), record = value.records.find(item => item.id === id);
      if (!record) fail('RECENT_NOT_FOUND', '此文档没有已登记的本地路径，请通过打开按钮重新选择。', 404);
      return record.sourcePath;
    }),
    resolveMany: ids => serial(async () => {
      if (!Array.isArray(ids) || ids.length > MAX_TABS || ids.some(id => typeof id !== 'string' || !ID.test(id))) fail('INVALID_WORKSPACE', '受保护文档标识无效或超过 32 项。', 400);
      await safeBase();const { value } = await load(), records = new Map(value.records.map(item => [item.id, item.sourcePath]));
      return ids.map(id => {
        if (!records.has(id)) fail('RECENT_NOT_FOUND', '另一个标签的本地路径尚未登记，已停止保存；请先核对或关闭该标签。', 409);
        return records.get(id);
      });
    }),
    update: input => serial(() => transaction(value => {
      if (!plain(input) || Object.keys(input).some(key => !['restoreSession', 'reading', 'readings', 'session', 'removeRecentId', 'clearRecents'].includes(key))) fail('INVALID_WORKSPACE', '会话设置字段无效。', 400);
      if (input.restoreSession !== undefined) { if (typeof input.restoreSession !== 'boolean') fail('INVALID_WORKSPACE', '恢复会话设置无效。', 400);value.restoreSession = input.restoreSession; }
      if (input.readings !== undefined && (!Array.isArray(input.readings) || input.readings.length > MAX_TABS)) fail('INVALID_WORKSPACE', '批量阅读位置数据无效或超过 32 项。', 400);
      const readings = [...(input.readings || []), ...(input.reading === undefined ? [] : [input.reading])], seen = new Set();
      if (readings.length > MAX_TABS) fail('INVALID_WORKSPACE', '批量阅读位置超过 32 项。', 400);
      for (const entry of readings) {
        if (!plain(entry) || typeof entry.id !== 'string' || !ID.test(entry.id) || seen.has(entry.id)) fail('INVALID_WORKSPACE', '批量阅读位置包含重复或无效文档。', 400);
        seen.add(entry.id);const record = value.records.find(item => item.id === entry.id);
        if (!record) fail('RECENT_NOT_FOUND', '阅读位置对应的本地文档尚未登记。', 404);record.reading = readingValue(entry.value);
      }
      if (input.session !== undefined) value.session = sessionValue(input.session, new Set(value.records.map(item => item.id)));
      if (input.removeRecentId !== undefined) { if (!ID.test(input.removeRecentId)) fail('INVALID_WORKSPACE', '最近文档标识无效。', 400);value.recents = value.recents.filter(id => id !== input.removeRecentId); }
      if (input.clearRecents !== undefined) { if (input.clearRecents !== true) fail('INVALID_WORKSPACE', '清理最近文件请求无效。', 400);value.recents = []; }
    })),
  });
}
module.exports = { createWorkspaceStore, WorkspaceError };
