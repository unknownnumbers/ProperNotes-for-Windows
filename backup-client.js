/* Local backup API and change-driven scheduling. Importing this module does no I/O. */
const DEFAULT_INTERVAL_MINUTES = 5;
const MAX_TIMER_DELAY = 2147483647;
const MAX_PDF_BYTES = 640 * 1024 * 1024;

export class BackupClientError extends Error {
  constructor(code, message, { status = 0, details = null, cause } = {}) {
    super(message);
    this.name = 'BackupClientError';
    this.code = code;
    this.status = status;
    this.details = details;
    if (cause) this.cause = cause;
  }
}

function aborted(signal) {
  if (signal?.aborted) throw new BackupClientError('ABORTED', '本次备份操作已取消。');
}
function inputError(message) {
  return new BackupClientError('INVALID_INPUT', message);
}
function pdfBytes(value) {
  let result;
  if (value instanceof Uint8Array) result = value;
  else if (value instanceof ArrayBuffer) result = new Uint8Array(value);
  else if (ArrayBuffer.isView(value)) result = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  if (!result?.length) throw inputError('没有可提交的 PDF 备份数据。');
  if (result.length > MAX_PDF_BYTES) throw inputError('PDF 备份超过当前支持的 640 MiB。');
  return result.slice();
}
async function responseError(response) {
  let details = null;
  try {
    const text = await response.text();
    try { details = JSON.parse(text); } catch { details = text || null; }
  } catch { /* HTTP status remains available even when the body cannot be read. */ }
  const supplied = details && typeof details === 'object' ? details.error : null;
  const code = typeof supplied?.code === 'string' ? supplied.code : 'HTTP_' + response.status;
  const message = typeof supplied?.message === 'string' ? supplied.message
    : typeof details === 'string' ? details : response.statusText || '请求失败';
  return new BackupClientError(code, '本地备份服务错误（' + response.status + '）：' + message, { status: response.status, details });
}

