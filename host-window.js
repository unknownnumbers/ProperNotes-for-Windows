/* A narrow top-frame WebView2 close protocol. No path, command or script is
 * accepted from messages; importing this module installs no listeners. */
const HEX64 = /^[a-f\d]{64}$/;
const REQUEST_ID = /^(?:[a-f\d]{32}|[a-f\d]{64})$/;
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const hex64 = value => typeof value === 'string' && HEX64.test(value);

export function connectNativeWindow({ requestClose, onError = () => {} } = {}) {
  const webview = window.chrome?.webview;
  if (window !== window.top || !webview || typeof webview.addEventListener !== 'function' || typeof webview.postMessage !== 'function') return null;
  if (location.protocol !== 'http:' || location.hostname !== '127.0.0.1' || !/^\d+$/.test(location.port)
    || !['/', '/index.html'].includes(location.pathname) || typeof requestClose !== 'function') return null;
  const origin = location.origin;
  let binding = null, initializing = null, pending = false, disposed = false;
  const completed = new Set();
  function report(error) { try { onError(error instanceof Error ? error : new Error('独立窗口通信失败。')); } catch { /* UI errors must not authorize a close. */ } }
  function post(value) {
    if (disposed) return false;
    try { webview.postMessage(value);return true; } catch (error) { report(error);return false; }
  }
  function ready() {
    if (binding) post({ type: 'propernotes-app-ready', protocol: 1, ...binding });
  }
  function remember(id) { if (completed.size >= 64) completed.delete(completed.values().next().value);completed.add(id); }
  function result(id, allow, message = '', expiresAt = null) {
    if (!binding) return;
    post({ type: 'propernotes-close-result', protocol: 1, nonce: binding.nonce, requestId: id, allow,
      ...(allow ? { expiresAt } : {}),
      ...(message ? { message: String(message).slice(0, 2000) } : {}) });
  }
  async function initialize(value) {
    if (!hex64(value.nonce) || !hex64(value.instance) || !hex64(value.revision)) return;
    if (binding) {
      if (binding.nonce === value.nonce && binding.instance === value.instance && binding.revision === value.revision) ready();
      return;
    }
    if (initializing) return;
    const candidate = { nonce: value.nonce, instance: value.instance, revision: value.revision };
    initializing = candidate;
    try {
      const response = await fetch('/build-info.json', { credentials: 'same-origin', cache: 'no-store', redirect: 'error' });
      if (!response.ok || new URL(response.url).origin !== origin) throw new Error('独立窗口无法核对当前应用修订。');
      const text = await response.text();
      if (text.length > 65536) throw new Error('应用修订信息超过可核对范围。');
      const build = JSON.parse(text);
      if (!object(build) || build.app !== 'inkpdf-local' || build.instance !== candidate.instance || build.revision !== candidate.revision) throw new Error('窗口与当前应用修订不一致，请关闭后重新启动。');
      if (disposed || initializing !== candidate || location.origin !== origin || window !== window.top) return;
      binding = candidate;ready();
    } catch (error) { report(error); }
    finally { if (initializing === candidate) initializing = null; }
  }
  async function receive(event) {
    if (disposed || window !== window.top || location.origin !== origin) return;
    const value = event.data;
    if (!object(value) || value.protocol !== 1 || typeof value.type !== 'string') return;
    if (value.type === 'propernotes-host-init') { await initialize(value);return; }
    if (value.type !== 'propernotes-close-request' || !binding || value.nonce !== binding.nonce || typeof value.requestId !== 'string' || !REQUEST_ID.test(value.requestId)) return;
    if (pending || completed.has(value.requestId)) { result(value.requestId, false, '已有关闭请求正在处理，请等待后重试。');return; }
    pending = true;remember(value.requestId);
    try {
      const decision = await requestClose();
      if (!object(decision) || typeof decision.allow !== 'boolean') throw new Error('当前页面未能确认安全关闭。');
      if (decision.allow && (!Number.isSafeInteger(decision.expiresAt) || decision.expiresAt <= Date.now() || decision.expiresAt > Date.now() + 9000)) throw new Error('关闭确认已过期，窗口仍保留，请重试。');
      result(value.requestId, decision.allow, decision.message || '', decision.expiresAt);
    } catch (error) {
      result(value.requestId, false, error?.message || '关闭检查未完成，窗口仍保留。');report(error);
    } finally { pending = false; }
  }
  const listener = event => { receive(event).catch(report); };
  webview.addEventListener('message', listener);
  post({ type: 'propernotes-app-hello', protocol: 1 });
  return Object.freeze({ dispose() { disposed = true;webview.removeEventListener('message', listener);binding = null;initializing = null; } });
}
