/* Browser fallback metadata only. No PDF bytes, path, file handle or token enters
 * this store; a recent browser file always needs an explicit re-selection. */
export const BROWSER_WORKSPACE_KEY = 'propernotes.browser-workspace.v1';
const VERSION = 1, MAX_RECORDS = 256, MAX_RECENTS = 100, MAX_TABS = 32;
const MAX_LENGTH = 1024 * 1024;
const LAYOUTS = ['single-continuous', 'double-continuous', 'single-page', 'double-page'];
const ZOOM_MODES = ['custom', 'fit', 'whole', 'height'];
const BROWSER_ID = /^browser-[a-f\d]{8}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{12}$/i;
const NATIVE_ID = /^document-[a-f\d]{64}$/i;
const HASH = /^[a-f\d]{64}$/i;
const plain = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const clone = value => JSON.parse(JSON.stringify(value));
const bounded = (value, min, max) => typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
const index = value => Number.isInteger(value) && value >= 0 && value < 10000;
const empty = () => ({ restoreSession: true, records: [], session: { tabs: [], activeId: null } });
function invalid(message) { throw new TypeError(message || '浏览器文件记录无效，未保存本次更改。'); }
function title(value) {
  if (typeof value !== 'string' || !value || value.length > 512 || /[\u0000-\u001f\u007f/\\]/.test(value)) invalid('最近文件名称无效。');
  return value;
}
function reading(value) {
  if (!plain(value) || !index(value.pageIndex) || !bounded(value.zoom, .1, 4)
    || !ZOOM_MODES.includes(value.zoomMode) || !LAYOUTS.includes(value.layout)
    || !bounded(value.scrollLeft, 0, 16000000) || !bounded(value.scrollTop, 0, 16000000)) invalid('阅读位置记录无效。');
  let anchor = null;
  if (value.anchor !== null && value.anchor !== undefined) {
    if (!plain(value.anchor) || !index(value.anchor.index) || !bounded(value.anchor.x, -16000000, 16000000) || !bounded(value.anchor.y, -16000000, 16000000)) invalid('阅读位置锚点无效。');
    anchor = { index: value.anchor.index, x: value.anchor.x, y: value.anchor.y };
  }
  return { pageIndex: value.pageIndex, zoom: value.zoom, zoomMode: value.zoomMode, layout: value.layout, anchor, scrollLeft: value.scrollLeft, scrollTop: value.scrollTop };
}
function session(value, records) {
  if (!plain(value) || !Array.isArray(value.tabs) || value.tabs.length > MAX_TABS) invalid('会话标签记录无效或超过 32 项。');
  const known = new Set(records.map(record => record.id)), seen = new Set(), tabs = [];
  for (const tab of value.tabs) {
    if (!plain(tab) || typeof tab.id !== 'string' || (!NATIVE_ID.test(tab.id) && !(BROWSER_ID.test(tab.id) && known.has(tab.id)))
      || seen.has(tab.id) || tab.dirty !== undefined && typeof tab.dirty !== 'boolean') invalid('会话包含重复或未知标签。');
    seen.add(tab.id);tabs.push({ id: tab.id, dirty: tab.dirty === true });
  }
  if (value.activeId !== null && (typeof value.activeId !== 'string' || !seen.has(value.activeId))) invalid('会话活动标签无效。');
  if (tabs.length && value.activeId === null) invalid('会话缺少活动标签。');
  return { tabs, activeId: value.activeId };
}
function model(value) {
  if (!plain(value) || typeof value.restoreSession !== 'boolean' || !Array.isArray(value.records) || value.records.length > MAX_RECORDS) invalid();
  const ids = new Set(), records = [];
  for (const record of value.records) {
    if (!plain(record) || typeof record.id !== 'string' || !BROWSER_ID.test(record.id) || ids.has(record.id) || typeof record.fingerprint !== 'string' || !HASH.test(record.fingerprint)
      || !Number.isSafeInteger(record.lastOpened) || record.lastOpened < 0 || record.lastOpened > 8640000000000000
      || typeof record.visible !== 'boolean') invalid();
    ids.add(record.id);
    const next = { id: record.id, filename: title(record.filename), fingerprint: record.fingerprint.toLowerCase(), lastOpened: record.lastOpened, visible: record.visible };
    if (record.reading !== undefined) next.reading = reading(record.reading);
    records.push(next);
  }
  if (records.filter(record => record.visible).length > MAX_RECENTS) invalid();
  return { restoreSession: value.restoreSession, records, session: session(value.session, records) };
}
function uuid() {
  try { if (typeof globalThis.crypto?.randomUUID === 'function') return globalThis.crypto.randomUUID(); } catch { /* Identity is not a security capability. */ }
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, letter => { const nibble = Math.floor(Math.random() * 16);return (letter === 'x' ? nibble : nibble & 3 | 8).toString(16); });
}
function trim(value, retainedId = null) {
  const ordered = value.records.slice().sort((a, b) => (b.id === retainedId) - (a.id === retainedId) || b.lastOpened - a.lastOpened);
  let visible = 0;for (const record of ordered) if (record.visible && ++visible > MAX_RECENTS) record.visible = false;
  const protectedIds = new Set(value.session.tabs.map(tab => tab.id));if (retainedId) protectedIds.add(retainedId);
  for (let offset = ordered.length - 1; ordered.length > MAX_RECORDS && offset >= 0; offset--) {
    if (!ordered[offset].visible && !protectedIds.has(ordered[offset].id)) ordered.splice(offset, 1);
  }
  if (ordered.length > MAX_RECORDS) invalid('文件元数据超过当前保留上限。');
  value.records = ordered;
}
/** Explicit storage injection keeps a denied localStorage getter out of module import. */
export function createBrowserWorkspace(storage) {
  let current = empty(), lastRaw = null, status = 'defaults', message = '浏览器文件只保存名称、指纹及阅读位置；重开需要重新选择文件。', canWrite = true;
  function inspect() {
    if (!storage || typeof storage.getItem !== 'function' || typeof storage.setItem !== 'function') return { canWrite: false, status: 'unavailable', message: '无法访问浏览器文件记录；本次更改仅在当前窗口有效。' };
    let raw;
    try { raw = storage.getItem(BROWSER_WORKSPACE_KEY); }
    catch { return { canWrite: false, status: 'unavailable', message: '无法读取浏览器文件记录；本次更改仅在当前窗口有效。' }; }
    if (raw === null) return { raw, value: empty(), canWrite: true, status: 'defaults', message: '尚无浏览器最近文件记录。' };
    try {
      if (typeof raw !== 'string' || !raw || raw.length > MAX_LENGTH) invalid();
      const parsed = JSON.parse(raw);
      if (!plain(parsed) || !Number.isInteger(parsed.version)) invalid();
      if (parsed.version !== VERSION) return { raw, canWrite: false, status: 'unsupported', message: '浏览器文件记录版本不受支持，原记录保留且不覆盖。' };
      return { raw, value: model(parsed.workspace), canWrite: true, status: 'ready', message: '浏览器文件元数据已读取；重开需要重新选择对应文件。' };
    } catch { return { raw, canWrite: false, status: 'corrupt', message: '浏览器文件记录损坏，原记录保留且不覆盖；本次更改仅在当前窗口有效。' }; }
  }
  function snapshot() {
    const result = clone(current);
    result.recents = result.records.filter(record => record.visible).sort((a, b) => b.lastOpened - a.lastOpened);
    return { ...result, status, message, canWrite };
  }
  const initial = inspect();if (initial.value) current = initial.value;
  lastRaw = initial.raw ?? null;({ status, message, canWrite } = initial);
  function mutate(operation) {
    // Another window may have written a future/corrupt record. Never overwrite it.
    const checked = inspect();
    if (checked.canWrite && checked.raw !== lastRaw && checked.value) current = checked.value;
    const next = clone(current), retainedId = operation(next);trim(next, retainedId);current = model(next);
    ({ status, message, canWrite } = checked);lastRaw = checked.raw ?? lastRaw;
    if (!canWrite) return snapshot();
    try {
      const raw = JSON.stringify({ version: VERSION, workspace: current });
      if (raw.length > MAX_LENGTH) invalid('浏览器文件记录超过 1 MiB，原记录未覆盖。');
      storage.setItem(BROWSER_WORKSPACE_KEY, raw);lastRaw = raw;
      status = 'saved';message = '浏览器文件元数据已保存；未保存的 PDF 内容不在会话记录中。';
    } catch {
      canWrite = false;status = 'unavailable';message = '浏览器文件记录保存失败；本次更改仅在当前窗口有效。';
    }
    return snapshot();
  }
  return Object.freeze({
    read: snapshot,
    update(patch) {
      if (!plain(patch)) invalid();
      return mutate(next => {
        if (patch.restoreSession !== undefined) { if (typeof patch.restoreSession !== 'boolean') invalid();next.restoreSession = patch.restoreSession; }
        if (patch.reading !== undefined) {
          if (!plain(patch.reading)) invalid();
          const record = next.records.find(item => item.id === patch.reading.id);if (!record) invalid('此浏览器文件没有已知记录。');
          record.reading = reading(patch.reading.value);
        }
        if (patch.session !== undefined) next.session = session(patch.session, next.records);
        if (patch.removeRecentId !== undefined) {
          if (typeof patch.removeRecentId !== 'string' || !BROWSER_ID.test(patch.removeRecentId)) invalid();
          const record = next.records.find(item => item.id === patch.removeRecentId);if (record) record.visible = false;
        }
        if (patch.clearRecents !== undefined) {
          if (typeof patch.clearRecents !== 'boolean') invalid();
          if (patch.clearRecents) for (const record of next.records) record.visible = false;
        }
      });
    },
    register(input) {
      if (!plain(input) || typeof input.fingerprint !== 'string' || !HASH.test(input.fingerprint)) invalid('浏览器文件需要有效的 SHA-256 指纹。');
      const filename = title(input.filename), fingerprint = input.fingerprint.toLowerCase();let id = input.id;
      if (id !== undefined && (typeof id !== 'string' || !BROWSER_ID.test(id))) invalid('浏览器文件标识无效。');
      const workspace = mutate(next => {
        if (id !== undefined) {
          const known = next.records.find(record => record.id === id);
          if (!known || known.fingerprint !== fingerprint) invalid('重新选择的文件与此最近文件记录不匹配。');
          known.filename = filename;known.lastOpened = Date.now();known.visible = true;
        } else {
          do { id = 'browser-' + uuid(); } while (next.records.some(record => record.id === id));
          next.records.push({ id, filename, fingerprint, lastOpened: Date.now(), visible: true });
        }
        return id;
      });
      return { entry: clone(workspace.records.find(record => record.id === id)), workspace };
    },
  });
}