export function createBackupClient({ baseUrl = '', fetchImpl = globalThis.fetch } = {}) {
  if (typeof fetchImpl !== 'function') throw inputError('当前环境不支持访问本地备份服务。');
  const base = String(baseUrl).replace(/\/+$/, '');
  let token = null, sessionTask = null;

  async function fetchResponse(path, options = {}) {
    aborted(options.signal);
    try {
      return await fetchImpl(base + path, { credentials: 'same-origin', cache: 'no-store', redirect: 'error', ...options });
    } catch (cause) {
      if (options.signal?.aborted || cause?.name === 'AbortError') {
        throw new BackupClientError('ABORTED', '本次备份操作已取消。', { cause });
      }
      throw new BackupClientError('NETWORK_ERROR', '无法连接本地备份服务，请稍后重试。', { cause });
    }
  }
  async function readJson(response) {
    try { return await response.json(); }
    catch (cause) {
      throw new BackupClientError('INVALID_RESPONSE', '本地备份服务返回了无法读取的数据。', { status: response.status, cause });
    }
  }
  async function session(signal) {
    aborted(signal);
    if (token) return token;
    if (!sessionTask) {
      // The handshake is shared. Aborting one caller must not cancel another
      // caller's handshake; cancelled callers are checked before their next request.
      sessionTask = (async () => {
        const response = await fetchResponse('/api/session');
        if (!response.ok) throw await responseError(response);
        const data = await readJson(response);
        if (typeof data?.token !== 'string' || !data.token) {
          throw new BackupClientError('INVALID_RESPONSE', '本地备份服务没有返回有效的会话标识。', { status: response.status });
        }
        token = data.token;
        return token;
      })();
    }
    const pending = sessionTask;
    try {
      const result = await pending;
      aborted(signal);
      return result;
    } finally {
      if (sessionTask === pending) sessionTask = null;
    }
  }
  async function request(path, { method = 'GET', json, body, headers = {}, signal, binary = false } = {}) {
    for (let attempt = 0; attempt < 2; attempt++) {
      const currentToken = await session(signal);
      aborted(signal);
      const response = await fetchResponse(path, {
        method, signal, headers: {
          'X-ProperNotes-Token': currentToken,
          ...(json === undefined ? {} : { 'Content-Type': 'application/json' }), ...headers,
        },
        ...(json === undefined ? body === undefined ? {} : { body } : { body: JSON.stringify(json) }),
      });
      if (!response.ok) {
        const error = await responseError(response);
        // This specific rejection occurs before a server mutation. Network
        // failures and other HTTP errors are never retried automatically here.
        if (attempt === 0 && response.status === 403 && error.code === 'INVALID_TOKEN') {
          if (token === currentToken) token = null;
          continue;
        }
        throw error;
      }
      if (!binary) return await readJson(response);
      if (!/\bapplication\/pdf\b/i.test(response.headers.get('Content-Type') || '')) {
        throw new BackupClientError('INVALID_RESPONSE', '本地备份服务没有返回 PDF 文件。', { status: response.status });
      }
      const length = Number(response.headers.get('Content-Length'));
      if (Number.isFinite(length) && length > MAX_PDF_BYTES) {
        await response.body?.cancel().catch(() => {});
        throw new BackupClientError('FILE_SIZE', 'PDF 备份超过当前支持的 640 MiB。', { status: response.status });
      }
      try {
        const result = new Uint8Array(await response.arrayBuffer());
        if (result.length > MAX_PDF_BYTES) throw new BackupClientError('FILE_SIZE', 'PDF 备份超过当前支持的 640 MiB。', { status: response.status });
        return result;
      }
      catch (cause) {
        if (cause instanceof BackupClientError) throw cause;
        throw new BackupClientError('INVALID_RESPONSE', '无法读取本地 PDF 备份。', { status: response.status, cause });
      }
    }
    throw new BackupClientError('INVALID_TOKEN', '无法建立有效的本地备份会话。', { status: 403 });
  }

  return Object.freeze({
    settings: (options = {}) => request('/api/backups/settings', options),
    list: (options = {}) => request('/api/backups/list', options),
    updateSettings: (settings, options = {}) => request('/api/backups/settings', { ...options, method: 'POST', json: settings }),
    cleanup: (selection, options = {}) => request('/api/backups/cleanup', { ...options, method: 'POST', json: selection }),
    async createSnapshot(snapshot, options = {}) {
      if (!snapshot || typeof snapshot.documentId !== 'string' || !snapshot.documentId || typeof snapshot.filename !== 'string' || !snapshot.filename) {
        throw inputError('备份需要有效的文档标识和文件名。');
      }
      if (snapshot.sourcePath !== undefined && snapshot.sourcePath !== null && typeof snapshot.sourcePath !== 'string') {
        throw inputError('备份源路径必须为文字，无法获取路径时请传入空值。');
      }
      if (snapshot.revision !== undefined && !(Number.isSafeInteger(snapshot.revision) && snapshot.revision >= 0)
        && !(typeof snapshot.revision === 'string' && snapshot.revision.length <= 160 && !/[\u0000-\u001f\u007f]/.test(snapshot.revision))) {
        throw inputError('备份修改版本必须为非负整数或有效的短文字标识。');
      }
      const metadata = { documentId: snapshot.documentId, filename: snapshot.filename, sourcePath: snapshot.sourcePath ?? null };
      if (snapshot.revision !== undefined) metadata.revision = snapshot.revision;
      let encodedMetadata;
      try { encodedMetadata = encodeURIComponent(JSON.stringify(metadata)); }
      catch (cause) { throw new BackupClientError('INVALID_INPUT', '备份文件信息无法编码，请检查文件名和源路径。', { cause }); }
      return request('/api/backups/snapshots', {
        ...options, method: 'POST', body: pdfBytes(snapshot.bytes),
        headers: { 'Content-Type': 'application/pdf', 'X-ProperNotes-Metadata': encodedMetadata },
      });
    },
    async readSnapshot(selection, options = {}) {
      const query = new URLSearchParams();
      for (const key of ['rootId', 'documentId', 'snapshotId']) {
        if (typeof selection?.[key] !== 'string' || !selection[key]) throw inputError('请选择完整的备份记录。');
        query.set(key, selection[key]);
      }
      return request('/api/backups/snapshot?' + query.toString(), { ...options, binary: true });
    },
  });
}

