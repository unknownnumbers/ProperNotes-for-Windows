/* Pure standard-PDF outline editing model. No PDF, filesystem or browser I/O. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.ProperNotesBookmarks = factory();
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const MAX_BOOKMARKS = 5000, MAX_DEPTH = 48, MAX_EDITS = 10000;
  const TARGET_TYPES = ['page', 'named', 'external', 'unsupported'];
  class BookmarkError extends Error {
    constructor(code, message) { super(message);this.name = 'BookmarkError';this.code = code; }
  }
  function fail(code, message) { throw new BookmarkError(code, message); }
  function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
  function validId(value) { return typeof value === 'string' && /^[A-Za-z0-9_-]{1,160}$/.test(value); }
  function validPageCount(value) { return Number.isInteger(value) && value > 0 && value <= 10000; }
  function title(value) {
    if (typeof value !== 'string' || !value.trim() || value.length > 512 || /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(value)) fail('BOOKMARK_TITLE', '书签标题不能为空、包含控制字符或超过 512 个字符。');
    return value.trim();
  }
  function createId() {
    try { if (globalThis.crypto?.randomUUID) return 'bookmark-local-' + globalThis.crypto.randomUUID(); } catch (_) { /* Use an editing identifier fallback. */ }
    return 'bookmark-local-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2) + '-' + Math.random().toString(36).slice(2);
  }
  function cloneTree(input, pageCount) {
    if (!validPageCount(pageCount) || !Array.isArray(input)) fail('BOOKMARK_MODEL', '书签与文档页面信息无效。');
    const ids = new Set(), seen = new Set();let total = 0;
    function copy(nodes, depth) {
      if (!Array.isArray(nodes) || (nodes.length && depth >= MAX_DEPTH)) fail('BOOKMARK_LIMIT', '书签层级超过当前编辑范围。');
      return nodes.map(node => {
        if (++total > MAX_BOOKMARKS) fail('BOOKMARK_LIMIT', '书签数量超过当前编辑范围。');
        if (!object(node) || seen.has(node) || !validId(node.id) || ids.has(node.id) || typeof node.title !== 'string' || node.title.length > 4096
          || typeof node.expanded !== 'boolean' || (node.pageIndex !== null && (!Number.isInteger(node.pageIndex) || node.pageIndex < 0 || node.pageIndex >= pageCount))
          || !TARGET_TYPES.includes(node.targetType) || (node.namedDestination !== undefined && (typeof node.namedDestination !== 'string' || node.namedDestination.length > 4096))) fail('BOOKMARK_MODEL', '书签树包含无效或重复节点。');
        seen.add(node);ids.add(node.id);
        const children = copy(node.children, depth + 1);
        return { id: node.id, title: node.title, pageIndex: node.pageIndex, expanded: children.length ? node.expanded : false, children, targetType: node.targetType, ...(node.namedDestination === undefined ? {} : { namedDestination: node.namedDestination }) };
      });
    }
    return copy(input, 0);
  }
  function applyEdits(input, edits, { pageCount } = {}) {
    if (!Array.isArray(edits) || edits.length > MAX_EDITS) fail('BOOKMARK_EDITS', '书签操作记录无效或超过当前编辑范围。');
    const tree = cloneTree(input, pageCount), entries = new Map();
    function index(nodes, parentId, depth) {
      nodes.forEach(node => { entries.set(node.id, { node, siblings: nodes, parentId, depth });index(node.children, node.id, depth + 1); });
    }
    index(tree, null, 0);
    function requireEntry(id) {
      if (!validId(id) || !entries.has(id)) fail('BOOKMARK_MISSING', '要修改的书签已不存在，请重新选择。');
      return entries.get(id);
    }
    function removeEntries(node) { node.children.forEach(removeEntries);entries.delete(node.id); }
    for (const edit of edits) {
      if (!object(edit) || typeof edit.type !== 'string') fail('BOOKMARK_EDITS', '书签操作记录无效。');
      if (edit.type === 'add') {
        if (!validId(edit.id) || !edit.id.startsWith('bookmark-local-') || entries.has(edit.id)) fail('BOOKMARK_ID', '新书签标识无效或已存在。');
        if (!Number.isInteger(edit.pageIndex) || edit.pageIndex < 0 || edit.pageIndex >= pageCount) fail('BOOKMARK_PAGE', '请选择文档中存在的页面。');
        if (edit.parentId !== null && !validId(edit.parentId)) fail('BOOKMARK_PARENT', '请选择有效的父书签或顶层。');
        const parent = edit.parentId === null ? null : requireEntry(edit.parentId);
        const siblings = parent ? parent.node.children : tree, depth = parent ? parent.depth + 1 : 0;
        const position = edit.index === undefined ? siblings.length : edit.index;
        if (!Number.isInteger(position) || position < 0 || position > siblings.length) fail('BOOKMARK_POSITION', '书签插入位置无效。');
        if (entries.size >= MAX_BOOKMARKS || depth >= MAX_DEPTH) fail('BOOKMARK_LIMIT', '书签数量或层级超过当前编辑范围。');
        const node = { id: edit.id, title: title(edit.title), pageIndex: edit.pageIndex, expanded: false, children: [], targetType: 'page' };
        siblings.splice(position, 0, node);entries.set(node.id, { node, siblings, parentId: edit.parentId, depth });
      } else if (edit.type === 'rename') {
        requireEntry(edit.id).node.title = title(edit.title);
      } else if (edit.type === 'set-target') {
        if (!Number.isInteger(edit.pageIndex) || edit.pageIndex < 0 || edit.pageIndex >= pageCount) fail('BOOKMARK_PAGE', '请选择文档中存在的页面。');
        const node = requireEntry(edit.id).node;
        node.pageIndex = edit.pageIndex;node.targetType = 'page';
        delete node.namedDestination;
      } else if (edit.type === 'delete') {
        const entry = requireEntry(edit.id);entry.siblings.splice(entry.siblings.indexOf(entry.node), 1);removeEntries(entry.node);
      } else if (edit.type === 'set-expanded') {
        if (typeof edit.expanded !== 'boolean') fail('BOOKMARK_EXPANDED', '书签展开状态无效。');
        const node = requireEntry(edit.id).node;node.expanded = node.children.length ? edit.expanded : false;
      } else fail('BOOKMARK_EDITS', '不支持此书签操作，已停止保存。');
    }
    return tree;
  }
  return Object.freeze({ applyEdits, createId, BookmarkError, MAX_BOOKMARKS, MAX_DEPTH, MAX_EDITS });
});
