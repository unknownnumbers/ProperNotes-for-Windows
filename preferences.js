/* UI preferences only. PDF content and editing data never enter this store. */
import { normalizeInkPreferences, validInkPreferences, normalizeNavigationPreferences, validNavigationPreferences } from './input-preferences.js';
import { SHORTCUT_COMMANDS, normalizeShortcuts, validateShortcuts } from './keyboard-shortcuts.js';
import { FAVOURITE_TOOLS, FAVOURITE_REVIEW_TOOLS, defaultFavouriteStyle } from './favourite-tools.js';
export const PREFERENCES_KEY = 'inkpdf.preferences.v2';
export const LEGACY_PREFERENCES_KEY = 'inkpdf.preferences.v1';
export const PREFERENCES_VERSION = 2;

const MAX_RECORD_LENGTH = 1024 * 1024;
export const MAX_FAVOURITES = 512;
const CORE_TOOLS = ['pen', 'highlighter', 'eraser', 'select', 'pan'];
const SHAPE_TOOLS = ['line', 'arrow', 'rectangle', 'ellipse'];
const TEXT_TOOLS = ['textbox', 'note'];
const REVIEW_TOOLS = FAVOURITE_REVIEW_TOOLS;
const DRAWING_TOOLS = FAVOURITE_TOOLS;
const TOOLS = [...CORE_TOOLS, ...SHAPE_TOOLS, ...TEXT_TOOLS, ...REVIEW_TOOLS];
const LAYOUTS = ['single-continuous','double-continuous','single-page','double-page'];
const TOOL_NAMES = { pen: '手写笔', highlighter: '荧光笔', line: '直线', arrow: '箭头', rectangle: '矩形', ellipse: '椭圆', textbox:'文本框', note:'便笺', 'text-highlight':'文本高亮', 'text-underline':'下划线', 'text-squiggle':'波浪线', 'text-strike':'删除线' };
const THEMES = ['system', 'light', 'dark'];
const SIDEBARS = ['pages', 'bookmarks', 'annotations'];
const FALLBACK_STYLES = {
  pen: { color: '#24262b', width: 2.5, opacity: 1, pressure: true },
  highlighter: { color: '#f3ce46', width: 15, opacity: 0.36, pressure: false },
  eraser: { color: '#24262b', width: 12, opacity: 1, pressure: false },
  select: { color: '#e25752', width: 2.5, opacity: 1, pressure: true },
  pan: { color: '#24262b', width: 2.5, opacity: 1, pressure: false },
  line: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  arrow: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  rectangle: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  ellipse: { color: '#24262b', width: 2.5, opacity: 1, pressure: false, fill: 'none' },
  textbox: { color:'#24262b',width:1,opacity:1,pressure:false,fill:'none',fontSize:16,fontFamily:'sans',bold:false,italic:false },
  note: { color:'#24262b',width:1,opacity:1,pressure:false,fill:'#fff2a8',fontSize:16,fontFamily:'sans',bold:false,italic:false },
  ...Object.fromEntries(REVIEW_TOOLS.map(tool=>[tool,defaultFavouriteStyle(tool)])),
};

