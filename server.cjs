const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { createBackupStore, BackupStoreError, MAX_PDF_BYTES } = require('./backup-store.cjs');
const { createNativeFiles, NativeFileError, MAX_FILE_BYTES } = require('./native-files.cjs');
const { WorkspaceError } = require('./workspace-store.cjs');

const base = __dirname;
const realBase = fs.realpathSync(base);
// Capture the prepared revision once for this process. Reading the public marker
// on each health check would incorrectly bless an old process after a new build.
const loadedBuild = (() => {
  try {
    const file = path.join(base, 'build-info.json'), stat = fs.lstatSync(file);
    if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 64 * 1024) return { instance: null, revision: null };
    const value = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (value?.app !== 'inkpdf-local') return { instance: null, revision: null };
    return {
      instance: typeof value.instance === 'string' && /^[a-f\d]{64}$/.test(value.instance) ? value.instance : null,
      revision: typeof value.revision === 'string' && /^[a-f\d]{64}$/.test(value.revision) ? value.revision : null,
    };
  } catch { return { instance: null, revision: null }; }
})();
const flag = process.argv.indexOf('--port');
const port = flag >= 0 ? Number(process.argv[flag + 1]) : 9150;
if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('Invalid port');
// Only a launcher that keeps a redirected stdin pipe open opts into ownership.
// EOF is a private lifetime capability; there is no HTTP shutdown endpoint and
// an existing/reused standalone service never acquires another window's owner.
const ownerStdin = process.argv.includes('--owner-stdin');
const ownerIdFlag = process.argv.indexOf('--owner-id');
const ownerId = ownerStdin && ownerIdFlag >= 0 ? process.argv[ownerIdFlag + 1] : null;
if ((ownerStdin && (typeof ownerId !== 'string' || !/^[a-f\d]{64}$/.test(ownerId)))
  || (!ownerStdin && ownerIdFlag >= 0) || (ownerIdFlag >= 0 && ownerIdFlag !== process.argv.lastIndexOf('--owner-id'))) throw new Error('Invalid owned service identity');
const store = createBackupStore({ baseDir: base });
const nativeFiles = createNativeFiles({ baseDir: base });
const token = crypto.randomBytes(32).toString('base64url');
const JSON_LIMIT = 64 * 1024;
const SNAPSHOT_LIMIT = MAX_PDF_BYTES;
const METADATA_LIMIT = 8 * 1024;
const acceptedHosts = new Set(['127.0.0.1:' + port, 'localhost:' + port]);
const publicFiles = new Set(['/index.html', '/styles.css', '/app.js', '/host-window.js', '/ink-engine.js', '/pdf-storage.js', '/annotation-geometry.js', '/text-appearance.js', '/document-view.js', '/text-selection.js', '/pdf-bookmarks.js', '/preferences.js', '/favourite-tools.js', '/pdf-text-search.js', '/backup-client.js', '/file-api.js', '/browser-workspace.js', '/input-preferences.js', '/keyboard-shortcuts.js', '/build-info.json']);
const vendorTypes = new Set(['.js', '.mjs', '.wasm', '.bcmap', '.pfb', '.ttf', '.otf', '.icc']);
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8', '.pdf': 'application/pdf', '.wasm': 'application/wasm', '.bcmap': 'application/octet-stream', '.pfb': 'application/octet-stream', '.ttf': 'font/ttf', '.otf': 'font/otf', '.icc': 'application/octet-stream' };

