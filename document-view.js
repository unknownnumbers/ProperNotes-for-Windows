/* Multi-page local PDF view. Importing this module creates no DOM or PDF task. */
const LAYOUTS = ['single-continuous', 'double-continuous', 'single-page', 'double-page'];
const PAGE_GAP = 24;
const MAX_CACHED_PAGES = 8;
const MAX_CANVAS_PIXELS = 48000000;
const MAX_PAGE_PIXELS = 12000000;
const MAX_EXTENT = 16000000;
const SURFACES = ['page-frame', 'paper', 'document', 'ink', 'pdf-text'];

function cancelled() { const error = new Error('页面显示请求已失效。');error.name = 'AbortError';return error; }
function finite(value) { return typeof value === 'number' && Number.isFinite(value); }
function clamp(value, minimum, maximum) { return Math.max(minimum, Math.min(maximum, value)); }
function ignored(error) { return ['AbortError', 'RenderingCancelledException'].includes(error?.name); }

export function createDocumentView(options) {
  const {
    viewport, tray, templateFrame, pdf, metrics, inkPages, histories, pdfjsLib,
    createEngine, onActivate = () => {}, onReady = () => {}, onError = () => {},
    onStatus = () => {}, canActivate = () => true, getTool = () => 'pen',
    getStyle = () => ({}), renderDensity = () => window.devicePixelRatio || 1,
  } = options || {};
  if (!viewport || !tray || !templateFrame || !pdf || !pdfjsLib || typeof createEngine !== 'function'
    || !Array.isArray(metrics) || !metrics.length || metrics.length > 10000
    || !Array.isArray(inkPages) || inkPages.length !== metrics.length || !(histories instanceof Map)) {
    throw new TypeError('连续阅读视图缺少有效的文档、页面或批注数据。');
  }
  for (const metric of metrics) if (!metric || !finite(metric.width) || !finite(metric.height) || metric.width <= 0 || metric.height <= 0 || metric.width > 100000 || metric.height > 100000) throw new RangeError('页面尺寸超出当前阅读范围。');

  let layout = LAYOUTS.includes(options.layout) ? options.layout : 'single-continuous';
  let zoom = finite(options.zoom) ? clamp(options.zoom, .1, 4) : 1;
  let annotationsVisible = options.annotationsVisible !== false, textSelectable = options.textSelectable === true;
  let activeIndex = 0, mounted = false, destroyed = false, epoch = 0, showSerial = 0, clock = 0;
  let rows = [], pageRows = [], maxWidth = 0, frameRequest = 0, running = 0, inflightPixels = 0, activeShow = null;
  let queue = [], desiredIndices = new Set();
  const records = new Map(), slots = new Map(), container = document.createElement('div');
  const pristine = templateFrame.cloneNode(true);
  container.className = 'pn-document-view';container.setAttribute('aria-label', 'PDF 页面');
  Object.assign(container.style, { position: 'relative', flexShrink: '0' });

  function doubles() { return layout.startsWith('double-'); }
  function continuous() { return layout.endsWith('-continuous'); }
  function indexOf(value) { return clamp(Math.trunc(Number(value)) || 0, 0, metrics.length - 1); }
  function groupFor(index) { const first = doubles() ? Math.floor(index / 2) * 2 : index;return doubles() && first + 1 < metrics.length ? [first, first + 1] : [first]; }
  function notify(callback, ...args) { try { callback(...args); } catch (error) { try { onError(error, activeIndex, { callback: true }); } catch { /* UI error reporting must not recursively fail. */ } } }

  function createSlot(index) {
    if (slots.has(index)) return slots.get(index);
    const slot = document.createElement('div');slot.className = 'pn-page-slot';slot.dataset.documentPage = String(index);
    Object.assign(slot.style, { position: 'absolute', flexShrink: '0', background: '#fff', boxShadow: '0 4px 16px #0000001a' });
    const placeholder = document.createElement('button');placeholder.type = 'button';placeholder.className = 'pn-page-placeholder';
    placeholder.textContent = '第 ' + (index + 1) + ' 页 · 点击显示';
    Object.assign(placeholder.style, { position: 'absolute', zIndex: '2', inset: '0', width: '100%', height: '100%', padding: '12px', fontSize: '12px', color: '#686a70', background: '#fff' });
    placeholder.addEventListener('click', () => { if (!canActivate()) return;show(index, { preserveView: true, interactive: true }).catch(error => { if (!ignored(error)) notify(onError, error, index); }); });
    slot.append(placeholder);slot._placeholder = placeholder;
    slot.addEventListener('pointerdown', event => {
      const record = records.get(index);
      if (record?.engine && paintCurrent(record)) {
        if (canActivate()) {
          try { activate(record); } catch (error) { event.preventDefault();event.stopPropagation();notify(onError, error, index); }
        } else { event.preventDefault();event.stopPropagation(); }
        return;
      }
      // A missing canvas has no writable surface. Explicitly ask the user to
      // wait, rather than treating this pointer as the start of a later stroke.
      event.preventDefault();event.stopPropagation();
      if (!canActivate()) return;
      notify(onStatus, '正在显示第 ' + (index + 1) + ' 页，请稍候再落笔。');
      show(index, { preserveView: true, interactive: true }).catch(error => { if (!ignored(error)) notify(onError, error, index); });
    }, true);
    slots.set(index, slot);return slot;
  }

  function layoutPlan(nextLayout = layout, nextZoom = zoom, focus = activeIndex) {
    const isDouble = nextLayout.startsWith('double-'), isContinuous = nextLayout.endsWith('-continuous');
    const groups = [];
    if (isContinuous) for (let index = 0; index < metrics.length; index += isDouble ? 2 : 1) groups.push(isDouble && index + 1 < metrics.length ? [index, index + 1] : [index]);
    else { const first = isDouble ? Math.floor(focus / 2) * 2 : focus;groups.push(isDouble && first + 1 < metrics.length ? [first, first + 1] : [first]); }
    let y = 0, width = 0;const plan = [];
    for (const indices of groups) {
      const rowWidth = indices.reduce((sum, index) => sum + metrics[index].width * nextZoom, 0) + PAGE_GAP * (indices.length - 1);
      const rowHeight = Math.max(...indices.map(index => metrics[index].height * nextZoom));
      if (!Number.isFinite(rowWidth) || rowWidth > MAX_EXTENT || y + rowHeight > MAX_EXTENT) throw new RangeError('此倍率下的连续页面范围过大，请降低倍率或改用非连续阅读。');
      plan.push({ indices, y, width: rowWidth, height: rowHeight });y += rowHeight + PAGE_GAP;width = Math.max(width, rowWidth);
    }
    return { rows: plan, width, height: Math.max(0, y - PAGE_GAP) };
  }

  function applyLayout(plan, { reuseNodes = false } = {}) {
    const previousRows = rows;
    const reuse = reuseNodes && previousRows.length === plan.rows.length && previousRows.every((row, index) => row.element && row.indices.length === plan.rows[index].indices.length && row.indices.every((page, column) => page === plan.rows[index].indices[column]));
    rows = plan.rows;maxWidth = plan.width;pageRows = [];
    if (!reuse) container.replaceChildren();container.style.width = plan.width + 'px';container.style.height = plan.height + 'px';
    for (const [rowIndex, row] of rows.entries()) {
      const element = reuse ? previousRows[rowIndex].element : document.createElement('div');element.className = 'pn-page-row';row.element = element;
      Object.assign(element.style, { position: 'absolute', top: row.y + 'px', left: (plan.width - row.width) / 2 + 'px', width: row.width + 'px', height: row.height + 'px' });
      let left = 0;
      for (const index of row.indices) {
        pageRows[index] = rowIndex;const slot = createSlot(index), metric = metrics[index];
        Object.assign(slot.style, { width: metric.width * zoom + 'px', height: metric.height * zoom + 'px', left: left + 'px', top: '0' });
        left += metric.width * zoom + PAGE_GAP;if (!reuse) element.append(slot);
        const record = records.get(index);if (record?.els) resizeRecord(record);
      }
      if (!reuse) container.append(element);
    }
  }

  function resizeRecord(record) {
    const metric = metrics[record.index];
    Object.assign(record.els['page-frame'].style, { width: metric.width * zoom + 'px', height: metric.height * zoom + 'px', position: 'relative', zIndex: '1' });
    Object.assign(record.els.paper.style, { width: metric.width + 'px', height: metric.height + 'px', transform: 'scale(' + zoom + ')', transformOrigin: 'top left' });
    for (const name of ['document', 'ink']) Object.assign(record.els[name].style, { width: metric.width + 'px', height: metric.height + 'px' });
  }

  function makeRecord(index) {
    const frame = pristine.cloneNode(true);frame.hidden = false;frame.removeAttribute('id');
    frame.querySelectorAll('[id]').forEach(element => element.removeAttribute('id'));
    const els = { 'page-frame': frame };
    for (const name of SURFACES.slice(1)) { els[name] = frame.querySelector('[data-ui="' + name + '"]');if (!els[name]) throw new Error('页面模板缺少 ' + name + '。'); }
    els.document.replaceChildren();els['pdf-text'].replaceChildren();els.ink.replaceChildren();
    els.ink.setAttribute('aria-label', '第 ' + (index + 1) + ' 页批注');
    const record = { index, els, engine: null, initializing: false, loading: false, ready: false, canvas: null, textNotice: '', task: null, textTask: null, promise: null, job: null, pins: 0, paintEpoch: -1, paintAnnotations: null, density: 0, lastUsed: ++clock, error: null };
    resizeRecord(record);records.set(index, record);return record;
  }

  function paintCurrent(record) { return record.ready && record.paintEpoch === epoch && record.paintAnnotations === annotationsVisible; }
  function applyVisibility(record) {
    const readable = paintCurrent(record);
    record.els.document.style.visibility = readable ? 'visible' : 'hidden';
    record.els.ink.style.visibility = readable && annotationsVisible ? 'visible' : 'hidden';
    record.els.ink.style.pointerEvents = readable && annotationsVisible && !textSelectable ? '' : 'none';
    record.els['pdf-text'].hidden = !textSelectable || !readable;
    const slot = slots.get(record.index);if (slot) slot._placeholder.hidden = readable;
  }
  function syncTools() {
    const tool = annotationsVisible ? getTool() : 'pan', style = getStyle();
    for (const record of records.values()) {
      if (record.engine) { if (record.appliedTool !== tool) { record.engine.setTool(tool);record.appliedTool = tool; }record.engine.setStyle(style); }
      applyVisibility(record);
    }
  }
  function syncRecord(record, finish = false) {
    if (!record.engine || record.initializing) return;
    if (finish) record.engine.finishGesture();
    inkPages[record.index] = record.engine.getStrokes();histories.set(record.index, record.engine.exportHistory());
  }
  function finishAll() { for (const record of records.values()) syncRecord(record, true); }

  function activate(record, { force = false } = {}) {
    if (!record?.engine || !paintCurrent(record)) return false;
    if (!force && record.index !== activeIndex && !canActivate()) return false;
    const previous = records.get(activeIndex), oldIndex = activeIndex;
    if (previous && previous !== record) syncRecord(previous, true);
    const tool = annotationsVisible ? getTool() : 'pan';
    if (record.appliedTool !== tool) { record.engine.setTool(tool);record.appliedTool = tool; }
    record.engine.setStyle(getStyle());activeIndex = record.index;record.lastUsed = ++clock;
    if (previous && previous !== record) previous.engine?.clearSelection();
    if (mounted && (force || oldIndex !== record.index)) {
      try { onActivate(record, record.index); }
      catch (error) { activeIndex = oldIndex;throw error; }
    }
    for (const cached of records.values()) cached.els['page-frame'].classList.toggle('is-active-page', cached.index === activeIndex);
    return true;
  }

  function residentPixels() { let total = 0;for (const record of records.values()) if (record.canvas) total += record.canvas.width * record.canvas.height;return total; }
  function hasTextSelection(record) {
    if (!textSelectable) return false;const selection = window.getSelection();if (!selection || selection.isCollapsed || selection.rangeCount !== 1) return false;
    const range = selection.getRangeAt(0), layer = record.els['pdf-text'];return layer.contains(range.startContainer) && layer.contains(range.endContainer);
  }
  function evict(record) {
    if (!record || record.index === activeIndex || record.job || record.pins || record.engine?.hasActiveGesture() || hasTextSelection(record)) return false;
    syncRecord(record);record.task?.cancel();record.textTask?.cancel();record.engine?.destroy();
    if (record.canvas) { record.canvas.width = 0;record.canvas.height = 0; }
    record.els['page-frame'].remove();const slot = slots.get(record.index);if (slot) { slot._placeholder.hidden = false;slot._placeholder.textContent = '第 ' + (record.index + 1) + ' 页 · 点击显示'; }
    records.delete(record.index);return true;
  }
  function trimCache(extraPixels = 0, protect = -1) {
    const candidates = [...records.values()].filter(record => record.index !== activeIndex && record.index !== protect && !record.job && !record.pins && !record.engine?.hasActiveGesture() && !hasTextSelection(record)).sort((a, b) => a.lastUsed - b.lastUsed);
    for (const record of candidates) {
      if (records.size <= MAX_CACHED_PAGES && residentPixels() + inflightPixels + extraPixels <= MAX_CANVAS_PIXELS) break;
      evict(record);
    }
  }

  function densityFor(metric, index) {
    let requested = Number(renderDensity(metric, zoom));if (!Number.isFinite(requested) || requested <= 0) requested = 1;
    return Math.min(requested, index === activeIndex ? 4 : 2, Math.sqrt(MAX_PAGE_PIXELS / (metric.width * metric.height)), 16384 / Math.max(metric.width, metric.height));
  }

  async function textFor(page, view, record, job) {
    const container = document.createElement('div');container.className = 'ip-text-layer textLayer';container.dataset.ui = 'pdf-text';container.setAttribute('aria-label', '第 ' + (record.index + 1) + ' 页正文文字');
    const totalScale = view.scale * view.userUnit;
    container.style.setProperty('--total-scale-factor', String(totalScale));container.style.setProperty('--scale-round-x', '1px');container.style.setProperty('--scale-round-y', '1px');
    container.style.width = view.rawDims.pageWidth * totalScale + 'px';container.style.height = view.rawDims.pageHeight * totalScale + 'px';
    try {
      const text = await page.getTextContent({ includeMarkedContent: true, disableNormalization: true });checkJob(record, job);
      if (text.items.length > 250000) return { container, notice: '此页文字层过大，仍可阅读页面和批注。' };
      record.textTask = new pdfjsLib.TextLayer({ textContentSource: text, container, viewport: view });await record.textTask.render();checkJob(record, job);
      return { container, notice: text.items.some(item => typeof item.str === 'string' && /\S/u.test(item.str)) ? '' : '此页没有可选择的文本层；未进行 OCR。' };
    } catch (error) { if (ignored(error) || job.cancelled || destroyed || job.epoch !== epoch) throw cancelled();container.replaceChildren();return { container, notice: '此页文字层无法读取，仍可阅读页面和批注。' }; }
    finally { if (record.job === job) record.textTask = null; }
  }
  function checkJob(record, job) { if (destroyed || job.cancelled || job.epoch !== epoch || record.job !== job || records.get(record.index) !== record) throw cancelled(); }

  async function renderRecord(record, job) {
    const page = await pdf.getPage(record.index + 1);checkJob(record, job);
    const view = page.getViewport({ scale: 1 }), metric = metrics[record.index];
    if (Math.abs(view.width - metric.width) > .02 || Math.abs(view.height - metric.height) > .02) throw new Error('页面尺寸不一致，无法保证批注位置准确。');
    job.zoom = zoom;
    let density = densityFor(view, record.index), wanted = Math.ceil(view.width * density) * Math.ceil(view.height * density);
    trimCache(wanted, record.index);
    const available = Math.max(1, MAX_CANVAS_PIXELS - residentPixels() - inflightPixels);
    if (wanted > available) density = Math.min(density, Math.sqrt(available / (view.width * view.height)) * .995);
    const canvas = document.createElement('canvas');canvas.width = Math.max(1, Math.ceil(view.width * density));canvas.height = Math.max(1, Math.ceil(view.height * density));
    canvas.style.width = view.width + 'px';canvas.style.height = view.height + 'px';canvas.dataset.density = String(density);job.pixels = canvas.width * canvas.height;inflightPixels += job.pixels;
    record.task = page.render({ canvasContext: canvas.getContext('2d', { alpha: false }), viewport: view, transform: [density, 0, 0, density, 0, 0], background: 'rgb(255,255,255)', annotationMode: annotationsVisible ? pdfjsLib.AnnotationMode.ENABLE : pdfjsLib.AnnotationMode.DISABLE });
    await record.task.promise;checkJob(record, job);
    const text = record.ready ? null : await textFor(page, view, record, job);checkJob(record, job);
    if (!record.engine) {
      record.initializing = true;
      try {
        record.engine = createEngine(record.els.ink, record.index, record);record.engine.setPageSize(view.width, view.height);record.engine.loadPage(inkPages[record.index]);
        if (histories.has(record.index)) record.engine.importHistory(histories.get(record.index));
        const tool = annotationsVisible ? getTool() : 'pan';record.engine.setTool(tool);record.appliedTool = tool;record.engine.setStyle(getStyle());
      } catch (error) { record.engine?.destroy();record.engine = null;throw error; }
      finally { record.initializing = false; }
    }
    const oldCanvas = record.canvas;record.canvas = canvas;record.density = density;record.paintEpoch = epoch;record.paintAnnotations = annotationsVisible;record.ready = true;record.error = null;
    record.els.document.replaceChildren(canvas);if (oldCanvas) { oldCanvas.width = 0;oldCanvas.height = 0; }
    if (text) { record.els['pdf-text'].replaceWith(text.container);record.els['pdf-text'] = text.container;record.textNotice = text.notice; }
    const slot = createSlot(record.index);if (!record.els['page-frame'].parentNode) slot.append(record.els['page-frame']);
    const tool = annotationsVisible ? getTool() : 'pan';if (record.appliedTool !== tool) { record.engine.setTool(tool);record.appliedTool = tool; }record.engine.setStyle(getStyle());
    resizeRecord(record);applyVisibility(record);record.lastUsed = ++clock;notify(onReady, record, record.index);
    return record;
  }

  function pump() {
    if (destroyed) return;
    queue.sort((a, b) => b.priority - a.priority || (a.priority >= 100 ? b.order - a.order : a.order - b.order));
    while (running < 2 && queue.length) {
      const job = queue.shift(), record = job.record;
      if (job.cancelled || record.job !== job) continue;
      running++;record.loading = true;
      function complete(error, value) {
        if (error && !ignored(error)) {
          record.error = error;record.failedSignature = epoch + ':' + zoom;const slot = slots.get(record.index);if (slot) slot._placeholder.textContent = '第 ' + (record.index + 1) + ' 页暂无法显示 · 点击重试';
          if (record.ready) applyVisibility(record);
        }
        running--;inflightPixels -= job.pixels || 0;
        if (record.job === job) { record.loading = false;record.promise = null;record.job = null;record.task = null; }
        if (!error) activateVisibleReady();
        trimCache(0, record.index);pump();
        // Public promises settle only after the old job was removed, so an
        // immediate visibility retry cannot receive the same rejected promise.
        if (error) job.reject(error);else job.resolve(value);
        if (!error && !destroyed && job.epoch === epoch && job.zoom !== zoom && desiredIndices.has(record.index)) ensure(record.index, 20).catch(next => { if (!ignored(next)) notify(onError, next, record.index, { background: true }); });
      }
      renderRecord(record, job).then(value => complete(null, value), error => complete(error));
    }
  }
  function ensure(index, priority = 0, force = false) {
    if (destroyed) return Promise.reject(cancelled());
    index = indexOf(index);const record = records.get(index) || makeRecord(index);record.lastUsed = ++clock;
    if (!force && paintCurrent(record) && Math.abs(record.density - densityFor(metrics[index], index)) < .15) return Promise.resolve(record);
    if (record.promise) { if (record.job) record.job.priority = Math.max(record.job.priority, priority);return record.promise; }
    const job = { record, epoch, priority, order: ++clock, cancelled: false, pixels: 0 };
    record.job = job;record.promise = new Promise((resolve, reject) => { job.resolve = resolve;job.reject = reject; });queue.push(job);pump();return record.promise;
  }
  function invalidate() {
    epoch++;
    for (const job of queue) { job.cancelled = true;job.reject(cancelled());if (job.record.job === job) { job.record.promise = null;job.record.job = null;job.record.loading = false; } }
    queue = [];
    for (const record of records.values()) {
      if (record.job) { record.job.cancelled = true;record.task?.cancel();record.textTask?.cancel(); }
      applyVisibility(record);
    }
  }
  function discardUnusedRequests() {
    for (const record of records.values()) {
      const job = record.job;
      if (!job || job.priority < 100 || record.index === activeIndex || record.index === activeShow?.index || record.pins || desiredIndices.has(record.index)) continue;
      job.cancelled = true;record.task?.cancel();record.textTask?.cancel();
      if (queue.includes(job)) {
        queue = queue.filter(value => value !== job);record.job = null;record.promise = null;record.loading = false;job.reject(cancelled());
      }
    }
    trimCache();
  }

  function localTop() { return mounted ? container.getBoundingClientRect().top - viewport.getBoundingClientRect().top + viewport.scrollTop : 0; }
  function nearestRow(y) {
    let low = 0, high = rows.length - 1;
    while (low < high) { const middle = Math.floor((low + high) / 2);if (rows[middle].y + rows[middle].height < y) low = middle + 1;else high = middle; }
    return low;
  }
  function visibleCandidates() {
    if (!rows.length) return [];
    const area = viewport.getBoundingClientRect(), bounds = container.getBoundingClientRect();
    const x = area.left + viewport.clientWidth / 2 - bounds.left;
    const y = viewport.scrollTop - localTop() + viewport.clientHeight / 2, middle = nearestRow(y), candidates = [];
    for (let rowIndex = Math.max(0, middle - 5); rowIndex <= Math.min(rows.length - 1, middle + 5); rowIndex++) {
      const row = rows[rowIndex], distance = y < row.y ? row.y - y : y > row.y + row.height ? y - row.y - row.height : 0;
      if (distance > Math.max(1000, viewport.clientHeight * 1.6) && rowIndex !== middle) continue;
      let left = (maxWidth - row.width) / 2;
      for (const index of row.indices) {
        const right = left + metrics[index].width * zoom, horizontal = x < left ? left - x : x > right ? x - right : 0;
        candidates.push({ index, distance: distance + horizontal * .25 + (index === activeIndex ? -.1 : 0) });left = right + PAGE_GAP;
      }
    }
    return candidates.sort((a, b) => a.distance - b.distance || a.index - b.index).slice(0, MAX_CACHED_PAGES).map(value => value.index);
  }
  function scheduleViewport() {
    if (!mounted || destroyed || !container.isConnected) return;
    const candidates = visibleCandidates();desiredIndices = new Set(candidates);
    for (const index of candidates) {
      const record = records.get(index);if (record?.error && record.failedSignature === epoch + ':' + zoom) continue;
      ensure(index, index === activeIndex ? 50 : 5).catch(error => { if (!ignored(error)) notify(onError, error, index, { background: true }); });
    }
    activateVisibleReady(candidates);
  }
  function activateVisibleReady(candidates = visibleCandidates()) {
    if (!mounted || destroyed || !container.isConnected || !canActivate() || !candidates.length || records.get(activeIndex)?.engine?.hasActiveGesture()) return;
    const nearest = candidates.find(index => paintCurrent(records.get(index) || {}));
    if (nearest !== undefined && nearest !== activeIndex) { try { activate(records.get(nearest)); } catch (error) { notify(onError, error, nearest); } }
  }
  function scroll() { if (!frameRequest) frameRequest = requestAnimationFrame(() => { frameRequest = 0;scheduleViewport(); }); }

  function captureAnchor() {
    const index = continuous() ? (visibleCandidates()[0] ?? activeIndex) : activeIndex, slot = slots.get(index);
    if (!mounted || !slot?.isConnected) return null;
    const bounds = slot.getBoundingClientRect(), area = viewport.getBoundingClientRect();
    return { index, x: (area.left + viewport.clientWidth / 2 - bounds.left) / zoom, y: (area.top + viewport.clientHeight / 2 - bounds.top) / zoom };
  }
  function restoreAnchor(anchor) {
    if (!anchor || !mounted) return;const slot = slots.get(anchor.index);if (!slot?.isConnected) return;
    const bounds = slot.getBoundingClientRect(), area = viewport.getBoundingClientRect();
    viewport.scrollLeft += bounds.left + anchor.x * zoom - area.left - viewport.clientWidth / 2;
    viewport.scrollTop += bounds.top + anchor.y * zoom - area.top - viewport.clientHeight / 2;
  }

  async function prepare(index = 0) { activeIndex = indexOf(index);applyLayout(layoutPlan(layout, zoom, activeIndex));return ensure(activeIndex, 100); }
  function mount() {
    if (destroyed) throw cancelled();const record = records.get(activeIndex);if (!paintCurrent(record || {})) throw new Error('当前页面尚未准备完成。');
    const previous = [...tray.childNodes];tray.replaceChildren(container);mounted = true;
    try { activate(record, { force: true }); }
    catch (error) { mounted = false;tray.replaceChildren(...previous);throw error; }
    viewport.addEventListener('scroll', scroll, { passive: true });scheduleViewport();return record;
  }
  async function show(index, { preserveView = false, interactive = false } = {}) {
    if (destroyed) throw cancelled();index = indexOf(index);const serial = ++showSerial, previous = activeIndex;
    activeShow?.reject(cancelled());
    const request = { index, serial };request.cancel = new Promise((resolve, reject) => { request.reject = reject; });activeShow = request;
    const target = records.get(index) || makeRecord(index);target.pins++;
    try {
      const record = await Promise.race([ensure(index, 100), request.cancel]);if (destroyed || serial !== showSerial || interactive && !canActivate()) throw cancelled();
      if (!continuous() && pageRows[index] === undefined) applyLayout(layoutPlan(layout, zoom, index));
      try { activate(record, { force: true }); }
      catch (error) { activeIndex = previous;if (!continuous()) applyLayout(layoutPlan());throw error; }
      if (mounted && !preserveView) {
        const slot = slots.get(index), bounds = slot.getBoundingClientRect(), area = viewport.getBoundingClientRect();
        viewport.scrollTop += bounds.top - area.top - 16;
        if (bounds.left < area.left + 16 || bounds.right > area.left + viewport.clientWidth - 16) viewport.scrollLeft += bounds.left - area.left - 16;
      }
      scheduleViewport();return record;
    } finally { target.pins--;if (activeShow === request) activeShow = null;discardUnusedRequests(); }
  }
  function setZoom(value, { preserveAnchor = true } = {}) {
    if (!finite(value) || value < .1 || value > 4) throw new RangeError('页面倍率须在 10%–400% 之间。');
    const plan = layoutPlan(layout, value), anchor = preserveAnchor ? captureAnchor() : null;zoom = value;applyLayout(plan, { reuseNodes: true });restoreAnchor(anchor);scroll();return zoom;
  }
  async function setLayout(value) {
    if (!LAYOUTS.includes(value)) throw new TypeError('没有此阅读布局。');
    if (value === layout) return layout;
    const plan = layoutPlan(value), anchor = captureAnchor(), previous = layout;layout = value;
    try { applyLayout(plan);restoreAnchor(anchor);scheduleViewport();return layout; }
    catch (error) { layout = previous;applyLayout(layoutPlan());restoreAnchor(anchor);throw error; }
  }
  async function setAnnotationsVisible(value) {
    if (typeof value !== 'boolean') throw new TypeError('批注显隐须为布尔值。');if (value === annotationsVisible) return;
    const previous = annotationsVisible;finishAll();annotationsVisible = value;invalidate();syncTools();
    // Wait for canceled tasks to finish before enqueuing the new active render.
    const active = records.get(activeIndex);if (active?.promise) await active.promise.catch(() => {});
    try { await ensure(activeIndex, 100);syncTools();scheduleViewport(); }
    catch (error) {
      annotationsVisible = previous;invalidate();
      for (const record of records.values()) if (record.ready && record.paintAnnotations === previous) record.paintEpoch = epoch;
      syncTools();scheduleViewport();throw error;
    }
  }
  function setTextSelectable(value) { textSelectable = Boolean(value);for (const record of records.values()) applyVisibility(record); }
  function refresh() { scroll(); }
  function activateReadyPage(index) {
    const record=records.get(index);if(!record?.engine||!paintCurrent(record)||!canActivate())return null;
    return activate(record)?record:null;
  }
  function getFitMetrics(index = activeIndex) { const indices = groupFor(indexOf(index));return { width: indices.reduce((sum, i) => sum + metrics[i].width, 0), height: Math.max(...indices.map(i => metrics[i].height)), gap: PAGE_GAP * (indices.length - 1), indices }; }
  function getAdjacentIndex(direction) { const step = direction < 0 ? -1 : 1;if (doubles()) { const first = Math.floor(activeIndex / 2) * 2 + step * 2;return first < 0 || first >= metrics.length ? activeIndex : first; }return indexOf(activeIndex + step); }
  function destroy() {
    if (destroyed) return;finishAll();destroyed = true;mounted = false;++showSerial;activeShow?.reject(cancelled());activeShow = null;invalidate();
    viewport.removeEventListener('scroll', scroll);if (frameRequest) cancelAnimationFrame(frameRequest);
    for (const record of records.values()) { record.task?.cancel();record.textTask?.cancel();record.engine?.destroy();if (record.canvas) { record.canvas.width = 0;record.canvas.height = 0; } }
    records.clear();container.remove();
  }
  applyLayout(layoutPlan());
  return Object.freeze({
    prepare, mount, show, setZoom, setLayout, setAnnotationsVisible, setTextSelectable, syncTools, finishAll, refresh, destroy,
    getFitMetrics, getAdjacentIndex, captureAnchor, restoreAnchor, activateReadyPage,
    getActiveSurface: name => SURFACES.includes(name) ? records.get(activeIndex)?.els[name] || null : null,
    getActiveRecord: () => records.get(activeIndex) || null,
    getRecords: () => Object.freeze([...records.values()].filter(record => record.engine)),
    ownsRecord: record => !!record && records.get(record.index) === record,
    getLayout: () => layout, getZoom: () => zoom, getContainer: () => container,
    limits: Object.freeze({ cachedPages: MAX_CACHED_PAGES, canvasPixels: MAX_CANVAS_PIXELS, extent: MAX_EXTENT, renderConcurrency: 2 }),
  });
}