function object(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function finiteIn(value, min, max) {
  return typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
}
function validColor(value) {
  return typeof value === 'string' && /^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(value);
}
function color(value, fallback) {
  if (!validColor(value)) return fallback;
  const hex = value.slice(1).toLowerCase();
  return '#' + (hex.length === 3 ? hex.split('').map(character => character + character).join('') : hex);
}
function validFill(value) {
  return value === 'none' || validColor(value);
}
function style(value, fallback, tool) {
  const input = object(value) ? value : {};
  const result = {
    color: color(input.color, fallback.color),
    width: finiteIn(input.width, 0.1, 100) ? input.width : fallback.width,
    opacity: finiteIn(input.opacity, 0, 1) ? input.opacity : fallback.opacity,
    pressure: [...SHAPE_TOOLS,...TEXT_TOOLS,...REVIEW_TOOLS].includes(tool) ? false : typeof input.pressure === 'boolean' ? input.pressure : fallback.pressure,
  };
  if ([...SHAPE_TOOLS,...TEXT_TOOLS,...REVIEW_TOOLS].includes(tool)) {
    const supplied = input.fill === undefined ? fallback.fill : input.fill;
    result.fill = ['rectangle', 'ellipse',...TEXT_TOOLS].includes(tool) && validColor(supplied) ? color(supplied, 'none') : 'none';
  }
  if(TEXT_TOOLS.includes(tool))Object.assign(result,{fontSize:finiteIn(input.fontSize,4,240)?input.fontSize:fallback.fontSize,fontFamily:['sans','serif','mono'].includes(input.fontFamily)?input.fontFamily:fallback.fontFamily,bold:typeof input.bold==='boolean'?input.bold:fallback.bold,italic:typeof input.italic==='boolean'?input.italic:fallback.italic});
  return result;
}
function validStyle(value, tool) {
  return object(value) && validColor(value.color) && finiteIn(value.width, 0.1, 100)
    && finiteIn(value.opacity, 0, 1) && typeof value.pressure === 'boolean'
    && (![...SHAPE_TOOLS,...TEXT_TOOLS,...REVIEW_TOOLS].includes(tool) || value.pressure === false)
    && (value.fill === undefined || (['line', 'arrow',...REVIEW_TOOLS].includes(tool) ? value.fill === 'none' : validFill(value.fill)))
    && (!TEXT_TOOLS.includes(tool)||(finiteIn(value.fontSize,4,240)&&['sans','serif','mono'].includes(value.fontFamily)&&typeof value.bold==='boolean'&&typeof value.italic==='boolean'));
}
function favouriteName(value, tool, width) {
  if (typeof value !== 'string') return TOOL_NAMES[tool] + ' · ' + width + ' px';
  const name = value.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 160);
  return name || TOOL_NAMES[tool] + ' · ' + width + ' px';
}
function validId(value) {
  return typeof value === 'string' && /^[A-Za-z0-9_-]{1,160}$/.test(value);
}
function favourites(value, styles) {
  if (!Array.isArray(value)) return [];
  const result = [], assigned = new Set();
  // Reserve existing IDs first so a migrated ID cannot replace a later entry's ID.
  const reserved = new Set(value.filter(entry => object(entry) && validId(entry.id)).map(entry => entry.id));
  for (let index = 0; index < value.length; index++) {
    const entry = value[index];
    if (!object(entry) || !DRAWING_TOOLS.includes(entry.tool)) continue;
    const safeStyle = style(entry, styles[entry.tool], entry.tool);
    let id = validId(entry.id) && !assigned.has(entry.id) ? entry.id : null;
    if (!id) {
      const base = 'fav-legacy-' + (index + 1);
      id = base;
      let suffix = 2;
      while (reserved.has(id) || assigned.has(id)) id = base + '-' + suffix++;
    }
    assigned.add(id);
    result.push({ id, tool: entry.tool, ...safeStyle, name: favouriteName(entry.name, entry.tool, safeStyle.width) });
    if (result.length === MAX_FAVOURITES) break;
  }
  return result;
}
function validFavourite(value) {
  return validStyle(value, value?.tool) && DRAWING_TOOLS.includes(value.tool)
    && (value.id === undefined || validId(value.id))
    && typeof value.name === 'string' && value.name.length > 0 && value.name.length <= 160
    && !/[\u0000-\u001f\u007f]/.test(value.name);
}

