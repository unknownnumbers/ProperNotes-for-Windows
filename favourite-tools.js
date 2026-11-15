/* Single-toolbar preset data operations. No PDF, engine or storage side effects. */
const SHAPE_TOOLS = ['line', 'arrow', 'rectangle', 'ellipse'];
export const FAVOURITE_TEXT_TOOLS = Object.freeze(['textbox', 'note']);
export const FAVOURITE_REVIEW_TOOLS = Object.freeze(['text-highlight', 'text-underline', 'text-squiggle', 'text-strike']);
export const FAVOURITE_TOOLS = Object.freeze(['pen', 'highlighter', ...SHAPE_TOOLS, ...FAVOURITE_TEXT_TOOLS, ...FAVOURITE_REVIEW_TOOLS]);
const TOOLS = FAVOURITE_TOOLS;
const FILLABLE_TOOLS = ['rectangle', 'ellipse', ...FAVOURITE_TEXT_TOOLS];
const TOOL_NAMES = { pen: '手写笔', highlighter: '荧光笔', line: '直线', arrow: '箭头', rectangle: '矩形', ellipse: '椭圆', textbox:'文本框', note:'便笺', 'text-highlight':'文本高亮', 'text-underline':'下划线', 'text-squiggle':'波浪线', 'text-strike':'删除线' };
const ID_PATTERN = /^[A-Za-z0-9_-]{1,160}$/;
const DEFAULT_STYLES = {
  pen: { color: '#24262b', width: 2.5, opacity: 1, pressure: true },
  highlighter: { color: '#f3ce46', width: 15, opacity: 0.36, pressure: false },
  line: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  arrow: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  rectangle: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  ellipse: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  textbox: { color:'#24262b', width:1, opacity:1, pressure:false, fill:'none', fontSize:16, fontFamily:'sans', bold:false, italic:false },
  note: { color:'#24262b', width:1, opacity:1, pressure:false, fill:'#fff2a8', fontSize:16, fontFamily:'sans', bold:false, italic:false },
  'text-highlight': { color:'#f3ce46', width:1, opacity:0.36, pressure:false, fill:'none' },
  'text-underline': { color:'#24262b', width:1.5, opacity:1, pressure:false, fill:'none' },
  'text-squiggle': { color:'#24262b', width:1.5, opacity:1, pressure:false, fill:'none' },
  'text-strike': { color:'#24262b', width:1.5, opacity:1, pressure:false, fill:'none' },
};
let idSequence = 0;

function object(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function fail(message) {
  throw new TypeError(message);
}
function validTool(tool) {
  if (!TOOLS.includes(tool)) fail('请选择支持收藏的笔、形状、文字批注或文本审阅工具。');
  return tool;
}
function validId(id) {
  return typeof id === 'string' && ID_PATTERN.test(id);
}
function nextId(used = new Set()) {
  try {
    const uuid = globalThis.crypto?.randomUUID?.();
    if (validId(uuid) && !used.has(uuid)) return uuid;
  } catch {
    // A restricted context can omit crypto.randomUUID; IDs are not security tokens.
  }
  let id;
  do {
    idSequence += 1;
    id = 'fav-' + Date.now().toString(36) + '-' + idSequence.toString(36)
      + '-' + Math.random().toString(36).slice(2, 12);
  } while (used.has(id));
  return id;
}
function color(value) {
  if (typeof value !== 'string' || !/^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(value)) {
    fail('请选择有效的收藏颜色。');
  }
  const hex = value.slice(1).toLowerCase();
  return '#' + (hex.length === 3 ? hex.split('').map(character => character + character).join('') : hex);
}
function finiteIn(value, min, max, message) {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max) fail(message);
  return value;
}
function style(input, tool) {
  if (!object(input)) fail('无法读取收藏工具的样式。');
  const defaults = DEFAULT_STYLES[validTool(tool)];
  // Undefined fields are absent, so switching to a new tool can use its own
  // defaults. All other annotation fields are excluded from the output below.
  const value = { ...defaults };
  for (const key of ['color', 'width', 'opacity', 'pressure', 'fill', 'fontSize', 'fontFamily', 'bold', 'italic']) if (input[key] !== undefined) value[key] = input[key];
  if (typeof value.pressure !== 'boolean') fail('收藏的压感设置无效。');
  const result = {
    color: color(value.color),
    width: finiteIn(value.width, 0.1, 100, '收藏粗细须在 0.1 到 100 之间。'),
    opacity: finiteIn(value.opacity, 0, 1, '收藏透明度须在 0 到 1 之间。'),
    pressure: ['pen', 'highlighter'].includes(tool) ? value.pressure : false,
  };
  if ([...SHAPE_TOOLS, ...FAVOURITE_TEXT_TOOLS, ...FAVOURITE_REVIEW_TOOLS].includes(tool)) {
    result.fill = FILLABLE_TOOLS.includes(tool) && value.fill !== 'none' ? color(value.fill) : 'none';
  }
  if (FAVOURITE_TEXT_TOOLS.includes(tool)) {
    result.fontSize = finiteIn(value.fontSize, 4, 240, '收藏字号须在 4 到 240 之间。');
    if (!['sans', 'serif', 'mono'].includes(value.fontFamily)) fail('请选择支持的收藏字体族。');
    if (typeof value.bold !== 'boolean' || typeof value.italic !== 'boolean') fail('收藏的粗体或斜体设置无效。');
    Object.assign(result, { fontFamily:value.fontFamily, bold:value.bold, italic:value.italic });
  }
  return result;
}
/** Fresh defaults; callers cannot mutate the module's preset templates. */
export function defaultFavouriteStyle(tool) {
  return style({}, validTool(tool));
}
/** Validated style only: never returns name, preset ID or PDF annotation data. */
export function favouriteStyle(tool, input = {}) {
  return style(input, validTool(tool));
}
function defaultName(tool, values) {
  return TOOL_NAMES[tool] + ' · ' + (FAVOURITE_TEXT_TOOLS.includes(tool) ? '字号 ' + values.fontSize : values.width + ' px');
}
function name(value, fallback) {
  if (value === undefined) return fallback;
  if (typeof value !== 'string') fail('收藏名称必须是文字。');
  const result = value.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 160);
  if (!result) fail('请输入收藏名称。');
  return result;
}
function copyFavourite(input, reservedIds = new Set()) {
  if (!object(input)) fail('无法读取收藏工具。');
  const tool = validTool(input.tool);
  const values = style(input, tool);
  return {
    id: validId(input.id) ? input.id : nextId(reservedIds),
    tool,
    ...values,
    name: name(input.name, defaultName(tool, values)),
  };
}