function scheduleSettings(value) {
  if (!value || !Number.isFinite(value.intervalMinutes) || value.intervalMinutes < 0.1 || value.intervalMinutes > 1440
    || !Number.isSafeInteger(value.maxPerDocument) || value.maxPerDocument < 0 || value.maxPerDocument > 1000) {
    throw new BackupClientError('INVALID_SETTINGS', '本地备份间隔或每个 PDF 的保留数量无效。');
  }
  if (typeof value.directory !== 'string' || !value.directory || typeof value.rootId !== 'string' || !value.rootId || !Array.isArray(value.roots)) {
    throw new BackupClientError('INVALID_SETTINGS', '本地备份设置缺少目录信息，已停止本次调度。');
  }
  return { ...value, intervalMinutes: value.intervalMinutes, maxPerDocument: value.maxPerDocument };
}

/** getSnapshot receives a cancellation signal and the latest marked document/revision.
 * It must capture coherent PDF bytes without changing the document's dirty state.
 * The scheduler neither writes application state nor starts on module import.
 */
export function createBackupScheduler({ client, getSnapshot, onStatus = () => {} } = {}) {
  if (!client || typeof client.settings !== 'function' || typeof client.createSnapshot !== 'function'
    || typeof getSnapshot !== 'function' || typeof onStatus !== 'function') throw inputError('备份调度器缺少必要的接口。');
  let active = false, generation = 0, timer = null, controller = null, inFlight = null, configuring = null;
  let settings = null, pending = null, sequence = 0, lastSuccessful = null;

  function report(state, message, extra = {}) {
    // A UI callback must not break scheduling or cause another snapshot attempt.
    try { onStatus({ state, message, ...extra }); } catch { /* UI owns its errors. */ }
  }
  function current(epoch) { return active && generation === epoch; }
  function clearTimer() { if (timer !== null) clearTimeout(timer); timer = null; }
  function interval() {
    return Math.min(MAX_TIMER_DELAY, Math.max(1, Math.round((settings?.intervalMinutes || DEFAULT_INTERVAL_MINUTES) * 60000)));
  }
  function schedule(epoch) {
    if (!current(epoch)) return;
    clearTimer();
    if (settings?.maxPerDocument === 0) return;
    timer = setTimeout(() => { timer = null; void round(epoch); }, interval());
  }
  function invalidate() {
    generation++;clearTimer();controller?.abort();controller = null;
    return generation;
  }
  async function configure(epoch, supplied) {
    const localController = new AbortController();controller = localController;
    try {
      const value = supplied === undefined ? await client.settings({ signal: localController.signal }) : supplied;
      if (generation !== epoch) return null;
      settings = scheduleSettings(value);
      report(!active ? 'stopped' : settings.maxPerDocument === 0 ? 'disabled' : 'scheduled', !active
        ? '备份设置已更新，自动备份调度仍处于停止状态。' : settings.maxPerDocument === 0
          ? '自动备份已关闭，已有备份继续保留。' : '自动备份已设置，只备份有变化的文档。', { settings });
      schedule(epoch);
      return settings;
    } catch (error) {
      if (generation !== epoch) return null;
      settings = null;
      report('error', active ? '无法读取备份设置；将按默认 5 分钟间隔重试。' : '无法读取备份设置，自动调度保持停止。', { error });
      schedule(epoch);
      return null;
    } finally { if (controller === localController) controller = null; }
  }
  async function perform(epoch) {
    const localController = new AbortController();controller = localController;
    try {
      if (!settings) {
        const value = await client.settings({ signal: localController.signal });
        if (!current(epoch)) return;
        settings = scheduleSettings(value);
        if (settings.maxPerDocument !== 0) report('scheduled', '已重新读取本地备份设置。', { settings });
      }
      if (settings.maxPerDocument === 0) {
        report('disabled', '自动备份已关闭，已有备份继续保留。', { settings });
        return;
      }
      const change = pending;
      if (!change) return;
      report('running', '正在生成本地 PDF 备份…', { documentId: change.documentId, revision: change.revision });
      if (!current(epoch)) return;
      const snapshot = await getSnapshot({ signal: localController.signal, documentId: change.documentId, revision: change.revision });
      if (!current(epoch) || localController.signal.aborted) return;
      if (!snapshot) {
        report('scheduled', '当前文档暂不可备份，将在下个间隔重试。');
        return;
      }
      if (change.documentId && snapshot.documentId !== change.documentId) {
        report('scheduled', '等待当前文档的可用快照；未备份其他文档。');
        return;
      }
      if (snapshot.revision === undefined) throw inputError('PDF 快照缺少修改版本，已停止本次备份。');
      // Revision numbers can be reused after reopening a document. A new change
      // ticket still deserves a snapshot; server content hashes handle duplicates.
      if (lastSuccessful?.ticket === change.ticket && lastSuccessful.documentId === snapshot.documentId
        && Object.is(lastSuccessful.revision, snapshot.revision)) return;
      const result = await client.createSnapshot(snapshot, { signal: localController.signal });
      if (!current(epoch) || localController.signal.aborted) return;
      if (!result || (result.created !== true && !(result.created === false && ['unchanged', 'disabled'].includes(result.reason)))) {
        throw new BackupClientError('INVALID_RESPONSE', '本地备份服务没有确认备份结果。');
      }
      if (result.reason === 'disabled') {
        const value = await client.settings({ signal: localController.signal });
        if (!current(epoch)) return;
        settings = scheduleSettings(value);
        report(settings.maxPerDocument === 0 ? 'disabled' : 'scheduled', settings.maxPerDocument === 0
          ? '自动备份已由设置关闭，已有备份继续保留。' : '备份配置已变化，将在下个间隔重试。', { settings });
        return;
      }
      if (result.documentId !== snapshot.documentId) throw new BackupClientError('INVALID_RESPONSE', '本地备份服务返回的文档标识不一致。');
      lastSuccessful = { ticket: change.ticket, documentId: snapshot.documentId, revision: snapshot.revision };
      if (pending?.ticket === change.ticket && pending.documentId === change.documentId) pending = null;
      report(result.created ? 'saved' : 'unchanged', result.created ? '本地 PDF 备份已保存。' : '文档内容与已有备份相同，未新增副本。', {
        documentId: snapshot.documentId, revision: snapshot.revision, result,
      });
    } catch (error) {
      if (current(epoch)) report('error', '本地备份未完成，将在下个间隔重试；当前 PDF 的保存状态未改变。', { error });
    } finally { if (controller === localController) controller = null; }
  }
  async function round(epoch) {
    if (!current(epoch)) return;
    if (inFlight) { schedule(epoch);return; }
    const task = perform(epoch);inFlight = task;
    try { await task; }
    finally {
      if (inFlight === task) inFlight = null;
      if (current(epoch)) schedule(epoch);
    }
  }
  function start() {
    if (active) return configuring || Promise.resolve(settings);
    active = true;
    const epoch = invalidate();
    const task = configure(epoch);configuring = task;
    return task.finally(() => { if (configuring === task) configuring = null; });
  }
  function stop() {
    active = false;invalidate();
    report('stopped', '自动备份调度已停止，已有备份继续保留。');
  }
  function reconfigure(value) {
    const epoch = invalidate();
    const task = configure(epoch, value);configuring = task;
    return task.finally(() => { if (configuring === task) configuring = null; });
  }
  function markChanged({ documentId = null, revision } = {}) {
    if (documentId !== null && (typeof documentId !== 'string' || !documentId)) throw inputError('待备份文档的标识无效。');
    pending = { ticket: ++sequence, documentId, revision };
    return pending.ticket;
  }
  return Object.freeze({ start, stop, reconfigure, markChanged });
}