/** Make a fresh, whitelisted snapshot. Unknown state fields (including PDF data) are excluded. */
export function normalizePreferences(input, defaults = {}) {
  const seed = object(defaults) ? defaults : {};
  const value = object(input) ? input : {};
  const baseStyles = {}, styles = {};
  for (const tool of TOOLS) {
    baseStyles[tool] = style(object(seed.styles) ? seed.styles[tool] : null, FALLBACK_STYLES[tool], tool);
    styles[tool] = style(object(value.styles) ? value.styles[tool] : null, baseStyles[tool], tool);
  }
  return {
    ink:normalizeInkPreferences(value.ink,normalizeInkPreferences(seed.ink)),
    navigation:normalizeNavigationPreferences(value.navigation,normalizeNavigationPreferences(seed.navigation)),
    shortcuts:normalizeShortcuts(value.shortcuts,normalizeShortcuts(seed.shortcuts)),
    layout:LAYOUTS.includes(value.layout)?value.layout:LAYOUTS.includes(seed.layout)?seed.layout:'single-continuous',
    theme: THEMES.includes(value.theme) ? value.theme : THEMES.includes(seed.theme) ? seed.theme : 'system',
    dock: ['left', 'right'].includes(value.dock) ? value.dock : seed.dock === 'right' ? 'right' : 'left',
    sidebar: SIDEBARS.includes(value.sidebar) ? value.sidebar : SIDEBARS.includes(seed.sidebar) ? seed.sidebar : 'pages',
    showSidebar: typeof value.showSidebar === 'boolean' ? value.showSidebar : typeof seed.showSidebar === 'boolean' ? seed.showSidebar : true,
    showProperties: typeof value.showProperties === 'boolean' ? value.showProperties : typeof seed.showProperties === 'boolean' ? seed.showProperties : true,
    zoomLocked: typeof value.zoomLocked === 'boolean' ? value.zoomLocked : typeof seed.zoomLocked === 'boolean' ? seed.zoomLocked : false,
    annotationsVisible: typeof value.annotationsVisible === 'boolean' ? value.annotationsVisible : typeof seed.annotationsVisible === 'boolean' ? seed.annotationsVisible : true,
    favouritesCollapsed: typeof value.favouritesCollapsed === 'boolean' ? value.favouritesCollapsed : typeof seed.favouritesCollapsed === 'boolean' ? seed.favouritesCollapsed : false,
    sidebarWidth: finiteIn(value.sidebarWidth, 112, 360) ? value.sidebarWidth : finiteIn(seed.sidebarWidth, 112, 360) ? seed.sidebarWidth : 143,
    propertiesWidth: finiteIn(value.propertiesWidth, 180, 420) ? value.propertiesWidth : finiteIn(seed.propertiesWidth, 180, 420) ? seed.propertiesWidth : 205,
    styles,
    // An intentionally empty collection is a valid preference, not a request for defaults.
    favourites: favourites(Array.isArray(value.favourites) ? value.favourites : seed.favourites, styles),
  };
}