/** Assign IDs to legacy entries and replace duplicate IDs without mutating the list. */
export function ensureFavouriteIds(list) {
  if (!Array.isArray(list)) fail('收藏列表无效。');
  const reserved = new Set(list.filter(object).map(input => input.id).filter(validId));
  const used = new Set();
  return list.map(input => {
    const favourite = copyFavourite(input, reserved);
    if (used.has(favourite.id)) favourite.id = nextId(reserved);
    used.add(favourite.id);
    reserved.add(favourite.id);
    return favourite;
  });
}

/** Capture a tool preset. Calling this never changes the active tool or selected ink. */
export function createFavourite(tool, input = {}, label) {
  validTool(tool);
  const values = style(input, tool);
  return { id: nextId(), tool, ...values, name: name(label, defaultName(tool, values)) };
}

/** Editing a preset retains its identity; only the validated tool and preset fields may change. */
export function updateFavourite(input, patch) {
  const original = copyFavourite(input);
  if (!object(patch)) fail('收藏修改内容无效。');
  const tool = Object.prototype.hasOwnProperty.call(patch, 'tool') ? validTool(patch.tool) : original.tool;
  const candidate = {};
  for (const key of ['color', 'width', 'opacity', 'pressure', 'fill', 'fontSize', 'fontFamily', 'bold', 'italic']) {
    if (Object.prototype.hasOwnProperty.call(patch, key) && patch[key] !== undefined) candidate[key] = patch[key];
    else if (original[key] !== undefined) {
      const compatible = key === 'pressure' ? ['pen','highlighter'].includes(original.tool) && ['pen','highlighter'].includes(tool)
        : key === 'fill' ? FILLABLE_TOOLS.includes(original.tool) && FILLABLE_TOOLS.includes(tool)
          : ['fontSize','fontFamily','bold','italic'].includes(key) ? FAVOURITE_TEXT_TOOLS.includes(original.tool) && FAVOURITE_TEXT_TOOLS.includes(tool) : true;
      if (compatible) candidate[key] = original[key];
    }
  }
  const values = style(candidate, tool);
  return {
    id: original.id,
    tool,
    ...values,
    name: Object.prototype.hasOwnProperty.call(patch, 'name') ? name(patch.name, original.name) : original.name,
  };
}

/** A copy has its own ID, so editing it does not affect the original preset. */
export function duplicateFavourite(input) {
  const original = copyFavourite(input);
  const suffix = ' 副本';
  return {
    ...original,
    id: nextId(new Set([original.id])),
    name: original.name.slice(0, 160 - suffix.length).trimEnd() + suffix,
  };
}

function indexOf(list, id) {
  return list.findIndex(favourite => favourite.id === id);
}

export function removeFavourite(list, id) {
  const result = ensureFavouriteIds(list);
  const index = indexOf(result, id);
  if (index >= 0) result.splice(index, 1);
  return result;
}

/** Move by a signed integer offset; end positions clamp to the list boundaries. */
export function moveFavourite(list, id, offset) {
  if (!Number.isSafeInteger(offset)) fail('收藏排序位置无效。');
  const result = ensureFavouriteIds(list);
  const from = indexOf(result, id);
  if (from < 0) return result;
  const to = Math.max(0, Math.min(result.length - 1, from + offset));
  if (from === to) return result;
  const [favourite] = result.splice(from, 1);
  result.splice(to, 0, favourite);
  return result;
}

/** Compare complete drawing settings, without relying on object identity or names. */
export function favouriteMatchesStyle(input, tool, inputStyle) {
  if (!object(input) || input.tool !== tool || !TOOLS.includes(tool)) return false;
  try {
    const first = style(input, tool), second = style(inputStyle, tool);
    return Object.keys(first).every(key => first[key] === second[key]);
  } catch {
    return false;
  }
}