class RequestError extends Error {
  constructor(code, message, httpStatus = 400) { super(message);this.code = code;this.httpStatus = httpStatus; }
}
function fail(code, message, status) { throw new RequestError(code, message, status); }
function commonHeaders(res) {
  res.setHeader('Cache-Control', 'no-store');res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');res.setHeader('Cross-Origin-Resource-Policy', 'same-origin');
}
function sendJSON(req, res, status, value) {
  if (res.destroyed || res.writableEnded) return;
  const body = Buffer.from(JSON.stringify(value), 'utf8');
  commonHeaders(res);res.statusCode = status;
  res.setHeader('Content-Type', types['.json']);res.setHeader('Content-Length', body.length);
  res.end(req.method === 'HEAD' ? undefined : body);
}
function sendError(req, res, error, isAPI) {
  if (res.destroyed || res.writableEnded) return;
  if (res.headersSent) { res.destroy();return; }
  const known = error instanceof RequestError || (typeof BackupStoreError === 'function' && error instanceof BackupStoreError)
    || (typeof NativeFileError === 'function' && error instanceof NativeFileError) || error instanceof WorkspaceError;
  const status = known && Number.isInteger(error.httpStatus) && error.httpStatus >= 400 && error.httpStatus <= 599 ? error.httpStatus : 500;
  const code = known && typeof error.code === 'string' ? error.code : 'INTERNAL_ERROR';
  const message = known && (isAPI || error instanceof RequestError) ? error.message : '无法处理本地请求。';
  if (status === 413) res.setHeader('Connection', 'close');
  sendJSON(req, res, status, { error: { code, message } });
}
function method(req, res, allowed) {
  if (!allowed.includes(req.method)) { res.setHeader('Allow', allowed.join(', '));fail('METHOD_NOT_ALLOWED', '此接口不支持该请求方法。', 405); }
}
function checkRequestOrigin(req) {
  const host = typeof req.headers.host === 'string' ? req.headers.host.toLowerCase() : '';
  const hostCount = req.rawHeaders.filter((value, index) => index % 2 === 0 && value.toLowerCase() === 'host').length;
  if (hostCount !== 1 || !acceptedHosts.has(host)) fail('INVALID_HOST', '仅接受当前本机服务地址。', 403);
  const origin = 'http://' + host;
  if (req.headers.origin !== undefined && req.headers.origin !== origin) fail('INVALID_ORIGIN', '已拒绝跨来源请求。', 403);
  const fetchSite = req.headers['sec-fetch-site'];
  if (fetchSite !== undefined && !['same-origin', 'none'].includes(fetchSite)) fail('INVALID_ORIGIN', '已拒绝跨来源请求。', 403);
  return origin;
}
function requireToken(req) {
  const value = req.headers['x-propernotes-token'];
  if (typeof value !== 'string') fail('INVALID_TOKEN', '本地会话已失效，请刷新后重试。', 403);
  const supplied = Buffer.from(value, 'utf8'), expected = Buffer.from(token, 'utf8');
  if (supplied.length !== expected.length || !crypto.timingSafeEqual(supplied, expected)) fail('INVALID_TOKEN', '本地会话已失效，请刷新后重试。', 403);
}
function contentType(req, expected) {
  const actual = String(req.headers['content-type'] || '').split(';', 1)[0].trim().toLowerCase();
  if (actual !== expected) fail('UNSUPPORTED_MEDIA_TYPE', '请求内容类型不正确。', 415);
  if (req.headers['content-encoding'] && req.headers['content-encoding'] !== 'identity') fail('UNSUPPORTED_MEDIA_TYPE', '不接受压缩的请求内容。', 415);
}
function readBody(req, limit) {
  const declared = req.headers['content-length'];
  if (declared !== undefined && (typeof declared !== 'string' || !/^\d+$/.test(declared))) fail('INVALID_BODY_SIZE', '请求内容长度无效。');
  if (declared !== undefined && Number(declared) > limit) fail('BODY_TOO_LARGE', '请求内容超过当前处理范围。', 413);
  return new Promise((resolve, reject) => {
    const chunks = [];let length = 0, finished = false;
    const clean = () => { req.removeListener('data', onData);req.removeListener('end', onEnd);req.removeListener('error', onError);req.removeListener('aborted', onAborted); };
    const finish = (error) => {
      if (finished) return;finished = true;clean();
      if (error) { chunks.length = 0;req.resume();reject(error); }
      else {
        try { resolve(Buffer.concat(chunks, length)); }
        catch { reject(new RequestError('BODY_READ_FAILED', '无法在当前内存条件下读取请求内容。', 500)); }
      }
    };
    const onData = chunk => { length += chunk.length;if (length > limit) finish(new RequestError('BODY_TOO_LARGE', '请求内容超过当前处理范围。', 413));else chunks.push(chunk); };
    const onEnd = () => finish();
    const onError = () => finish(new RequestError('REQUEST_ABORTED', '读取请求内容失败。', 400));
    const onAborted = () => finish(new RequestError('REQUEST_ABORTED', '请求已中断。', 400));
    req.on('data', onData);req.on('end', onEnd);req.on('error', onError);req.on('aborted', onAborted);
    if (req.aborted) onAborted();
  });
}
function plainObject(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
async function readJSON(req) {
  contentType(req, 'application/json');const body = await readBody(req, JSON_LIMIT);
  try {
    const value = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(body));
    if (!plainObject(value)) fail('INVALID_JSON', '请求内容必须是 JSON 对象。');
    return value;
  } catch (error) { if (error instanceof RequestError) throw error;fail('INVALID_JSON', '请求 JSON 无法读取。'); }
}
function metadataHeader(req) {
  const header = req.headers['x-propernotes-metadata'];
  if (typeof header !== 'string' || Buffer.byteLength(header, 'utf8') > METADATA_LIMIT) fail('INVALID_METADATA', '文档信息缺失或超过 8 KB。');
  let value;
  try { value = JSON.parse(decodeURIComponent(header)); } catch { fail('INVALID_METADATA', '文档信息无法读取。'); }
  if (!plainObject(value)) fail('INVALID_METADATA', '文档信息无效。');
  return value;
}
function snapshotMetadata(req) {
  const value = metadataHeader(req);
  return { documentId: value.documentId, filename: value.filename, sourcePath: value.sourcePath, revision: value.revision };
}
function queryValue(url, key) {
  const values = url.searchParams.getAll(key);if (values.length !== 1 || !values[0]) fail('INVALID_QUERY', '缺少有效的备份标识。');return values[0];
}
async function api(req, res, url, pathname) {
  if (pathname === '/api/session') { method(req, res, ['GET']);sendJSON(req, res, 200, { token });return; }
  if (pathname === '/api/workspace/get' || pathname === '/api/workspace/update') {
    method(req, res, ['POST']);requireToken(req);const value = await readJSON(req);
    if (pathname === '/api/workspace/get') {
      if (Object.keys(value).length) fail('INVALID_WORKSPACE', '读取会话记录不接受路径或其他参数。');
      sendJSON(req, res, 200, await nativeFiles.getWorkspace());return;
    }
    sendJSON(req, res, 200, await nativeFiles.updateWorkspace(value));return;
  }
  if (pathname === '/api/files/open' || pathname === '/api/files/reopen') {
    method(req, res, ['POST']);requireToken(req);
    const request = await readJSON(req);
    if (pathname === '/api/files/open' && Object.keys(request).length) fail('INVALID_FILE_METADATA', '打开文件不接受指定路径，请通过原生对话框选择。');
    if (pathname === '/api/files/reopen' && (Object.keys(request).length !== 1 || typeof request.id !== 'string')) fail('INVALID_FILE_METADATA', '重新打开只接受已登记文档的标识。');
    const selected = pathname === '/api/files/open' ? await nativeFiles.open() : await nativeFiles.reopen(request.id);
    if (res.destroyed || res.writableEnded) return;
    if (selected === null) { commonHeaders(res);res.statusCode = 204;res.end();return; }
    const metadata = encodeURIComponent(JSON.stringify(selected.metadata));
    if (Buffer.byteLength(metadata, 'utf8') > METADATA_LIMIT) fail('FILE_METADATA_TOO_LARGE', '文件路径过长，无法通过当前本地文件接口返回。', 413);
    commonHeaders(res);res.setHeader('Content-Type', 'application/pdf');res.setHeader('Content-Length', selected.bytes.length);
    res.setHeader('X-ProperNotes-File', metadata);res.end(selected.bytes);return;
  }
  if (pathname === '/api/files/save') {
    method(req, res, ['POST']);requireToken(req);contentType(req, 'application/pdf');
    const value = metadataHeader(req), bytes = await readBody(req, MAX_FILE_BYTES);
    const saved = await nativeFiles.save({ token: value.token, saveAs: value.saveAs, filename: value.filename, excludeTokens: value.excludeTokens, excludeDocumentIds: value.excludeDocumentIds, bytes });
    if (saved === null) { commonHeaders(res);res.statusCode = 204;res.end();return; }
    sendJSON(req, res, 200, saved);return;
  }
  if (pathname === '/api/backups/settings') {
    method(req, res, ['GET', 'POST']);
    if (req.method === 'GET') { sendJSON(req, res, 200, await store.getSettings());return; }
    requireToken(req);sendJSON(req, res, 200, await store.updateSettings(await readJSON(req)));return;
  }
  if (pathname === '/api/backups/list') { method(req, res, ['GET']);sendJSON(req, res, 200, await store.list());return; }
  if (pathname === '/api/backups/cleanup') {
    method(req, res, ['POST']);requireToken(req);sendJSON(req, res, 200, await store.cleanup(await readJSON(req)));return;
  }
  if (pathname === '/api/backups/snapshots') {
    method(req, res, ['POST']);requireToken(req);contentType(req, 'application/pdf');
    const metadata = snapshotMetadata(req), bytes = await readBody(req, SNAPSHOT_LIMIT);
    const result = await store.createSnapshot({ ...metadata, bytes });sendJSON(req, res, result.created ? 201 : 200, result);return;
  }
  if (pathname === '/api/backups/snapshot') {
    method(req, res, ['GET']);
    const snapshot = await store.readSnapshot({ rootId: queryValue(url, 'rootId'), documentId: queryValue(url, 'documentId'), snapshotId: queryValue(url, 'snapshotId') });
    const body = Buffer.from(snapshot.bytes);if (body.length > SNAPSHOT_LIMIT) fail('BODY_TOO_LARGE', '备份超过当前可恢复的大小。', 413);
    const filename = Buffer.from(path.basename(typeof snapshot.metadata?.filename === 'string' ? snapshot.metadata.filename : 'backup.pdf').slice(0, 240), 'utf8').toString('utf8');
    const encodedName = encodeURIComponent(filename).replace(/['()*]/g, character => '%' + character.charCodeAt(0).toString(16).toUpperCase());
    commonHeaders(res);res.setHeader('Content-Type', 'application/pdf');res.setHeader('Content-Length', body.length);
    res.setHeader('Content-Disposition', 'attachment; filename="ProperNotes-backup.pdf"; filename*=UTF-8\'\'' + encodedName);res.end(body);return;
  }
  fail('API_NOT_FOUND', '没有此本地接口。', 404);
}
function within(directory, candidate) {
  const relative = path.relative(directory, candidate);
  return relative === '' || (relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative));
}
async function checkPublicAsset(file) {
  try { if (await store.isPrivatePath(file)) fail('PRIVATE_PATH', '备份与设置仅可通过本地管理接口访问。', 403); }
  catch (error) {
    if (error instanceof RequestError) throw error;
    // Only fixed application files / permitted vendor asset types reach here.
    // Backup PDFs, manifests, configuration, server/dev scripts and temp files are
    // excluded before this check, so a broken backup config cannot expose them.
    // Let the GUI load and display the API's actual settings/configuration error.
  }
}
async function staticFile(req, res, pathname) {
  method(req, res, ['GET', 'HEAD']);
  if (pathname === '/') pathname = '/index.html';
  if (pathname.includes('\0') || pathname.includes('\\') || pathname.includes(':')) fail('NOT_FOUND', '没有此文件。', 404);
  const vendor = pathname.startsWith('/vendor/') && vendorTypes.has(path.extname(pathname).toLowerCase());
  if (!publicFiles.has(pathname) && !vendor) fail('NOT_FOUND', '没有此文件。', 404);
  const file = path.resolve(base, '.' + pathname), vendorBase = path.join(base, 'vendor');
  if (!within(base, file) || (vendor && !within(vendorBase, file)) || path.relative(base, file).split(path.sep).some(part => part.startsWith('.'))) fail('NOT_FOUND', '没有此文件。', 404);
  await checkPublicAsset(file);
  let current = base, info;
  try {
    const components = path.relative(base, file).split(path.sep).filter(Boolean);
    for (let index = 0; index < components.length; index++) {
      current = path.join(current, components[index]);info = await fs.promises.lstat(current);
      if (info.isSymbolicLink()) fail('FORBIDDEN_PATH', '不可通过链接访问本地文件。', 403);
      if (index + 1 < components.length && !info.isDirectory()) fail('NOT_FOUND', '没有此文件。', 404);
    }
    if (!info?.isFile()) fail('NOT_FOUND', '没有此文件。', 404);
    const real = await fs.promises.realpath(file);
    if (!within(realBase, real) || (vendor && !within(path.join(realBase, 'vendor'), real))) fail('FORBIDDEN_PATH', '不可访问此文件。', 403);
    await checkPublicAsset(real);
    const handle = await fs.promises.open(real, 'r');
    try {
      const actual = await handle.stat(), checkedReal = await fs.promises.realpath(file);
      if (!actual.isFile() || checkedReal !== real || actual.dev !== info.dev || actual.ino !== info.ino) fail('FORBIDDEN_PATH', '文件在读取前已改变，请重试。', 403);
      commonHeaders(res);res.setHeader('Content-Type', types[path.extname(file).toLowerCase()] || 'application/octet-stream');res.setHeader('Content-Length', actual.size);
      if (req.method === 'HEAD') { await handle.close();res.end();return; }
      const stream = handle.createReadStream();stream.on('error', () => res.destroy());res.on('close', () => stream.destroy());stream.pipe(res);
    } catch (error) { await handle.close().catch(() => {});throw error; }
  } catch (error) { if (error?.code === 'ENOENT' || error?.code === 'ENOTDIR') fail('NOT_FOUND', '没有此文件。', 404);throw error; }
}

let activeOperations = 0, draining = false, drainTimer = null;
let listenerPending = true, listenerClosing = false, listenerClosed = false, drainFinished = false;
function finishDrain() {
  if (!draining || drainFinished || !listenerClosed || activeOperations) return;
  drainFinished = true;
  if (drainTimer) { clearInterval(drainTimer);drainTimer = null; }
  if (ownerStdin && !process.stdin.destroyed) process.stdin.destroy();
  // Exit naturally after Node releases its remaining I/O handles. Never force
  // process.exit(): a response may have aborted while a file transaction ran.
  console.log('ProperNotes local service finished its owned-window shutdown.');
}
function closeListener() {
  if (listenerPending || listenerClosing) return;
  if (!server.listening) { listenerClosed = true;finishDrain();return; }
  listenerClosing = true;
  // Bundled Node 24 also closes idle keep-alive sockets here. Active requests
  // may finish normally; closeAllConnections would truncate uploads/writes.
  server.close(error => {
    listenerClosed = true;
    if (error && error.code !== 'ERR_SERVER_NOT_RUNNING') { process.exitCode = 1;console.error('The owned local listener could not finish closing.'); }
    finishDrain();
  });
}
function requestDrain() {
  if (draining) return;
  draining = true;
  // A Promise alone need not keep Node alive. Hold the event loop until both
  // the HTTP listener and every awaited API operation have actually finished.
  // No deadline kills a native dialog, PDF write, backup or workspace commit.
  drainTimer = setInterval(finishDrain, 250);
  closeListener();
}
const server = http.createServer({ maxHeaderSize: 16 * 1024 }, (req, res) => {
  req.on('error', () => {});res.on('error', () => {});let isAPI = false;
  if (draining) {
    req.resume();res.setHeader('Connection', 'close');
    sendJSON(req, res, 503, { error: { code: 'SERVICE_CLOSING', message: '本地窗口正在关闭，请等待当前文件操作结束后重新启动。' } });return;
  }
  activeOperations++;
  const operation = Promise.resolve().then(async () => {
    const origin = checkRequestOrigin(req);let url, pathname;
    try { url = new URL(req.url, origin);pathname = decodeURIComponent(url.pathname); } catch { fail('INVALID_URL', '本地请求地址无效。'); }
    if (url.origin !== origin) fail('INVALID_URL', '本地请求地址无效。');
    isAPI = pathname === '/api' || pathname.startsWith('/api/');
    if (isAPI) { await api(req, res, url, pathname);return; }
    if (pathname === '/__inkpdf_health') { method(req, res, ['GET', 'HEAD']);sendJSON(req, res, 200, { app: 'inkpdf-local', product: 'ProperNotes for Windows', version: '0.1.0', apiVersion: 2, instance: loadedBuild.instance, revision: loadedBuild.revision, lifetime: ownerStdin ? 'owner-stdin' : 'standalone', ownerId });return; }
    await staticFile(req, res, pathname);
  }).catch(error => { req.resume();sendError(req, res, error, isAPI); });
  // Socket close/abort is not an operation boundary: nativeFiles/store may
  // still be publishing or cleaning a transaction after the client vanished.
  const finished = () => { activeOperations--;finishDrain(); };
  operation.then(finished, () => { res.destroy();console.error('A local response could not be completed.');finished(); });
});
server.on('listening', () => {
  listenerPending = false;
  if (draining) closeListener();else console.log('ProperNotes for Windows http://127.0.0.1:' + port);
});
server.on('error', error => {
  listenerPending = false;console.error(error.message);process.exitCode = 1;
  if (ownerStdin) { requestDrain();closeListener(); }
});
if (ownerStdin) {
  process.stdin.on('end', requestDrain);process.stdin.on('close', requestDrain);process.stdin.on('error', requestDrain);
  process.stdin.on('data', () => {});process.stdin.resume();
}
server.listen(port, '127.0.0.1');