function validPreferences(value) {
  return object(value) && THEMES.includes(value.theme) && ['left', 'right'].includes(value.dock)
    && (value.ink===undefined||validInkPreferences(value.ink))
    && (value.navigation===undefined||validNavigationPreferences(value.navigation))
    && (value.shortcuts===undefined||validateShortcuts(value.shortcuts).valid)
    && (value.layout===undefined||LAYOUTS.includes(value.layout))
    && SIDEBARS.includes(value.sidebar) && typeof value.showSidebar === 'boolean'
    && typeof value.showProperties === 'boolean' && object(value.styles)
    && (value.zoomLocked === undefined || typeof value.zoomLocked === 'boolean')
    && (value.annotationsVisible === undefined || typeof value.annotationsVisible === 'boolean')
    && (value.favouritesCollapsed === undefined || typeof value.favouritesCollapsed === 'boolean')
    && (value.sidebarWidth === undefined || finiteIn(value.sidebarWidth, 112, 360))
    && (value.propertiesWidth === undefined || finiteIn(value.propertiesWidth, 180, 420))
    && CORE_TOOLS.every(tool => validStyle(value.styles[tool], tool))
    && SHAPE_TOOLS.every(tool => value.styles[tool] === undefined || validStyle(value.styles[tool], tool))
    && TEXT_TOOLS.every(tool => value.styles[tool] === undefined || validStyle(value.styles[tool], tool))
    && REVIEW_TOOLS.every(tool => value.styles[tool] === undefined || validStyle(value.styles[tool], tool))
    && Array.isArray(value.favourites)
    && value.favourites.length <= MAX_FAVOURITES && value.favourites.every(validFavourite);
}
function needsMigration(value) {
  return value.ink===undefined||value.navigation===undefined||value.shortcuts===undefined||SHORTCUT_COMMANDS.some(({id})=>value.shortcuts?.[id]===undefined)
    ||value.layout===undefined||TEXT_TOOLS.some(tool=>value.styles[tool]===undefined)||REVIEW_TOOLS.some(tool=>value.styles[tool]===undefined||value.styles[tool].fill===undefined)||value.favouritesCollapsed === undefined || value.sidebarWidth === undefined
    || value.propertiesWidth === undefined || value.zoomLocked === undefined || value.annotationsVisible === undefined
    || value.favourites.some(entry => entry.id === undefined)
    || new Set(value.favourites.map(entry => entry.id)).size !== value.favourites.length
    || SHAPE_TOOLS.some(tool => value.styles[tool] === undefined || value.styles[tool].fill === undefined)
    || value.favourites.some(entry => [...SHAPE_TOOLS,...TEXT_TOOLS,...REVIEW_TOOLS].includes(entry.tool) && entry.fill === undefined);
}
function validLegacyPreferences(value) {
  return object(value)
    && (value.ink===undefined||validInkPreferences(value.ink))
    && (value.navigation===undefined||validNavigationPreferences(value.navigation))
    && (value.shortcuts===undefined||validateShortcuts(value.shortcuts).valid)
    && (value.layout===undefined||LAYOUTS.includes(value.layout))
    && (value.styles === undefined || (object(value.styles) && TOOLS.every(tool => value.styles[tool] === undefined || validStyle(value.styles[tool], tool))))
    && (value.favourites === undefined || (Array.isArray(value.favourites) && value.favourites.every(validFavourite)))
    && (value.dock === undefined || ['left', 'right'].includes(value.dock))
    && (value.theme === undefined || THEMES.includes(value.theme))
    && (value.sidebar === undefined || SIDEBARS.includes(value.sidebar))
    && (value.showSidebar === undefined || typeof value.showSidebar === 'boolean')
    && (value.showProperties === undefined || typeof value.showProperties === 'boolean')
    && (value.zoomLocked === undefined || typeof value.zoomLocked === 'boolean')
    && (value.annotationsVisible === undefined || typeof value.annotationsVisible === 'boolean')
    && (value.favouritesCollapsed === undefined || typeof value.favouritesCollapsed === 'boolean')
    && (value.sidebarWidth === undefined || finiteIn(value.sidebarWidth, 112, 360))
    && (value.propertiesWidth === undefined || finiteIn(value.propertiesWidth, 180, 420));
}
function result(status, canWrite, message, extra = {}) {
  return { status, canWrite, writable: canWrite, message, ...extra };
}
function parseRecord(raw) {
  if (typeof raw !== 'string' || !raw.length || raw.length > MAX_RECORD_LENGTH) return null;
  try {
    const parsed = JSON.parse(raw);
    return object(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

// This is a read-only inspection; loading never deletes legacy data or rewrites a damaged record.
function inspect(storage) {
  if (!storage || typeof storage.getItem !== 'function' || typeof storage.setItem !== 'function') {
    return result('unavailable', false, '无法访问本机偏好存储；本次设置仅在当前窗口有效。');
  }
  try {
    const raw = storage.getItem(PREFERENCES_KEY);
    if (raw !== null) {
      const record = parseRecord(raw);
      if (!record || !Number.isInteger(record.version)) {
        return result('corrupt', false, '已保存的偏好记录损坏，原记录未覆盖；本次设置仅在当前窗口有效。');
      }
      if (record.version !== PREFERENCES_VERSION) {
        return result('unsupported', false, '偏好记录版本不受支持，已保留原记录；本次设置仅在当前窗口有效。');
      }
      if (Array.isArray(record.preferences?.favourites) && record.preferences.favourites.length > MAX_FAVOURITES) {
        return result('limit-exceeded', false, '收藏超过当前 512 项安全上限，仅显示可处理的项目；原记录保留，偏好暂不写入。', { input: record.preferences });
      }
      if (!validPreferences(record.preferences)) {
        return result('corrupt', false, '部分偏好值无效，已采用安全值并保留原记录；本次设置仅在当前窗口有效。', { input: record.preferences });
      }
      if (needsMigration(record.preferences)) {
        return result('migrated', true, '已读取原有偏好并补齐文字／审阅收藏、阅读、墨迹、画布导航和快捷键设置；下次保存使用完整记录。', { input: record.preferences });
      }
      return result('ready', true, '偏好已从本机记录读取。', { input: record.preferences });
    }
    const legacyRaw = storage.getItem(LEGACY_PREFERENCES_KEY);
    if (legacyRaw === null) return result('defaults', true, '使用默认偏好；更改后保存到本机。');
    const legacy = parseRecord(legacyRaw);
    if (!legacy) return result('legacy-invalid', false, '旧版偏好无法读取，原记录已保留；本次设置仅在当前窗口有效。');
    if (Array.isArray(legacy.favourites) && legacy.favourites.length > MAX_FAVOURITES) {
      return result('limit-exceeded', false, '旧版收藏超过当前 512 项安全上限，仅显示可处理的项目；原记录保留，偏好暂不写入。', { input: legacy });
    }
    if (!validLegacyPreferences(legacy)) return result('legacy-invalid', false, '旧版偏好含无效字段，已采用安全值并保留原记录；本次设置仅在当前窗口有效。', { input: legacy });
    return result('migrated', true, '已读取旧版工具偏好；下次保存使用新版记录，旧记录继续保留。', { input: legacy });
  } catch {
    return result('unavailable', false, '无法读取本机偏好存储；本次设置仅在当前窗口有效。');
  }
}

/** Storage is explicitly supplied by the caller so a denied localStorage getter cannot break startup. */
export function loadPreferences(storage, defaults = {}) {
  const inspected = inspect(storage);
  const { input, ...status } = inspected;
  return { ...status, preferences: normalizePreferences(input, defaults) };
}

/** Recheck the stored version at every write, including changes made by another window. */
export function savePreferences(storage, preferences, defaults = {}) {
  const inspected = inspect(storage);
  if (!inspected.canWrite) {
    const { input, ...status } = inspected;
    return { ...status, saved: false };
  }
  if (Array.isArray(preferences?.favourites) && preferences.favourites.length > MAX_FAVOURITES) {
    return result('limit-exceeded', false, '收藏超过当前 512 项安全上限，本次偏好未保存；原记录继续保留。', { saved: false });
  }
  if(preferences?.favourites!==undefined&&(!Array.isArray(preferences.favourites)||!preferences.favourites.every(validFavourite))){
    return result('invalid-input',true,'收藏预设包含无效工具或样式，本次偏好未保存。',{saved:false});
  }
  if((preferences?.ink!==undefined&&!validInkPreferences(preferences.ink))||(preferences?.navigation!==undefined&&!validNavigationPreferences(preferences.navigation))
    ||(preferences?.shortcuts!==undefined&&!validateShortcuts(preferences.shortcuts).valid)){
    return result('invalid-input',true,'墨迹、导航或快捷键设置无效或冲突，本次偏好未保存。',{saved:false});
  }
  const snapshot = normalizePreferences(preferences, defaults);
  try {
    const serialized = JSON.stringify({ version: PREFERENCES_VERSION, preferences: snapshot });
    if (serialized.length > MAX_RECORD_LENGTH) {
      return result('limit-exceeded', false, '偏好记录超过当前可保存的大小，本次偏好未保存；原记录继续保留。', { saved: false });
    }
    storage.setItem(PREFERENCES_KEY, serialized);
    return result('saved', true, '偏好已保存到本机。', { saved: true });
  } catch {
    return result('unavailable', false, '偏好保存失败；本次更改仍在当前窗口有效，原记录未主动删除。', { saved: false });
  }
}
