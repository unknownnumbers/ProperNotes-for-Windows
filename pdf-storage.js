/* InkPDF single-file editing data and printable vector appearance. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory(require('pdf-lib'), require('./pdf-bookmarks.js'), require('./annotation-geometry.js'), require('./text-appearance.js'));
  else root.InkPDFStorage = factory(root.PDFLib, root.ProperNotesBookmarks, root.ProperNotesGeometry, root.ProperNotesText);
})(typeof globalThis === 'object' ? globalThis : this, function (PDFLib, Bookmarks, Geometry, Text) {
  'use strict';
  if (!PDFLib) throw new Error('PDFLib must be loaded before pdf-storage.js');
  if (!Bookmarks) throw new Error('pdf-bookmarks.js must be loaded before pdf-storage.js');
  if (!Geometry || !Text) throw new Error('Annotation geometry and text appearance helpers must be loaded before pdf-storage.js');
  const {
    PDFDocument, PDFName, PDFNumber, PDFString, PDFHexString, PDFDict, PDFArray, PDFRef,
    PDFStream, PDFRawStream, decodePDFRawStream,
  } = PDFLib;
  // v2 adds basic shapes; v3 locks; v4 text/review objects and similarity transforms.
  // Legacy formats keep their original appearance metadata during verification.
  const VERSION = 4;
  const SHAPE_TOOLS = ['line', 'arrow', 'rectangle', 'ellipse'];
  const TEXT_TOOLS = ['textbox', 'note'];
  const REVIEW_TOOLS = ['text-highlight', 'text-underline', 'text-squiggle', 'text-strike'];
  const KEY = PDFName.of('InkPDF');
  const MAX_SOURCE_BYTES = 256 * 1024 * 1024;
  const MAX_STATE_BYTES = 64 * 1024 * 1024;
  const MAX_POINTS = 2000000;
  const MAX_PAGES = 10000;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder('utf-8', { fatal: true });
  function documentId(value) {
    if(value!==undefined){
      if(typeof value!=='string'||!/^document-[A-Za-z0-9_-]{1,120}$/.test(value))fail('EDIT_DATA_INVALID','PDF 的文档标识无效，请保留原文件。');
      return value;
    }
    try{if(globalThis.crypto?.randomUUID)return 'document-'+globalThis.crypto.randomUUID();}catch{/* Use a local identifier when UUID is unavailable. */}
    return 'document-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2)+'-'+Math.random().toString(36).slice(2);
  }

  class StorageError extends Error {
    constructor(code, message, cause) {
      super(message);
      this.name = 'InkPDFStorageError';
      this.code = code;
      if (cause) this.cause = cause;
    }
  }
  function fail(code, message, cause) { throw new StorageError(code, message, cause); }
  function bytes(value) {
    let result;
    if (value instanceof Uint8Array) result = value;
    else if (value instanceof ArrayBuffer) result = new Uint8Array(value);
    else if (ArrayBuffer.isView(value)) result = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    else fail('INVALID_INPUT', '无法读取 PDF 文件数据。');
    if (result.length < 5 || result.length > MAX_SOURCE_BYTES * 2.5) fail('FILE_SIZE', 'PDF 文件为空或超过当前可处理的大小。');
    return result;
  }
  function isFiniteNumber(value) { return typeof value === 'number' && Number.isFinite(value); }
  function close(a, b) { return Math.abs(a - b) <= Math.max(0.001, Math.abs(b) * 0.000001); }
  function round(value) { return Math.round(value * 100000) / 100000; }
  function num(value) { return String(round(value)); }
  function matrixNum(value) { return value === 0 ? '0' : value.toFixed(12).replace(/\.?0+$/, ''); }
  function rgb(color) {
    let hex = color.slice(1);
    if (hex.length === 3) hex = hex.split('').map(c => c + c).join('');
    return [0, 2, 4].map(start => parseInt(hex.slice(start, start + 2), 16) / 255);
  }
  async function loadDocument(input) {
    try {
      const document = await PDFDocument.load(input, { updateMetadata: false, throwOnInvalidObject: true });
      if (document.isEncrypted) fail('ENCRYPTED_PDF', '暂不支持加密 PDF，请先使用未加密的副本。');
      const count = document.getPageCount();
      if (!count || count > MAX_PAGES) fail('PAGE_COUNT', 'PDF 没有可用页面或页数超过当前处理范围。');
      return document;
    } catch (error) {
      if (error instanceof StorageError) throw error;
      if (/encrypt|password/i.test(String(error && error.message))) fail('ENCRYPTED_PDF', '暂不支持加密 PDF，请先使用未加密的副本。', error);
      fail('INVALID_PDF', '无法解析此 PDF，文件可能已损坏或格式不受支持。', error);
    }
  }

  function metricOf(page) {
    const media = page.getMediaBox();
    const crop = page.getCropBox();
    let x0 = Math.max(media.x, crop.x), y0 = Math.max(media.y, crop.y);
    let x1 = Math.min(media.x + media.width, crop.x + crop.width), y1 = Math.min(media.y + media.height, crop.y + crop.height);
    if (!(x1 > x0 && y1 > y0)) { x0 = media.x; y0 = media.y; x1 = media.x + media.width; y1 = media.y + media.height; }
    let rotation = ((page.getRotation().angle % 360) + 360) % 360;
    if (![0, 90, 180, 270].includes(rotation)) fail('PAGE_ROTATION', 'PDF 页面旋转角度不是支持的 90 度倍数。');
    const unitObject = page.node.lookupMaybe(PDFName.of('UserUnit'), PDFNumber);
    const unit = unitObject ? unitObject.asNumber() : 1;
    if (!isFiniteNumber(unit) || unit <= 0 || unit > 75000) fail('PAGE_GEOMETRY', 'PDF 页面缩放单位无效。');
    let transform;
    if (rotation === 0) transform = [unit, 0, 0, -unit, -x0 * unit, y1 * unit];
    if (rotation === 90) transform = [0, unit, unit, 0, -y0 * unit, -x0 * unit];
    if (rotation === 180) transform = [-unit, 0, 0, unit, x1 * unit, -y0 * unit];
    if (rotation === 270) transform = [0, -unit, -unit, 0, y1 * unit, x1 * unit];
    const swapped = rotation === 90 || rotation === 270;
    return {
      width: (swapped ? y1 - y0 : x1 - x0) * unit,
      height: (swapped ? x1 - x0 : y1 - y0) * unit,
      viewBox: [x0, y0, x1, y1], rotation, transform: transform.map(value => value === 0 ? 0 : value),
    };
  }
  function getPageMetrics(document) { return document.getPages().map(metricOf); }
  function outlineValue(document, value) {
    const seen = new Set();
    while (value instanceof PDFRef) {
      if (seen.has(value.toString()) || seen.size > 8) fail('BOOKMARK_STRUCTURE', '书签对象引用存在循环或层级过深。');
      seen.add(value.toString());value = document.context.lookup(value);
    }
    return value;
  }
  function namedOutlineDestinations(document) {
    const legacy = new Map(), names = new Map(), ambiguousLegacy = new Set(), ambiguousNames = new Set();
    function add(map, ambiguous, name, destination) {
      if (typeof name !== 'string' || name.length > 4096) return;
      if (map.has(name) || ambiguous.has(name)) { map.delete(name);ambiguous.add(name);return; }
      map.set(name, destination);
    }
    try {
      const dictionary = outlineValue(document, document.catalog.get(PDFName.of('Dests')));
      if (dictionary instanceof PDFDict) {
        const entries = dictionary.entries();
        if (entries.length > Bookmarks.MAX_BOOKMARKS) throw new Error('Destination limit');
        for (const [key, value] of entries) add(legacy, ambiguousLegacy, key.decodeText(), value);
      }
    } catch (_) { legacy.clear(); }
    try {
      const catalogNames = outlineValue(document, document.catalog.get(PDFName.of('Names')));
      const root = catalogNames instanceof PDFDict ? catalogNames.get(PDFName.of('Dests')) : undefined;
      const visited = new Set();let total = 0;
      function walk(raw, depth) {
        if (raw === undefined) return;
        const dictionary = outlineValue(document, raw);
        if (!(dictionary instanceof PDFDict) || visited.has(dictionary) || depth >= Bookmarks.MAX_DEPTH || ++total > Bookmarks.MAX_BOOKMARKS) throw new Error('Invalid destination name tree');
        visited.add(dictionary);
        const entries = outlineValue(document, dictionary.get(PDFName.of('Names'))), children = outlineValue(document, dictionary.get(PDFName.of('Kids')));
        if (entries !== undefined && children !== undefined) throw new Error('Ambiguous name tree');
        if (entries !== undefined) {
          if (!(entries instanceof PDFArray) || entries.size() % 2 || entries.size() > Bookmarks.MAX_BOOKMARKS * 2) throw new Error('Invalid destination names');
          for (let i = 0; i < entries.size(); i += 2) {
            if (++total > Bookmarks.MAX_BOOKMARKS) throw new Error('Destination limit');
            const key = outlineValue(document, entries.get(i));
            if (!(key instanceof PDFString) && !(key instanceof PDFHexString)) throw new Error('Invalid destination key');
            add(names, ambiguousNames, key.decodeText(), entries.get(i + 1));
          }
        } else if (children !== undefined) {
          if (!(children instanceof PDFArray) || children.size() > Bookmarks.MAX_BOOKMARKS) throw new Error('Invalid destination children');
          for (let i = 0; i < children.size(); i++) walk(children.get(i), depth + 1);
        }
      }
      walk(root, 0);
    } catch (_) { names.clear(); }
    return { legacy, names };
  }
  function outlineDestination(document, dictionary, pageRefs, pageObjects, named) {
    let destination = dictionary.get(PDFName.of('Dest')), targetType = 'unsupported', namedDestination;
    const rawAction = dictionary.get(PDFName.of('A'));
    // PDF forbids using Dest and A together. Keep ambiguous targets untouched.
    if (destination !== undefined && rawAction !== undefined) return { pageIndex: null, targetType };
    try {
      if (rawAction !== undefined) {
        const action = outlineValue(document, rawAction);
        if (!(action instanceof PDFDict)) return { pageIndex: null, targetType };
        const kind = outlineValue(document, action.get(PDFName.of('S')));
        if (!(kind instanceof PDFName)) return { pageIndex: null, targetType };
        if (kind.toString() !== '/GoTo') return { pageIndex: null, targetType: 'external' };
        destination = action.get(PDFName.of('D'));
      }
      const seenNames = new Set();
      for (let depth = 0; depth < 8; depth++) {
        const value = outlineValue(document, destination);
        if (value instanceof PDFDict) { destination = value.get(PDFName.of('D'));continue; }
        if (value instanceof PDFArray) {
          const first = value.size() ? value.get(0) : undefined;
          const index = first instanceof PDFRef ? pageRefs.get(first.toString()) : pageObjects.get(outlineValue(document, first));
          // A numeric first item belongs to remote destinations; never guess a local page.
          return { pageIndex: index === undefined ? null : index, targetType: targetType === 'named' ? 'named' : index === undefined ? 'unsupported' : 'page', ...(namedDestination === undefined ? {} : { namedDestination }) };
        }
        if (value instanceof PDFName || value instanceof PDFString || value instanceof PDFHexString) {
          targetType = 'named';const key = value.decodeText(), kind = value instanceof PDFName ? 'legacy' : 'names', token = kind + ':' + key;
          if (key.length > 4096) break;
          if (namedDestination === undefined) namedDestination = key;
          if (seenNames.has(token)) break;
          seenNames.add(token);destination = named[kind].get(key);continue;
        }
        break;
      }
    } catch (_) { /* Unknown targets remain in the original PDF dictionary. */ }
    return { pageIndex: null, targetType, ...(namedDestination === undefined ? {} : { namedDestination }) };
  }
  function readBookmarkStructure(document) {
    const result = { tree: [], editable: true, message: '', root: null, records: new Map() };
    try {
      const rawRoot = document.catalog.get(PDFName.of('Outlines'));
      if (rawRoot === undefined) return result;
      const root = outlineValue(document, rawRoot);
      if (!(root instanceof PDFDict)) fail('BOOKMARK_STRUCTURE', 'PDF 的书签根对象无效。');
      const rootType = outlineValue(document, root.get(PDFName.of('Type')));
      if (rootType !== undefined && (!(rootType instanceof PDFName) || rootType.toString() !== '/Outlines')) fail('BOOKMARK_STRUCTURE', 'PDF 的书签根对象类型无效。');
      const pageRefs = new Map(), pageObjects = new Map(), seen = new Set([root]), named = namedOutlineDestinations(document);
      document.getPages().forEach((page, index) => { pageRefs.set(page.ref.toString(), index);pageObjects.set(page.node, index); });
      result.root = { dictionary: root, ref: rawRoot instanceof PDFRef ? rawRoot : document.context.getObjectRef(root) || null };
      function linkIs(raw, dictionary) { return raw !== undefined && outlineValue(document, raw) === dictionary; }
      function readChildren(parent, depth) {
        const children = [];let raw = parent.get(PDFName.of('First')), previous = null;
        while (raw !== undefined) {
          if (depth >= Bookmarks.MAX_DEPTH || result.records.size >= Bookmarks.MAX_BOOKMARKS) fail('BOOKMARK_LIMIT', '书签数量或层级超过当前编辑范围。');
          const dictionary = outlineValue(document, raw);
          if (!(dictionary instanceof PDFDict) || seen.has(dictionary)) fail('BOOKMARK_STRUCTURE', '书签树存在循环、重复节点或无效对象。');
          seen.add(dictionary);
          if (!linkIs(dictionary.get(PDFName.of('Parent')), parent)) fail('BOOKMARK_STRUCTURE', '书签父子关系不一致。');
          const previousRaw = dictionary.get(PDFName.of('Prev'));
          if (previous ? !linkIs(previousRaw, previous) : previousRaw !== undefined) fail('BOOKMARK_STRUCTURE', '书签前后顺序不一致。');
          const ref = raw instanceof PDFRef ? raw : document.context.getObjectRef(dictionary) || null;
          // Nonstandard direct dictionaries receive deterministic preorder IDs
          // for this immutable editing baseline, without unbounded path strings.
          const id = ref ? 'bookmark-pdf-' + ref.objectNumber + '-' + ref.generationNumber : 'bookmark-direct-' + result.records.size;
          if (result.records.has(id)) fail('BOOKMARK_STRUCTURE', '书签标识重复。');
          const rawTitle = outlineValue(document, dictionary.get(PDFName.of('Title')));
          if (!(rawTitle instanceof PDFString) && !(rawTitle instanceof PDFHexString)) fail('BOOKMARK_STRUCTURE', '书签标题不是有效的 PDF 文本。');
          const title = rawTitle.decodeText();
          if (title.length > 4096) fail('BOOKMARK_LIMIT', '书签标题超过当前编辑范围。');
          const record = { dictionary, ref, children: [] };result.records.set(id, record);
          const descendants = readChildren(dictionary, depth + 1);
          const count = outlineValue(document, dictionary.get(PDFName.of('Count')));
          const expanded = descendants.length > 0 && count instanceof PDFNumber && count.asNumber() > 0;
          const node = { id, title, ...outlineDestination(document, dictionary, pageRefs, pageObjects, named), expanded, children: descendants };
          record.node = node;children.push(node);previous = dictionary;raw = dictionary.get(PDFName.of('Next'));
        }
        const last = parent.get(PDFName.of('Last'));
        if (children.length ? !linkIs(last, previous) : last !== undefined) fail('BOOKMARK_STRUCTURE', '书签末尾指针不一致。');
        return children;
      }
      result.tree = readChildren(root, 0);
      function checkCounts(nodes) {
        let visible = 0;
        for (const node of nodes) {
          const descendants = checkCounts(node.children), dictionary = result.records.get(node.id).dictionary;
          const count = outlineValue(document, dictionary.get(PDFName.of('Count')));
          if (descendants) {
            if (!(count instanceof PDFNumber) || !Number.isInteger(count.asNumber()) || count.asNumber() !== (node.expanded ? descendants : -descendants)) fail('BOOKMARK_STRUCTURE', '书签展开计数与可见子项不一致。');
          } else if (count !== undefined && (!(count instanceof PDFNumber) || count.asNumber() !== 0)) fail('BOOKMARK_STRUCTURE', '无子项书签的展开计数无效。');
          visible += 1 + (node.expanded ? descendants : 0);
        }
        return visible;
      }
      const visible = checkCounts(result.tree), rootCount = outlineValue(document, root.get(PDFName.of('Count')));
      if (rootCount !== undefined && (!(rootCount instanceof PDFNumber) || !Number.isInteger(rootCount.asNumber()) || rootCount.asNumber() !== visible)) fail('BOOKMARK_STRUCTURE', '书签根目录的可见计数不一致。');
      // Validate the pure UI model too, without changing any PDF objects.
      result.tree = Bookmarks.applyEdits(result.tree, [], { pageCount: document.getPageCount() });
    } catch (error) {
      result.editable = false;result.message = '现有 PDF 书签暂不可编辑：' + (error?.message || '书签结构无法安全识别。') + ' 原书签仍保留。';
    }
    return result;
  }
  function bookmarkResult(document) {
    const value = readBookmarkStructure(document);
    return { bookmarks: value.tree, bookmarksEditable: value.editable, bookmarksMessage: value.message };
  }
  function applyBookmarkEdits(document, edits) {
    if (!Array.isArray(edits) || edits.length > Bookmarks.MAX_EDITS) fail('BOOKMARK_EDITS', '书签操作记录无效或超过当前编辑范围。');
    if (!edits.length) return false;
    const structure = readBookmarkStructure(document);
    if (!structure.editable) fail('BOOKMARK_READ_ONLY', structure.message);
    const tree = Bookmarks.applyEdits(structure.tree, edits, { pageCount: document.getPageCount() });
    const records = structure.records, pages = document.getPages(), renamed = new Set(edits.filter(edit => edit.type === 'rename' || edit.type === 'add').map(edit => edit.id));
    const retargeted = new Set(edits.filter(edit => edit.type === 'set-target').map(edit => edit.id));
    if (!structure.root && !tree.length) return false;
    const root = structure.root || { dictionary: document.context.obj({ Type: 'Outlines' }), ref: null };
    function ensureRef(record) { if (!record.ref) record.ref = document.context.register(record.dictionary);return record.ref; }
    ensureRef(root);
    function prepare(nodes) {
      for (const node of nodes) {
        let record = records.get(node.id);
        if (!record) {
          record = { dictionary: document.context.obj({ Title: PDFHexString.fromText(node.title), Dest: [pages[node.pageIndex].ref, PDFName.of('Fit')] }), ref: null };
          records.set(node.id, record);
        }
        ensureRef(record);
        // Keep existing targets and extension entries unless the user explicitly
        // retargeted this node. Renaming/expansion/tree relinking never resolves
        // or replaces an unknown, named or external destination.
        if (renamed.has(node.id)) record.dictionary.set(PDFName.of('Title'), PDFHexString.fromText(node.title));
        if (retargeted.has(node.id)) {
          record.dictionary.set(PDFName.of('Dest'), document.context.obj([pages[node.pageIndex].ref, PDFName.of('Fit')]));
          record.dictionary.delete(PDFName.of('A'));
        }
        prepare(node.children);
      }
    }
    prepare(tree);
    function linkChildren(parent, nodes) {
      const dictionary = parent.dictionary;let visible = 0;
      if (nodes.length) {
        dictionary.set(PDFName.of('First'), ensureRef(records.get(nodes[0].id)));
        dictionary.set(PDFName.of('Last'), ensureRef(records.get(nodes[nodes.length - 1].id)));
      } else { dictionary.delete(PDFName.of('First'));dictionary.delete(PDFName.of('Last')); }
      nodes.forEach((node, index) => {
        const record = records.get(node.id), item = record.dictionary;
        item.set(PDFName.of('Parent'), ensureRef(parent));
        if (index) item.set(PDFName.of('Prev'), ensureRef(records.get(nodes[index - 1].id)));else item.delete(PDFName.of('Prev'));
        if (index + 1 < nodes.length) item.set(PDFName.of('Next'), ensureRef(records.get(nodes[index + 1].id)));else item.delete(PDFName.of('Next'));
        const descendants = linkChildren(record, node.children);
        if (descendants) item.set(PDFName.of('Count'), PDFNumber.of(node.expanded ? descendants : -descendants));else item.delete(PDFName.of('Count'));
        visible += 1 + (node.expanded ? descendants : 0);
      });
      return visible;
    }
    const visible = linkChildren(root, tree);root.dictionary.set(PDFName.of('Count'), PDFNumber.of(visible));
    document.catalog.set(PDFName.of('Outlines'), ensureRef(root));return true;
  }
  function validatePages(input, expected) {
    if (!Array.isArray(input) || input.length !== expected.length) fail('EDIT_DATA_INVALID', '编辑数据的页面数量与 PDF 不一致。');
    return input.map((page, i) => {
      if (!page || !isFiniteNumber(page.width) || !isFiniteNumber(page.height) || page.width <= 0 || page.height <= 0 || page.width > 1000000 || page.height > 1000000 || ![0, 90, 180, 270].includes(page.rotation)) fail('EDIT_DATA_INVALID', '编辑数据包含无效的页面尺寸。');
      if (!Array.isArray(page.viewBox) || page.viewBox.length !== 4 || !page.viewBox.every(isFiniteNumber) || !Array.isArray(page.transform) || page.transform.length !== 6 || !page.transform.every(isFiniteNumber)) fail('EDIT_DATA_INVALID', '编辑数据包含无效的页面坐标变换。');
      const e = expected[i];
      if (!close(page.width, e.width) || !close(page.height, e.height) || page.rotation !== e.rotation || !page.viewBox.every((value, j) => close(value, e.viewBox[j])) || !page.transform.every((value, j) => close(value, e.transform[j]))) fail('EDIT_DATA_INVALID', '编辑数据的页面位置与原始 PDF 不一致。');
      return { width: page.width, height: page.height, rotation: page.rotation, viewBox: page.viewBox.slice(), transform: page.transform.slice() };
    });
  }
  function validColor(value) { return typeof value === 'string' && /^#(?:[a-f\d]{3}|[a-f\d]{6})$/i.test(value); }
  function validateInk(input, pages, formatVersion = VERSION) {
    if (!Array.isArray(input) || input.length !== pages.length) fail('EDIT_DATA_INVALID', '编辑数据缺少页面笔迹。');
    let totalPoints = 0, totalTextBytes = 0, totalTextPixels = 0, totalTextCharacters = 0, totalReviewPoints = 0;
    return input.map((strokes, pageIndex) => {
      if (!Array.isArray(strokes) || strokes.length > 50000) fail('EDIT_DATA_INVALID', '单页笔迹数量超过当前支持范围。');
      const ids = new Set(), page = pages[pageIndex];
      return strokes.map(stroke => {
        const shape = stroke && SHAPE_TOOLS.includes(stroke.tool), text = stroke && TEXT_TOOLS.includes(stroke.tool), review = stroke && REVIEW_TOOLS.includes(stroke.tool);
        const supportedTool = stroke && (['pen', 'highlighter'].includes(stroke.tool) || (formatVersion >= 2 && shape) || (formatVersion >= 4 && (text || review)));
        if (!stroke || typeof stroke.id !== 'string' || !stroke.id.length || stroke.id.length > 160 || ids.has(stroke.id) || !supportedTool || !validColor(stroke.color) || !isFiniteNumber(stroke.width) || stroke.width < 0.1 || stroke.width > 100 || !isFiniteNumber(stroke.opacity) || stroke.opacity < 0 || stroke.opacity > 1 || typeof stroke.pressure !== 'boolean' || !Array.isArray(stroke.points) || !stroke.points.length || stroke.points.length > 100000) fail('EDIT_DATA_INVALID', '批注编辑数据无效，已停止打开以避免丢失编辑能力。');
        if (stroke.locked !== undefined && typeof stroke.locked !== 'boolean') fail('EDIT_DATA_INVALID', '批注锁定状态无效，已停止恢复以避免丢失编辑属性。');
        if ((shape || text) && (stroke.points.length !== 2 || stroke.pressure !== false)) fail('EDIT_DATA_INVALID', '形状或文字框必须包含两个端点，并使用固定粗细。');
        if (review && (stroke.points.length < 4 || stroke.points.length > 16000 || stroke.points.length % 4 || stroke.pressure !== false)) fail('EDIT_DATA_INVALID', '文字审阅批注必须包含完整四角区域。');
        let transform;
        try { transform = Geometry.normalizeMatrix(stroke.transform); }
        catch (error) { fail('EDIT_DATA_INVALID', '批注变换无效，仅支持等比缩放、旋转和平移。', error); }
        if (formatVersion < 4 && !Geometry.isIdentity(transform)) fail('EDIT_DATA_VERSION', '旧格式不能包含对象变换。');
        const fill = stroke.fill === undefined ? 'none' : stroke.fill;
        if (text || shape && ['rectangle', 'ellipse'].includes(stroke.tool)) {
          if (fill !== 'none' && !validColor(fill)) fail('EDIT_DATA_INVALID', '形状填充颜色无效。');
        } else if (fill !== 'none') fail('EDIT_DATA_INVALID', '此工具不支持填充，已停止恢复以避免丢失属性。');
        ids.add(stroke.id);
        totalPoints += stroke.points.length;
        if (totalPoints > MAX_POINTS) fail('EDIT_DATA_SIZE', '此 PDF 的笔迹采样点超过当前处理范围。');
        const points = stroke.points.map(point => {
          if (!Array.isArray(point) || point.length !== 3 || !point.every(isFiniteNumber) || point[2] < 0 || point[2] > 1) fail('EDIT_DATA_INVALID', '笔迹包含无效坐标或压力值。');
          if (formatVersion >= 4) {
            if (point[0] < 0 || point[1] < 0 || point[0] > 100000 || point[1] > 100000) fail('EDIT_DATA_INVALID', '批注局部坐标超过当前编辑范围。');
            const world = Geometry.apply(point, transform);
            if (world[0] < -0.01 || world[1] < -0.01 || world[0] > page.width + 0.01 || world[1] > page.height + 0.01) fail('EDIT_DATA_INVALID', '变换后的批注坐标超出页面。');
          } else if (point[0] < -0.01 || point[1] < -0.01 || point[0] > page.width + 0.01 || point[1] > page.height + 0.01) fail('EDIT_DATA_INVALID', '笔迹包含无效页面坐标。');
          if ((shape || text || review) && (point[0] < 0 || point[1] < 0 || point[2] !== 0.5 || formatVersion < 4 && (point[0] > page.width || point[1] > page.height))) fail('EDIT_DATA_INVALID', '批注控制点或固定压力标记无效。');
          return point.slice();
        });
        if (shape) {
          const dx = Math.abs(points[1][0] - points[0][0]), dy = Math.abs(points[1][1] - points[0][1]);
          if (['rectangle', 'ellipse'].includes(stroke.tool) ? !(dx > 0 && dy > 0) : !(dx > 0 || dy > 0)) fail('EDIT_DATA_INVALID', '形状尺寸无效，已停止恢复。');
        }
        const result = { id: stroke.id, tool: stroke.tool, color: stroke.color, width: stroke.width, opacity: stroke.opacity, pressure: stroke.pressure, locked: stroke.locked === true, points };
        if (formatVersion >= 4) result.transform = transform;
        if (shape || text) result.fill = fill;
        if (formatVersion >= 4 && (text || ['rectangle', 'ellipse'].includes(stroke.tool))) {
          const [a, b] = points;
          for (const corner of [[a[0],a[1]],[a[0],b[1]],[b[0],a[1]],[b[0],b[1]]]) {
            const world = Geometry.apply(corner, transform);
            if (world[0] < -0.01 || world[1] < -0.01 || world[0] > page.width + 0.01 || world[1] > page.height + 0.01) fail('EDIT_DATA_INVALID', '变换后的批注框超出页面。');
          }
        }
        if (review) {
          result.fill = 'none';
          for (const quad of Geometry.reviewQuads(points)) {
            let sign = 0;
            for (let index = 0; index < 4; index++) {
              const a = quad[index], b = quad[(index+1)%4], c = quad[(index+2)%4], cross = (b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]);
              if (Math.abs(cross) < 1e-10 || sign && cross * sign < 0) fail('EDIT_DATA_INVALID', '文字审阅区域不是有效凸四边形。');sign = Math.sign(cross);
            }
          }
          if (stroke.tool !== 'text-highlight') {
            totalReviewPoints += Geometry.reviewPaths(stroke.tool, points, stroke.width).reduce((sum,path)=>sum+path.points.length,0);
            if (totalReviewPoints > MAX_POINTS) fail('EDIT_DATA_SIZE', '文字审阅的外观路径超过当前处理范围。');
          }
        }
        if (text) {
          if(typeof stroke.text !== 'string')fail('EDIT_DATA_INVALID','文字内容无效。');
          totalTextCharacters += stroke.text.length;
          if(totalTextCharacters > Text.limits.totalText)fail('EDIT_DATA_SIZE','整份 PDF 的新增文字超过 100 万个字符。');
          Object.assign(result, { text: stroke.text, fontSize: stroke.fontSize, fontFamily: stroke.fontFamily, bold: stroke.bold, italic: stroke.italic, raster: stroke.raster });
          let png;
          try { png = Text.validateRaster(result, { required: true }); }
          catch (error) { fail('EDIT_DATA_INVALID', '文字内容或外观无法可靠恢复：' + error.message, error); }
          totalTextBytes += png.bytes.length;totalTextPixels += png.width * png.height;
          if (totalTextBytes > Text.limits.totalBytes || totalTextPixels > Text.limits.totalPixels) fail('EDIT_DATA_SIZE', '整份 PDF 的文字外观超过当前大小或像素范围。');
          result.raster = { width: stroke.raster.width, height: stroke.raster.height, data: stroke.raster.data, key: stroke.raster.key };
        }
        return result;
      });
    });
  }
  function readStream(document, dictionary, name, maxBytes) {
    const ref = dictionary.get(PDFName.of(name));
    const stream = ref && document.context.lookup(ref);
    if (!(stream instanceof PDFRawStream)) fail('EDIT_DATA_INVALID', 'PDF 内的笔迹编辑数据不完整。');
    try {
      const output = decodePDFRawStream(stream).getBytes(maxBytes + 1);
      if (!output.length || output.length > maxBytes) fail('EDIT_DATA_SIZE', 'PDF 内的编辑数据超过当前处理范围。');
      return output.slice();
    } catch (error) {
      if (error instanceof StorageError) throw error;
      fail('EDIT_DATA_INVALID', '无法解压 PDF 内的笔迹编辑数据。', error);
    }
  }

  function ownedAnnotation(annotation) {
    return annotation instanceof PDFDict && annotation.get(PDFName.of('InkPDFOwned'))?.toString() === 'true';
  }
  function sameBytes(first, second) {
    if (first.length !== second.length) return false;
    for (let i = 0; i < first.length; i++) if (first[i] !== second[i]) return false;
    return true;
  }
  // Compare our generated appearance with the stored one by value, not object
  // numbers. An external save may recompress streams or reorder dictionary keys.
  // Unknown annotation properties are deliberately not ignored: removing an
  // externally changed Stamp would otherwise discard somebody else's edits.
  function samePDFValue(actual, expected, actualContext, expectedContext, budget, depth = 0) {
    if (++budget.visited > 500000 || depth > 64) fail('EDIT_DATA_INVALID', '笔迹外观结构过于复杂，已停止恢复以保留原文件。');
    if (actual instanceof PDFRef) actual = actualContext.lookup(actual);
    if (expected instanceof PDFRef) expected = expectedContext.lookup(expected);
    if (!actual || !expected) return false;
    if (actual instanceof PDFRawStream || expected instanceof PDFRawStream) {
      if (!(actual instanceof PDFRawStream) || !(expected instanceof PDFRawStream)) return false;
      const representationKeys = new Set(['/Length', '/Filter', '/DecodeParms']);
      if (!samePDFDictionary(actual.dict, expected.dict, actualContext, expectedContext, budget, depth + 1, representationKeys)) return false;
      const expectedBytes = decodePDFRawStream(expected).getBytes(MAX_STATE_BYTES * 4 + 1);
      if (expectedBytes.length > MAX_STATE_BYTES * 4) fail('EDIT_DATA_SIZE', '笔迹外观超过当前可恢复的大小。');
      const actualBytes = decodePDFRawStream(actual).getBytes(expectedBytes.length + 1);
      return sameBytes(actualBytes, expectedBytes);
    }
    if (actual instanceof PDFDict || expected instanceof PDFDict) {
      return actual instanceof PDFDict && expected instanceof PDFDict && samePDFDictionary(actual, expected, actualContext, expectedContext, budget, depth + 1);
    }
    if (actual instanceof PDFArray || expected instanceof PDFArray) {
      if (!(actual instanceof PDFArray) || !(expected instanceof PDFArray) || actual.size() !== expected.size()) return false;
      for (let i = 0; i < actual.size(); i++) if (!samePDFValue(actual.get(i), expected.get(i), actualContext, expectedContext, budget, depth + 1)) return false;
      return true;
    }
    return actual.constructor === expected.constructor && actual.toString() === expected.toString();
  }
  function samePDFDictionary(actual, expected, actualContext, expectedContext, budget, depth, skipped = new Set()) {
    const keys = dictionary => dictionary.keys().filter(key => !skipped.has(key.toString())).sort((a, b) => a.toString().localeCompare(b.toString()));
    const actualKeys = keys(actual), expectedKeys = keys(expected);
    if (actualKeys.length !== expectedKeys.length) return false;
    for (let i = 0; i < expectedKeys.length; i++) {
      if (actualKeys[i].toString() !== expectedKeys[i].toString()) return false;
      if (!samePDFValue(actual.get(actualKeys[i]), expected.get(expectedKeys[i]), actualContext, expectedContext, budget, depth + 1)) return false;
    }
    return true;
  }
  function annotationEntries(document, page) {
    const annotations = page.node.Annots(), entries = [];
    if (!annotations) return entries;
    for (let i = 0; i < annotations.size(); i++) {
      const value = annotations.get(i), annotation = document.context.lookup(value);
      if (ownedAnnotation(annotation)) entries.push({ index: i, value, annotation });
    }
    return entries;
  }
  // Walk PDF object references without interpreting content streams. Candidate
  // collection skips the Stamp's /P link, which leads back to the entire page
  // tree. Reachability checks below do not skip anything.
  function referencedObjects(context, roots, skipPageLink = false) {
    const refs = new Map(), seen = new Set(), stack = roots.slice();
    let visited = 0;
    while (stack.length) {
      let value = stack.pop();
      if (!value) continue;
      if (++visited > 2000000) fail('EDIT_DATA_SIZE', 'PDF 对象数量超过当前可安全恢复的范围。');
      if (value instanceof PDFRef) {
        const key = value.toString();
        if (refs.has(key)) continue;
        refs.set(key, value);
        value = context.lookup(value);
        if (!value) continue;
      }
      if (seen.has(value)) continue;
      seen.add(value);
      if (value instanceof PDFStream) stack.push(value.dict);
      else if (value instanceof PDFDict) {
        for (const [key, child] of value.entries()) if (!skipPageLink || key.toString() !== '/P') stack.push(child);
      } else if (value instanceof PDFArray) {
        for (let i = 0; i < value.size(); i++) stack.push(value.get(i));
      }
    }
    return refs;
  }
  async function currentSource(document, original, dictionary, stored, pages, inkPages, formatVersion) {
    const pageObjects = document.getPages(), originalPages = original.getPages();
    const removals = [], candidateRoots = [stored];
    const allowedDataKeys = new Set(['/Type', '/Version', '/Source', '/State']);
    if (dictionary.get(PDFName.of('Type'))?.toString() !== '/InkPDFData' || dictionary.keys().some(key => !allowedDataKeys.has(key.toString()))) fail('EXTERNAL_EDIT_CONFLICT', 'PDF 内本软件的数据结构已被改变，已停止恢复；请保留当前文件。');
    for (let i = 0; i < pageObjects.length; i++) {
      const page = pageObjects[i];
      // The supported formats have no independent page identity manifest. The embedded source is
      // used only to check original page references, never as displayed content.
      // Conservatively refuse reordered/renumbered pages instead of guessing.
      if (page.ref.toString() !== originalPages[i].ref.toString()) fail('EXTERNAL_PAGE_CHANGE', 'PDF 的页面顺序或对象编号已改变，无法可靠对应可编辑笔迹；请保留当前文件。');
      const entries = annotationEntries(document, page);
      if (entries.length !== (inkPages[i].length ? 1 : 0)) fail('EXTERNAL_INK_CHANGE', 'PDF 中本软件的批注外观被删除、复制或改变，已停止恢复，避免覆盖外部修改。');
      if (!entries.length) continue;
      const entry = entries[0];
      const ownerPage = entry.annotation.get(PDFName.of('P'));
      if (!(ownerPage instanceof PDFRef) || ownerPage.toString() !== page.ref.toString()) fail('EXTERNAL_INK_CHANGE', 'PDF 中的笔迹归属页面已改变，已停止恢复。');
      const expectedDocument = await PDFDocument.create();
      let expectedRef;
      await paintPage(expectedDocument, { ref: page.ref, node: { addAnnot(ref) { expectedRef = ref; } } }, pages[i], inkPages[i], formatVersion);
      // /P was checked above and must not recursively compare the page tree.
      const expectedAnnotation = expectedDocument.context.lookup(expectedRef);
      if (!samePDFDictionary(entry.annotation, expectedAnnotation, document.context, expectedDocument.context, { visited: 0 }, 0, new Set(['/P']))) fail('EXTERNAL_INK_CHANGE', 'PDF 中本软件的批注外观或属性已被其他软件修改，已停止恢复，避免丢失这些修改。');
      removals.push({ page, entry });
      candidateRoots.push(entry.value);
    }
    const candidates = referencedObjects(document.context, candidateRoots, true);
    // All checks have succeeded. Only the in-memory document is changed here;
    // open() never writes to the file that the user selected.
    for (const { page, entry } of removals) page.node.Annots().remove(entry.index);
    document.catalog.delete(KEY);
    // pdf-lib serializes unreferenced indirect objects, too. Remove only our
    // detached objects, and retain every candidate still referenced anywhere
    // else (including otherwise-unreachable objects and trailer metadata).
    const survivingRoots = document.context.enumerateIndirectObjects().filter(([ref]) => !candidates.has(ref.toString())).map(([, value]) => value);
    survivingRoots.push(...Object.values(document.context.trailerInfo));
    const retained = referencedObjects(document.context, survivingRoots);
    for (const { entry } of removals) {
      const ref = entry.value instanceof PDFRef ? entry.value : document.context.getObjectRef(entry.annotation);
      if (ref && retained.has(ref.toString())) fail('EXTERNAL_INK_REFERENCE', '其他 PDF 对象仍引用本软件的批注，无法安全重建；请保留当前文件。');
    }
    for (const [key, ref] of candidates) if (!retained.has(key)) document.context.delete(ref);
    const sourceBytes = await document.save({ useObjectStreams: true, addDefaultPage: false, updateFieldAppearances: false });
    if (sourceBytes.length > MAX_SOURCE_BYTES) fail('FILE_SIZE', '当前 PDF 内容超过可安全保存的 256 MB，原文件未修改。');
    return sourceBytes;
  }
  async function open(input) {
    const inputBytes = bytes(input);
    const document = await loadDocument(inputBytes);
    const stored = document.catalog.get(KEY);
    if (!stored) {
      const hasLostInkData = document.getPages().some(page => {
        const annotations = page.node.Annots();
        if (!annotations) return false;
        for (let i = 0; i < annotations.size(); i++) {
          const annotation = annotations.lookup(i);
          if (ownedAnnotation(annotation)) return true;
        }
        return false;
      });
      if (hasLostInkData) fail('EDIT_DATA_MISSING', '此 PDF 保留了本软件的批注外观，但编辑数据已缺失，无法恢复笔迹。请使用原保存文件。');
      return { sourceBytes: inputBytes.slice(), pages: getPageMetrics(document), inkPages: document.getPages().map(() => []), documentId:documentId(), restored: false, ...bookmarkResult(document) };
    }
    try {
      const dictionary = document.context.lookup(stored);
      if (!(dictionary instanceof PDFDict)) fail('EDIT_DATA_INVALID', 'PDF 内本软件的编辑数据损坏。');
      const version = dictionary.lookupMaybe(PDFName.of('Version'), PDFNumber);
      if (!version || ![1, 2, 3, VERSION].includes(version.asNumber())) fail('EDIT_DATA_VERSION', '此 PDF 的笔迹版本不受当前版本支持，请勿覆盖原文件。');
      const formatVersion = version.asNumber();
      const sourceBytes = readStream(document, dictionary, 'Source', MAX_SOURCE_BYTES);
      const stateBytes = readStream(document, dictionary, 'State', MAX_STATE_BYTES);
      let state;
      try { state = JSON.parse(decoder.decode(stateBytes)); } catch (error) { fail('EDIT_DATA_INVALID', 'PDF 内的笔迹编辑数据无法读取。', error); }
      if (!state || state.format !== 'inkpdf' || state.version !== formatVersion) fail('EDIT_DATA_VERSION', 'PDF 编辑数据版本与目录不一致或不受支持，请保留原文件。');
      const original = await loadDocument(sourceBytes);
      if (original.catalog.has(KEY)) fail('EDIT_DATA_INVALID', 'PDF 内的原稿包含嵌套编辑数据，已停止恢复。');
      if (original.getPageCount() !== document.getPageCount()) fail('EDIT_DATA_INVALID', '保存后的 PDF 页面与内嵌原稿不一致。');
      const pages = validatePages(state.pages, getPageMetrics(original));
      validatePages(pages, getPageMetrics(document));
      const inkPages = validateInk(state.inkPages, pages, formatVersion);
      const currentBytes = await currentSource(document, original, dictionary, stored, pages, inkPages, formatVersion);
      return { sourceBytes: currentBytes, pages, inkPages, documentId:documentId(state.documentId), restored: true, ...bookmarkResult(document) };
    } catch (error) {
      if (error instanceof StorageError) throw error;
      fail('EDIT_DATA_INVALID', 'PDF 内含笔迹编辑数据，但恢复失败。请保留原文件。', error);
    }
  }

  function inverse(matrix) {
    const [a, b, c, d, e, f] = matrix, determinant = a * d - b * c;
    if (!Number.isFinite(determinant) || Math.abs(determinant) < 1e-12) fail('PAGE_GEOMETRY', '页面坐标无法转换。');
    return [d / determinant, -b / determinant, -c / determinant, a / determinant, (c * f - d * e) / determinant, (b * e - a * f) / determinant];
  }
  function circlePath(x, y, radius) {
    const k = radius * 0.552284749831;
    return [num(x + radius), num(y), 'm',
      num(x + radius), num(y + k), num(x + k), num(y + radius), num(x), num(y + radius), 'c',
      num(x - k), num(y + radius), num(x - radius), num(y + k), num(x - radius), num(y), 'c',
      num(x - radius), num(y - k), num(x - k), num(y - radius), num(x), num(y - radius), 'c',
      num(x + k), num(y - radius), num(x + radius), num(y - k), num(x + radius), num(y), 'c h f'].join(' ');
  }
  function smoothPath(points) {
    let path = num(points[0][0]) + ' ' + num(points[0][1]) + ' m\n';
    let previous = points[0];
    for (let i = 1; i < points.length - 1; i++) {
      const control = points[i], middle = [(control[0] + points[i + 1][0]) / 2, (control[1] + points[i + 1][1]) / 2];
      const first = [previous[0] + 2 / 3 * (control[0] - previous[0]), previous[1] + 2 / 3 * (control[1] - previous[1])];
      const second = [middle[0] + 2 / 3 * (control[0] - middle[0]), middle[1] + 2 / 3 * (control[1] - middle[1])];
      path += [...first, ...second, ...middle].map(num).join(' ') + ' c\n';
      previous = middle;
    }
    const last = points[points.length - 1];
    return path + num(last[0]) + ' ' + num(last[1]) + ' l S';
  }
  function ellipsePath(x, y, width, height) {
    const rx = width / 2, ry = height / 2, cx = x + rx, cy = y + ry;
    const k = 0.552284749831;
    return [
      [cx + rx, cy].map(num).join(' ') + ' m',
      [cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry].map(num).join(' ') + ' c',
      [cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy].map(num).join(' ') + ' c',
      [cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry].map(num).join(' ') + ' c',
      [cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy].map(num).join(' ') + ' c h',
    ].join('\n');
  }
  function shapeContent(stroke) {
    const [first, last] = stroke.points;
    const color = rgb(stroke.color).map(num).join(' ');
    // Each complete shape is painted inside the existing transparency group,
    // so alpha is applied once where an arrow head overlaps its shaft.
    let content = color + ' RG\n' + color + ' rg\n' + num(stroke.width) + ' w\n1 J 1 j\n';
    if (stroke.tool === 'line' || stroke.tool === 'arrow') {
      content += first.slice(0, 2).map(num).join(' ') + ' m\n' + last.slice(0, 2).map(num).join(' ') + ' l S';
      if (stroke.tool === 'arrow') {
        const dx = last[0] - first[0], dy = last[1] - first[1], length = Math.hypot(dx, dy);
        const headLength = Math.min(Math.max(8, stroke.width * 4), length * 0.6);
        const halfWidth = Math.min(Math.max(4, stroke.width * 2), headLength * 0.5);
        const ux = dx / length, uy = dy / length;
        const baseX = last[0] - ux * headLength, baseY = last[1] - uy * headLength;
        const triangle = [[last[0], last[1]], [baseX - uy * halfWidth, baseY + ux * halfWidth], [baseX + uy * halfWidth, baseY - ux * halfWidth]];
        content += '\n' + triangle.map((point, index) => point.map(num).join(' ') + (index ? ' l' : ' m')).join('\n') + '\nh f';
      }
      return content;
    }
    const x = Math.min(first[0], last[0]), y = Math.min(first[1], last[1]);
    const width = Math.abs(last[0] - first[0]), height = Math.abs(last[1] - first[1]);
    const path = stroke.tool === 'rectangle' ? [x, y, width, height].map(num).join(' ') + ' re' : ellipsePath(x, y, width, height);
    if (stroke.fill !== 'none') content += rgb(stroke.fill).map(num).join(' ') + ' rg\n' + path + ' B';
    else content += path + ' S';
    return content;
  }
  function strokeContent(stroke) {
    if (SHAPE_TOOLS.includes(stroke.tool)) return shapeContent(stroke);
    if (REVIEW_TOOLS.includes(stroke.tool)) {
      const color = rgb(stroke.color).map(num).join(' ');
      if (stroke.tool === 'text-highlight') return color + ' rg\n' + Geometry.reviewQuads(stroke.points).map(quad => quad.map((point,index)=>point.slice(0,2).map(num).join(' ')+(index?' l':' m')).join('\n')+' h f').join('\n');
      return color + ' RG\n' + num(stroke.width) + ' w\n1 J 1 j\n' + Geometry.reviewPaths(stroke.tool,stroke.points,stroke.width).map(path=>path.points.map((point,index)=>point.map(num).join(' ')+(index?' l':' m')).join('\n')+' S').join('\n');
    }
    const points = stroke.points;
    const color = rgb(stroke.color).map(num).join(' ');
    let content = color + ' rg ' + color + ' RG\n1 J 1 j\n';
    const radiusAt = point => stroke.width * (stroke.pressure && stroke.tool !== 'highlighter' ? 0.25 + 1.25 * point[2] : 1) / 2;
    const first = points[0];
    if (points.length === 1 || points.every(point => Math.hypot(point[0] - first[0], point[1] - first[1]) < 0.1)) return content + circlePath(first[0], first[1], Math.max(0.3, radiusAt(first)));
    if (!stroke.pressure || stroke.tool === 'highlighter') return content + num(stroke.width) + ' w\n' + smoothPath(points);
    const left = [], right = [];
    points.forEach((point, i) => {
      const before = points[Math.max(0, i - 1)], after = points[Math.min(points.length - 1, i + 1)];
      const dx = after[0] - before[0], dy = after[1] - before[1], length = Math.hypot(dx, dy) || 1, radius = radiusAt(point);
      left.push([point[0] - dy / length * radius, point[1] + dx / length * radius]);
      right.push([point[0] + dy / length * radius, point[1] - dx / length * radius]);
    });
    const polygon = left.concat(right.reverse());
    content += polygon.map((point, i) => point.map(num).join(' ') + (i ? ' l' : ' m')).join('\n') + '\nh f\n';
    const last = points[points.length - 1];
    return content + circlePath(first[0], first[1], radiusAt(first)) + '\n' + circlePath(last[0], last[1], radiusAt(last));
  }
  function localBounds(stroke) {
    // Covers cap radii, arrowheads and review waves before the object matrix;
    // the enclosing page appearance is the final clipping boundary.
    let left=Infinity,top=Infinity,right=-Infinity,bottom=-Infinity;
    for(const point of stroke.points){left=Math.min(left,point[0]);top=Math.min(top,point[1]);right=Math.max(right,point[0]);bottom=Math.max(bottom,point[1]);}
    const padding=stroke.width*2+16;return [left-padding,top-padding,right+padding,bottom+padding];
  }
  async function textImage(document, stroke) {
    let decoded;
    try { decoded=await Text.decodeRaster(stroke); }
    catch(error){fail('TEXT_APPEARANCE','无法读取保存所需的文字外观：'+error.message,error);}
    const dictionary={Type:'XObject',Subtype:'Image',Width:decoded.width,Height:decoded.height,BitsPerComponent:8,ColorSpace:'DeviceRGB'};
    if(decoded.alpha){
      dictionary.SMask=document.context.register(document.context.flateStream(decoded.alpha,{Type:'XObject',Subtype:'Image',Width:decoded.width,Height:decoded.height,BitsPerComponent:8,ColorSpace:'DeviceGray'}));
    }
    const image=document.context.register(document.context.flateStream(decoded.rgb,dictionary));
    const box=Text.descriptor(stroke);
    // PNG's first row is the top row. Local editing coordinates also run down;
    // invert the image plane before applying the page's viewport-to-PDF map.
    return { resources:{XObject:{TextImage:image}}, content:'q\n'+[box.width,0,0,-box.height,box.left,box.top+box.height].map(num).join(' ')+' cm\n/TextImage Do\nQ' };
  }
  async function paintPage(document, page, metric, strokes, formatVersion = VERSION) {
    if (!strokes.length) return;
    const mapping = inverse(metric.transform), graphics = new Map();
    const [x0, y0, x1, y1] = metric.viewBox;
    // Appearance BBox is local to the unrotated annotation Rect, whereas points
    // use the rotated/cropped PDF.js viewport. Convert both rotation and origin.
    mapping[4] -= x0;
    mapping[5] -= y0;
    const forms = {}, graphicStates = {}, content = [];
    for (let index=0;index<strokes.length;index++) {
      const stroke=strokes[index],blend = stroke.tool === 'highlighter' || stroke.tool === 'text-highlight' ? 'Multiply' : 'Normal';
      const stateKey = stroke.opacity + '/' + blend;
      let graphicName = graphics.get(stateKey);
      if (!graphicName) {
        const stateRef = document.context.register(document.context.obj({ Type: 'ExtGState', ca: stroke.opacity, CA: stroke.opacity, BM: blend }));
        graphicName = 'InkState' + graphics.size;
        graphicStates[graphicName] = stateRef;
        graphics.set(stateKey, graphicName);
      }
      // A transparency group applies alpha once to the whole stroke, including overlapping caps.
      const text=TEXT_TOOLS.includes(stroke.tool)?await textImage(document,stroke):null;
      const form = document.context.flateStream(encoder.encode(text?text.content:strokeContent(stroke)), {
        Type: 'XObject', Subtype: 'Form', FormType: 1,
        BBox: formatVersion>=4?localBounds(stroke):[0, 0, metric.width, metric.height], Resources: text?text.resources:{},
        Group: { S: 'Transparency', CS: 'DeviceRGB', I: true },
      });
      const formName = 'InkStroke' + index;
      forms[formName] = document.context.register(form);
      const matrix=formatVersion>=4?Geometry.normalizeMatrix(stroke.transform):null;
      content.push('q\n' + mapping.map(num).join(' ') + ' cm\n' + (matrix?matrix.map(matrixNum).join(' ')+' cm\n':'') + '/' + graphicName + ' gs\n/' + formName + ' Do\nQ');
    }
    const appearance = document.context.register(document.context.flateStream(encoder.encode(content.join('\n')), {
      Type: 'XObject', Subtype: 'Form', FormType: 1,
      BBox: [0, 0, x1 - x0, y1 - y0],
      Resources: { XObject: forms, ExtGState: graphicStates },
    }));
    // Appending a printable annotation keeps handwritten ink above pre-existing
    // Widget/Text annotations, matching the live SVG overlay and print output.
    const annotation = document.context.register(document.context.obj({
      Type: 'Annot', Subtype: 'Stamp', Rect: metric.viewBox.slice(), F: 4,
      P: page.ref, AP: { N: appearance },
      NM: PDFString.of('InkPDF-page-' + page.ref.objectNumber),
      // This label is part of the strict appearance comparison. Preserve the
      // historical value when reconstructing a v1/v2 file's expected Stamp.
      Contents: PDFString.of(formatVersion >= 3 ? 'ProperNotes for Windows annotations' : 'InkPDF handwritten annotations'),
      InkPDFOwned: true,
    }));
    page.node.addAnnot(annotation);
  }
  async function save(options) {
    if (!options || !options.sourceBytes) fail('INVALID_INPUT', '没有可保存的 PDF。');
    const sourceBytes = bytes(options.sourceBytes);
    if (sourceBytes.length > MAX_SOURCE_BYTES) fail('FILE_SIZE', '原始 PDF 超过当前支持的 256 MB。');
    const document = await loadDocument(sourceBytes);
    if (document.catalog.has(KEY)) fail('SOURCE_NOT_ORIGINAL', '保存必须使用干净原稿，不能再次嵌入已批注 PDF。请重新打开文件。');
    if (document.getPages().some(page => annotationEntries(document, page).length)) fail('SOURCE_NOT_ORIGINAL', '保存基底仍包含本软件的批注外观，已停止保存以避免重复叠加。请重新打开原文件。');
    const expected = getPageMetrics(document);
    const pages = validatePages(options.pages || expected, expected);
    const inkPages = validateInk(options.inkPages, pages);
    const changedBookmarks = applyBookmarkEdits(document, options.bookmarkEdits === undefined ? [] : options.bookmarkEdits);
    // Embed the clean document after its explicit standard-outline edits. Its
    // page identities and geometry stay unchanged, and no private bookmark
    // database is needed to reopen the PDF in this or another reader.
    const embeddedSource = changedBookmarks ? await document.save({ useObjectStreams: true, addDefaultPage: false, updateFieldAppearances: false }) : sourceBytes;
    if (embeddedSource.length > MAX_SOURCE_BYTES) fail('FILE_SIZE', '书签修改后的 PDF 内容超过可安全保存的 256 MB，原文件未修改。');
    const state = { format: 'inkpdf', version: VERSION, documentId:documentId(options.documentId), pages, inkPages };
    const stateBytes = encoder.encode(JSON.stringify(state));
    if (stateBytes.length > MAX_STATE_BYTES) fail('EDIT_DATA_SIZE', '笔迹编辑数据超过当前支持的 64 MB。');
    const pageObjects = document.getPages();
    for(let index=0;index<pageObjects.length;index++)await paintPage(document,pageObjects[index],pages[index],inkPages[index]);
    const sourceRef = document.context.register(document.context.flateStream(embeddedSource));
    const stateRef = document.context.register(document.context.flateStream(stateBytes));
    document.catalog.set(KEY, document.context.register(document.context.obj({ Type: 'InkPDFData', Version: VERSION, Source: sourceRef, State: stateRef })));
    try {
      return await document.save({ useObjectStreams: true, addDefaultPage: false, updateFieldAppearances: false });
    } catch (error) { fail('SAVE_FAILED', '无法生成 PDF，原文件未被修改。', error); }
  }
  return Object.freeze({ open, save, getPageMetrics, StorageError, version: VERSION, limits: Object.freeze({ points: MAX_POINTS, pages: MAX_PAGES, stateBytes: MAX_STATE_BYTES }) });
});
