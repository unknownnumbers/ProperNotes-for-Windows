(function () {
  'use strict';

  const NS = 'http://www.w3.org/2000/svg';
  const DEFAULT_PAGE_WIDTH = 640;
  const DEFAULT_PAGE_HEIGHT = 860;
  const MAX_STROKES = 50000;
  const MAX_POINTS = 100000;
  const MAX_PAGE_POINTS = 2000000;
  const MAX_HISTORY_POINTS = 1000000;
  const MAX_HISTORY_BYTES = 32 * 1024 * 1024;
  const MAX_LASSO_POINTS = 512;
  const MAX_LASSO_CHECKS = 2000000;
  const SHAPES = ['line', 'arrow', 'rectangle', 'ellipse'];
  const TEXT_TOOLS = ['textbox', 'note'];
  const REVIEW_TOOLS = ['text-highlight', 'text-underline', 'text-squiggle', 'text-strike'];
  const SELECTION_STYLE_FIELDS = ['color', 'width', 'opacity', 'pressure', 'fill', 'fontSize', 'fontFamily', 'bold', 'italic'];
  const DRAWING_TOOLS = ['pen', 'highlighter', ...SHAPES, ...TEXT_TOOLS];
  const OBJECT_TOOLS = [...DRAWING_TOOLS, ...REVIEW_TOOLS];
  const G = window.ProperNotesGeometry;
  if (!G) throw new Error('annotation-geometry.js must be loaded before ink-engine.js');
  const isShape = (stroke) => SHAPES.includes(stroke.tool);
  const isText = (stroke) => TEXT_TOOLS.includes(stroke.tool);
  const isReview = (stroke) => REVIEW_TOOLS.includes(stroke.tool);
  const validColor = (value) => typeof value === 'string' && /^#(?:[a-f\d]{3}|[a-f\d]{6})$/i.test(value);
  const shapeFill = (tool, value) => ['rectangle', 'ellipse'].includes(tool) && validColor(value) ? value : 'none';
  let sequence = 0;
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const clamp = (value, minimum, maximum) => Math.max(minimum, Math.min(maximum, value));
  const number = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const freshId = () => 'ink-' + Date.now().toString(36) + '-' + (++sequence).toString(36);
  const round = (value) => Math.round(value * 100) / 100;
  const xy = (point) => round(point[0]) + ',' + round(point[1]);
  const strokeRadius = (stroke, point) => stroke.width * (stroke.pressure && stroke.tool === 'pen' ? 0.25 + 1.25 * point[2] : 1) / 2;

  function element(name, attributes) {
    const node = document.createElementNS(NS, name);
    Object.entries(attributes || {}).forEach(([key, value]) => node.setAttribute(key, String(value)));
    return node;
  }

  function distanceToSegment(point, from, to) {
    const dx = to[0] - from[0];
    const dy = to[1] - from[1];
    const length = dx * dx + dy * dy;
    const t = length ? clamp(((point[0] - from[0]) * dx + (point[1] - from[1]) * dy) / length, 0, 1) : 0;
    return Math.hypot(point[0] - from[0] - t * dx, point[1] - from[1] - t * dy);
  }

  function arrowHead(stroke) {
    const [from, to] = stroke.points;
    const dx = to[0] - from[0], dy = to[1] - from[1], length = Math.hypot(dx, dy);
    if (!length) return [to.slice(0, 2), to.slice(0, 2), to.slice(0, 2)];
    const ux = dx / length, uy = dy / length;
    const headLength = Math.min(Math.max(8, stroke.width * 4), length * 0.6);
    const halfWidth = Math.min(Math.max(4, stroke.width * 2), headLength * 0.5);
    return [[to[0], to[1]],
      [to[0] - ux * headLength - uy * halfWidth, to[1] - uy * headLength + ux * halfWidth],
      [to[0] - ux * headLength + uy * halfWidth, to[1] - uy * headLength - ux * halfWidth]];
  }

  function shapeBox(stroke) {
    const [a, b] = stroke.points;
    return { left: Math.min(a[0], b[0]), top: Math.min(a[1], b[1]), right: Math.max(a[0], b[0]), bottom: Math.max(a[1], b[1]) };
  }

  function shapeOutline(stroke) {
    const [a, b] = stroke.points;
    if (stroke.tool === 'line') return [[a, b]];
    if (stroke.tool === 'arrow') {
      const head = arrowHead(stroke);
      return [[a, b], [head[0], head[1]], [head[1], head[2]], [head[2], head[0]]];
    }
    const box = shapeBox(stroke);
    let points;
    if (stroke.tool === 'rectangle') points = [[box.left, box.top], [box.right, box.top], [box.right, box.bottom], [box.left, box.bottom]];
    else {
      const cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
      const rx = (box.right - box.left) / 2, ry = (box.bottom - box.top) / 2;
      const count = Math.min(4096, Math.max(72, Math.ceil(Math.PI * Math.sqrt(Math.max(rx, ry) * G.scaleOf(stroke.transform) / 0.5))));
      points = Array.from({ length: count }, (_, index) => {
        const angle = index / count * Math.PI * 2;
        return [cx + Math.cos(angle) * rx, cy + Math.sin(angle) * ry];
      });
    }
    return points.map((point, index) => [point, points[(index + 1) % points.length]]);
  }

  function triangleContains(point, triangle) {
    if (Math.abs((triangle[1][0] - triangle[0][0]) * (triangle[2][1] - triangle[0][1]) - (triangle[1][1] - triangle[0][1]) * (triangle[2][0] - triangle[0][0])) < 1e-8) return false;
    const cross = (a, b) => (b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0]);
    const signs = triangle.map((a, index) => cross(a, triangle[(index + 1) % 3]));
    return !(signs.some(value => value < -1e-8) && signs.some(value => value > 1e-8));
  }

  function shapeContains(point, stroke) {
    if (stroke.tool === 'arrow') return triangleContains(point, arrowHead(stroke));
    if (!['rectangle', 'ellipse'].includes(stroke.tool) || stroke.fill === 'none') return false;
    const box = shapeBox(stroke);
    if (point[0] < box.left || point[0] > box.right || point[1] < box.top || point[1] > box.bottom) return false;
    if (stroke.tool === 'rectangle') return true;
    const rx = (box.right - box.left) / 2, ry = (box.bottom - box.top) / 2;
    if (!rx || !ry) return false;
    return ((point[0] - (box.left + box.right) / 2) / rx) ** 2 + ((point[1] - (box.top + box.bottom) / 2) / ry) ** 2 <= 1;
  }

  function segmentDistance(a, b, c, d) {
    const cross = (p, q, r) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    const ac = cross(a, b, c), ad = cross(a, b, d), ca = cross(c, d, a), cb = cross(c, d, b);
    const overlap = Math.max(Math.min(a[0], b[0]), Math.min(c[0], d[0])) <= Math.min(Math.max(a[0], b[0]), Math.max(c[0], d[0])) + 1e-8
      && Math.max(Math.min(a[1], b[1]), Math.min(c[1], d[1])) <= Math.min(Math.max(a[1], b[1]), Math.max(c[1], d[1])) + 1e-8;
    if (overlap && ac * ad <= 0 && ca * cb <= 0) return 0;
    return Math.min(distanceToSegment(a, c, d), distanceToSegment(b, c, d), distanceToSegment(c, a, b), distanceToSegment(d, a, b));
  }

  function pointBounds(strokes) {
    if (!strokes.length) return null;
    let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
    for (const stroke of strokes) for (const local of localControlPoints(stroke)) {
      const point = G.apply(local, stroke.transform);left = Math.min(left, point[0]);top = Math.min(top, point[1]);right = Math.max(right, point[0]);bottom = Math.max(bottom, point[1]);
    }
    return { x: left, y: top, width: right - left, height: bottom - top };
  }
  function localControlPoints(stroke) {
    if (['rectangle', 'ellipse', ...TEXT_TOOLS].includes(stroke.tool)) {
      const box = shapeBox(stroke);return [[box.left, box.top], [box.right, box.top], [box.right, box.bottom], [box.left, box.bottom]];
    }
    if (stroke.tool === 'arrow') return stroke.points.concat(arrowHead(stroke));
    return stroke.points;
  }
  function boundsOf(strokes) {
    if (!strokes.length) return null;
    let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
    strokes.forEach((stroke) => {
      const first = stroke.points[0], scale = G.scaleOf(stroke.transform);
      const dot = !isShape(stroke) && !isText(stroke) && !isReview(stroke) && (stroke.points.length === 1 || stroke.points.every(point => Math.hypot(point[0] - first[0], point[1] - first[1]) < 0.1));
      if (stroke.tool === 'ellipse') {
        const box = shapeBox(stroke), m = stroke.transform || G.IDENTITY, center = G.apply([(box.left + box.right) / 2, (box.top + box.bottom) / 2], m);
        const rx = (box.right - box.left) / 2, ry = (box.bottom - box.top) / 2;
        const xRadius = Math.hypot(m[0] * rx, m[2] * ry) + stroke.width * scale / 2, yRadius = Math.hypot(m[1] * rx, m[3] * ry) + stroke.width * scale / 2;
        left = Math.min(left, center[0] - xRadius);right = Math.max(right, center[0] + xRadius);top = Math.min(top, center[1] - yRadius);bottom = Math.max(bottom, center[1] + yRadius);return;
      }
      const points = dot ? [first] : isReview(stroke) && stroke.tool !== 'text-highlight' ? G.reviewPaths(stroke.tool, stroke.points, stroke.width).flatMap(path => path.points) : localControlPoints(stroke);
      points.forEach((point) => {
        const world = G.apply(point, stroke.transform), pad = (stroke.tool === 'text-highlight' || isText(stroke) ? 0 : dot ? Math.max(0.3, strokeRadius(stroke, point)) : strokeRadius(stroke, point)) * scale;
        left = Math.min(left, world[0] - pad);top = Math.min(top, world[1] - pad);right = Math.max(right, world[0] + pad);bottom = Math.max(bottom, world[1] + pad);
      });
    });
    return Number.isFinite(left) ? { x: left, y: top, width: right - left, height: bottom - top } : null;
  }

  function smoothPath(points) {
    if (points.length < 2) return 'M' + xy(points[0]);
    let path = 'M' + xy(points[0]);
    for (let i = 1; i < points.length - 1; i++) {
      const middle = [(points[i][0] + points[i + 1][0]) / 2, (points[i][1] + points[i + 1][1]) / 2];
      path += ' Q' + xy(points[i]) + ' ' + xy(middle);
    }
    return path + ' L' + xy(points[points.length - 1]);
  }

  function outlinedPath(points, width) {
    if (points.length < 2) return '';
    const left = [], right = [];
    points.forEach((point, index) => {
      const previous = points[Math.max(0, index - 1)];
      const next = points[Math.min(points.length - 1, index + 1)];
      const dx = next[0] - previous[0], dy = next[1] - previous[1];
      const length = Math.hypot(dx, dy) || 1;
      const radius = width * (0.25 + 1.25 * point[2]) / 2;
      left.push([point[0] - dy / length * radius, point[1] + dx / length * radius]);
      right.push([point[0] + dy / length * radius, point[1] - dx / length * radius]);
    });
    return 'M' + left.map(xy).join(' L') + ' L' + right.reverse().map(xy).join(' L') + ' Z';
  }

  function densePoints(points, step) {
    if (points.length < 2) return points.map((point) => point.slice());
    const result = [points[0].slice()];
    for (let i = 1; i < points.length; i++) {
      const previous = points[i - 1], point = points[i];
      const parts = Math.max(1, Math.ceil(Math.hypot(point[0] - previous[0], point[1] - previous[1]) / step));
      if (result.length + parts > MAX_POINTS * 2) return points.map((sample) => sample.slice());
      for (let j = 1; j <= parts; j++) {
        const t = j / parts;
        result.push([previous[0] + (point[0] - previous[0]) * t, previous[1] + (point[1] - previous[1]) * t, previous[2] + (point[2] - previous[2]) * t]);
      }
    }
    return result;
  }

  function normalizeStroke(input, pageWidth, pageHeight) {
    if (!input || !Array.isArray(input.points)) return null;
    if (input.locked !== undefined && typeof input.locked !== 'boolean') throw new TypeError('批注锁定状态无效。');
    if (input.points.length > MAX_POINTS) throw new RangeError('单条笔迹采样点过多。');
    if (input.points.some((point) => !Array.isArray(point) || point.length < 2 || !Number.isFinite(Number(point[0])) || !Number.isFinite(Number(point[1])) || (point[2] !== undefined && !Number.isFinite(Number(point[2]))))) throw new TypeError('笔迹坐标或压力值无效。');
    const strokeTool = input.tool === undefined ? 'pen' : input.tool;
    if (!OBJECT_TOOLS.includes(strokeTool)) throw new TypeError('批注工具类型不受支持。');
    const special = [...SHAPES, ...TEXT_TOOLS, ...REVIEW_TOOLS].includes(strokeTool);
    if ([...SHAPES, ...TEXT_TOOLS].includes(strokeTool) && input.points.length !== 2) throw new TypeError('形状和文字框需要两个控制点。');
    if (REVIEW_TOOLS.includes(strokeTool) && (input.points.length < 4 || input.points.length > 16000 || input.points.length % 4)) throw new TypeError('文字标记需要完整的文字四边形。');
    const transform = G.normalizeMatrix(input.transform), localWidth = input.transform === undefined ? pageWidth : 100000, localHeight = input.transform === undefined ? pageHeight : 100000;
    if (input.transform !== undefined && input.points.some(point => point[0] < 0 || point[0] > 100000 || point[1] < 0 || point[1] > 100000)) throw new RangeError('批注局部坐标超出当前范围。');
    const points = input.points.map((point) => [clamp(Number(point[0]), 0, localWidth), clamp(Number(point[1]), 0, localHeight), special ? 0.5 : clamp(number(point[2], 0.5), 0, 1)]);
    if (!points.length) return null;
    if (SHAPES.includes(strokeTool)) {
      const dx = Math.abs(points[1][0] - points[0][0]), dy = Math.abs(points[1][1] - points[0][1]);
      if (['rectangle', 'ellipse'].includes(strokeTool) ? !dx || !dy : !Math.hypot(dx, dy)) throw new TypeError('形状尺寸无效。');
    }
    if (REVIEW_TOOLS.includes(strokeTool)) for (const quad of G.reviewQuads(points)) {
      const signs = quad.map((a, index) => {const b = quad[(index + 1) % 4], c = quad[(index + 2) % 4];return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);});
      if (signs.some(value => Math.abs(value) < 0.0000000001) || signs.some(value => value > 0) && signs.some(value => value < 0)) throw new TypeError('文字标记四边形无法可靠识别。');
    }
    let textFields = {};
    if (TEXT_TOOLS.includes(strokeTool)) {
      if (typeof input.text !== 'string' || input.text.length > 20000 || /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(input.text)) throw new TypeError('文字批注内容无效或超过 20000 个字符。');
      if (Math.abs(points[1][0] - points[0][0]) < 8 || Math.abs(points[1][1] - points[0][1]) < 8) throw new RangeError('文字框至少需要 8×8 个页面单位。');
      const fontSize = input.fontSize === undefined ? 14 : input.fontSize;
      if (typeof fontSize !== 'number' || !Number.isFinite(fontSize) || fontSize < 4 || fontSize > 240) throw new RangeError('字号应在 4–240 之间。');
      const fontFamily = input.fontFamily === undefined ? 'sans' : input.fontFamily;
      if (!['sans', 'serif', 'mono'].includes(fontFamily) || (input.bold !== undefined && typeof input.bold !== 'boolean') || (input.italic !== undefined && typeof input.italic !== 'boolean')) throw new TypeError('文字批注字体设置无效。');
      if (input.fill !== undefined && input.fill !== 'none' && !validColor(input.fill)) throw new TypeError('文字框填充颜色无效。');
      textFields = { text: input.text, fontSize, fontFamily, bold: input.bold === true, italic: input.italic === true };
      if (input.raster !== undefined) {
        const raster = input.raster;
        if (!raster || !Number.isInteger(raster.width) || !Number.isInteger(raster.height) || raster.width < 1 || raster.height < 1 || raster.width > 4096 || raster.height > 4096 || raster.width * raster.height > 4000000
          || typeof raster.data !== 'string' || raster.data.length > 24000000 || typeof raster.key !== 'string' || raster.key.length > 200000) throw new TypeError('文字批注外观缓存无效。');
        textFields.raster = { width: raster.width, height: raster.height, data: raster.data, key: raster.key };
      }
    }
    return {
      id: typeof input.id === 'string' && input.id.length && input.id.length <= 160 ? input.id : freshId(),
      tool: strokeTool,
      color: validColor(input.color) ? input.color : '#374151',
      width: clamp(number(input.width, 2.6), 0.1, 100),
      opacity: clamp(number(input.opacity, strokeTool === 'highlighter' ? 0.34 : 1), 0, 1),
      pressure: special ? false : input.pressure !== false,
      locked: input.locked === true,
      ...(isShape({ tool: strokeTool }) ? { fill: shapeFill(strokeTool, input.fill) } : TEXT_TOOLS.includes(strokeTool) ? { fill: validColor(input.fill) ? input.fill : 'none' } : REVIEW_TOOLS.includes(strokeTool) ? { fill: 'none' } : {}),
      ...textFields, transform, points
    };
  }

  window.createPrototypeInkEngine = function createPrototypeInkEngine(svg, options) {
    options = options || {};
    if (!svg || svg.namespaceURI !== NS) throw new TypeError('A valid SVG element is required.');
    let strokes = [];
    let selectedIds = new Set();
    let undoStack = [];
    let redoStack = [];
    let tool = 'pen';
    let gesture = null;
    let frame = 0;
    let destroyed = false;
    let pageWidth = DEFAULT_PAGE_WIDTH;
    let pageHeight = DEFAULT_PAGE_HEIGHT;
    let inputSettings = { pressureSensitivity: 50, smoothing: 0, response: 'frame' };
    const renderedStrokes = new Map();
    const originalTouchAction = svg.style.touchAction;
    const originalCursor = svg.style.cursor;
    const defaults = { color: '#374151', width: 2.6, opacity: 1, pressure: true, fill: 'none', eraserMode: 'stroke' };
    const inkLayer = element('g', { 'data-prototype-ink-layer': '', 'pointer-events': 'none' });
    const overlayLayer = element('g', { 'data-prototype-selection-layer': '', 'pointer-events': 'none' });
    svg.append(inkLayer, overlayLayer);
    svg.style.touchAction = 'none';

    function invoke(name, value) {
      if (typeof options[name] === 'function') options[name](value);
    }

    function gestureInputSource(event, effectiveTool, hardwareEraser = false) {
      // This is the engine tool at pointerdown, not a per-device tool profile.
      return { selectedTool: tool, effectiveTool, pointerType: event.pointerType || '', pointerId: event.pointerId, hardwareEraser };
    }

    function notifyGestureEnd(finished, event, outcome, result) {
      const type = event?.type;
      const termination = ['pointerup', 'pointercancel', 'lostpointercapture'].includes(type) ? type : 'forced-finish';
      // Outcome describes this engine's annotation-model work, not whether a
      // tool should be switched. Selection can change with an unchanged model;
      // cancelled freehand ink can be committed; text still needs modal Apply.
      invoke('onGestureEnd', { ...result, kind: finished.kind, ...finished.inputSource, termination, outcome });
    }

    function selectedStrokes() {
      return strokes.filter((stroke) => selectedIds.has(stroke.id));
    }

    function editableSelectedStrokes() {
      return strokes.filter((stroke) => selectedIds.has(stroke.id) && !stroke.locked);
    }

    function moveGesture(point, pointerId, before, inputSource) {
      const editable = editableSelectedStrokes();
      return { kind: 'move', pointerId, inputSource, start: point, last: point, before, original: clone(strokes),
        moveIds: new Set(editable.map((stroke) => stroke.id)), bounds: pointBounds(editable), visualBounds: boundsOf(editable) };
    }
    function fitsOnPage(annotations) {
      const bounds = boundsOf(annotations), controls = pointBounds(annotations), tolerance = 0.01;
      return bounds && controls && [bounds, controls].every(box => box.x >= -tolerance && box.y >= -tolerance && box.x + box.width <= pageWidth + tolerance && box.y + box.height <= pageHeight + tolerance);
    }
    function transformedSelection(original, ids, matrix) {
      const next = original.map(stroke => ids.has(stroke.id) ? { ...stroke, transform: G.normalizeMatrix(G.multiply(matrix, stroke.transform || G.IDENTITY)) } : stroke);
      if (!fitsOnPage(next.filter(stroke => ids.has(stroke.id)))) throw new RangeError('变换后批注将超出页面，未应用此次变换。');
      return next;
    }
    function overlayHandles() {
      const bounds = boundsOf(editableSelectedStrokes());if (!bounds) return [];
      const matrix = svg.getScreenCTM(), scale = matrix ? Math.max(0.01, Math.hypot(matrix.a, matrix.b)) : 1, pad = 5 / scale;
      const cx = bounds.x + bounds.width / 2, cy = bounds.y + bounds.height / 2;
      return [[bounds.x - pad, bounds.y - pad], [bounds.x + bounds.width + pad, bounds.y - pad], [bounds.x + bounds.width + pad, bounds.y + bounds.height + pad], [bounds.x - pad, bounds.y + bounds.height + pad]]
        .map(point => ({ kind: 'scale', point, cx, cy, radius: 8 / scale }))
        .concat([{ kind: 'rotate', point: [cx, bounds.y - 25 / scale], cx, cy, radius: 8 / scale }]);
    }
    function startTransform(handle, point, event, before, inputSource) {
      gesture = { kind: 'transform', action: handle.kind, pointerId: event.pointerId, inputSource, start: point, last: point, before, original: clone(strokes),
        ids: new Set(editableSelectedStrokes().map(stroke => stroke.id)), cx: handle.cx, cy: handle.cy, startDistance: Math.max(0.001, Math.hypot(point[0] - handle.cx, point[1] - handle.cy)), startAngle: Math.atan2(point[1] - handle.cy, point[0] - handle.cx), error: '' };
      invoke('onActivity', handle.kind === 'rotate' ? '拖动旋转未锁定批注' : '拖动等比缩放未锁定批注');
    }

    function selectionActivity(action) {
      const selected = selectedStrokes(), lockedCount = selected.filter((stroke) => stroke.locked).length;
      if (!selected.length) return '未选中批注';
      if (lockedCount === selected.length && lockedCount) return '已选中 ' + lockedCount + ' 条锁定批注；请先解锁再编辑';
      return selected.length + ' 条批注已选中' + (lockedCount ? '，仅' + (selected.length - lockedCount) + ' 条未锁定批注可' + action : '，可' + action);
    }

    function selectionState() {
      const selected = selectedStrokes();
      const editable = selected.filter(stroke => !stroke.locked), fieldCounts = {}, editableFieldCounts = {}, mixedFields = [], editableMixedFields = [];
      const style = selected.length ? { color: selected[0].color, width: selected[0].width, opacity: selected[0].opacity, pressure: selected[0].pressure } : undefined;
      const editableStyle = editable.length ? {} : undefined;
      for (const field of SELECTION_STYLE_FIELDS) {
        const applicable = selected.filter(stroke => supportsSelectionField(stroke, field));
        const writable = applicable.filter(stroke => !stroke.locked);
        fieldCounts[field] = applicable.length;editableFieldCounts[field] = writable.length;
        if (applicable.length) {
          style[field] = applicable[0][field];
          if (applicable.some(stroke => !sameSelectionField(field, stroke[field], applicable[0][field]))) mixedFields.push(field);
        }
        if (writable.length) {
          editableStyle[field] = writable[0][field];
          if (writable.some(stroke => !sameSelectionField(field, stroke[field], writable[0][field]))) editableMixedFields.push(field);
        }
      }
      const lockedCount = selected.filter((stroke) => stroke.locked).length;
      const countTool = (list, predicate) => list.filter(predicate).length;
      return { count: selected.length, lockedCount, unlockedCount: editable.length, bounds: boundsOf(selected), editableBounds: boundsOf(editable), style, editableStyle,
        mixedFields, editableMixedFields, fieldCounts, editableFieldCounts,
        applicableFields: SELECTION_STYLE_FIELDS.filter(field => fieldCounts[field]), editableFields: SELECTION_STYLE_FIELDS.filter(field => editableFieldCounts[field]),
        capabilities: { textCount: countTool(selected, isText), unlockedTextCount: countTool(editable, isText), reviewCount: countTool(selected, isReview), unlockedReviewCount: countTool(editable, isReview), fillCount: fieldCounts.fill, unlockedFillCount: editableFieldCounts.fill, penCount: fieldCounts.pressure, unlockedPenCount: editableFieldCounts.pressure },
        tools: Array.from(new Set(selected.map(stroke => stroke.tool))) };
    }

    function supportsSelectionField(stroke, field) {
      if (['color', 'opacity'].includes(field)) return true;
      if (field === 'width') return stroke.tool !== 'text-highlight';
      if (field === 'pressure') return stroke.tool === 'pen';
      if (field === 'fill') return ['rectangle', 'ellipse', ...TEXT_TOOLS].includes(stroke.tool);
      return ['fontSize', 'fontFamily', 'bold', 'italic'].includes(field) && isText(stroke);
    }

    function sameSelectionField(field, first, second) {
      const normalized = value => {
        if (!validColor(value)) return value;
        const hex = value.slice(1).toLowerCase();return '#' + (hex.length === 3 ? hex.split('').map(character => character + character).join('') : hex);
      };
      return field === 'color' || field === 'fill' ? normalized(first) === normalized(second) : first === second;
    }

    function selectionStylePatch(input = {}) {
      if (!input || typeof input !== 'object' || Array.isArray(input) || Object.keys(input).some(field => !SELECTION_STYLE_FIELDS.includes(field))) throw new TypeError('批注属性包含不支持的字段。');
      const patch = {};
      for (const field of SELECTION_STYLE_FIELDS) if (Object.prototype.hasOwnProperty.call(input, field) && input[field] !== undefined) patch[field] = input[field];
      if (patch.color !== undefined && !validColor(patch.color)) throw new TypeError('请选择有效的批注颜色。');
      if (patch.fill !== undefined && patch.fill !== 'none' && !validColor(patch.fill)) throw new TypeError('请选择有效的填充颜色。');
      for (const [field, minimum, maximum] of [['width', 0.1, 100], ['opacity', 0, 1], ['fontSize', 4, 240]]) {
        if (patch[field] !== undefined && (typeof patch[field] !== 'number' || !Number.isFinite(patch[field]) || patch[field] < minimum || patch[field] > maximum)) throw new RangeError(field === 'fontSize' ? '字号应在 4–240 之间。' : field === 'width' ? '基础粗细应在 0.1–100 之间。' : '透明度应在 0–1 之间。');
      }
      if (patch.fontFamily !== undefined && !['sans', 'serif', 'mono'].includes(patch.fontFamily)) throw new TypeError('字体族不受支持。');
      for (const field of ['pressure', 'bold', 'italic']) if (patch[field] !== undefined && typeof patch[field] !== 'boolean') throw new TypeError('压感、粗体和斜体设置必须是布尔值。');
      return patch;
    }

    function styleFitsOnPage(next, previous) {
      if (fitsOnPage([next])) return true;
      // Existing ink can meet the page edge and be clipped there. A color or
      // opacity edit must not reject that unchanged clipping, but style changes
      // cannot increase either painted overflow or generated control geometry
      // (for example, a pre-existing arrow head that touches the page edge).
      const tolerance = 0.01;
      const notFartherOutside = (bounds, old) => bounds && old && bounds.x >= Math.min(0, old.x) - tolerance && bounds.y >= Math.min(0, old.y) - tolerance
        && bounds.x + bounds.width <= Math.max(pageWidth, old.x + old.width) + tolerance
        && bounds.y + bounds.height <= Math.max(pageHeight, old.y + old.height) + tolerance;
      return notFartherOutside(boundsOf([next]), boundsOf([previous])) && notFartherOutside(pointBounds([next]), pointBounds([previous]));
    }

    function selectionStyleCandidates(input) {
      if (destroyed) throw new Error('批注页面已关闭，未修改属性。');
      const patch = selectionStylePatch(input);
      if (gesture) throw new Error('请先结束当前书写或对象操作，再修改属性。');
      const annotations = [];
      for (const stroke of strokes) {
        if (!selectedIds.has(stroke.id) || stroke.locked) continue;
        const applicable = Object.fromEntries(Object.entries(patch).filter(([field, value]) => supportsSelectionField(stroke, field) && !sameSelectionField(field, value, stroke[field])));
        if (!Object.keys(applicable).length) continue;
        const candidate = normalizeStroke({ ...stroke, ...applicable }, pageWidth, pageHeight);
        if (!candidate || !styleFitsOnPage(candidate, stroke)) throw new RangeError('修改后的批注将超出页面，未应用任何属性修改。');
        if (isText(stroke)) {
          const text = window.ProperNotesText;
          if (!text || typeof text.key !== 'function') throw new Error('文字外观模块不可用，未修改文字批注。');
          if (text.key(candidate) !== text.key(stroke)) delete candidate.raster;
        }
        annotations.push(candidate);
      }
      return { annotations, replacedIds: annotations.map(annotation => annotation.id) };
    }

    function notifySelection() {
      invoke('onSelection', selectionState());
    }

    function notifyChange() {
      invoke('onChange', { count: strokes.length, canUndo: undoStack.length > 0, canRedo: redoStack.length > 0 });
    }

    function normalizePageData(nextStrokes) {
      if (!Array.isArray(nextStrokes)) return [];
      if (nextStrokes.length > MAX_STROKES) throw new RangeError('本页笔迹数量过多。');
      const pointCount = nextStrokes.reduce((count, stroke) => count + (Array.isArray(stroke && stroke.points) ? stroke.points.length : 0), 0);
      if (pointCount > MAX_PAGE_POINTS) throw new RangeError('本页笔迹采样点过多。');
      const next = nextStrokes.map((stroke) => normalizeStroke(stroke, pageWidth, pageHeight)).filter(Boolean);
      const ids = new Set();
      next.forEach((stroke) => { if (ids.has(stroke.id)) stroke.id = freshId(); ids.add(stroke.id); });
      return next;
    }

    function trimHistory(stack) {
      while (stack.length > 45) stack.shift();
      let pointCount = stack.reduce((count, snapshot) => count + snapshot.reduce((sum, stroke) => sum + stroke.points.length, 0), 0);
      const byteCost = snapshot => snapshot.reduce((sum, stroke) => sum + stroke.points.length * 24 + (stroke.text?.length || 0) * 2 + (stroke.raster?.data?.length || 0) * 2 + 256, 0);
      let historyBytes = stack.reduce((total, snapshot) => total + byteCost(snapshot), 0);
      while (stack.length > 1 && (pointCount > MAX_HISTORY_POINTS || historyBytes > MAX_HISTORY_BYTES)) {
        pointCount -= stack[0].reduce((sum, stroke) => sum + stroke.points.length, 0);
        historyBytes -= byteCost(stack[0]);
        stack.shift();
      }
    }

    function renderStroke(stroke) {
      const group = element('g', { opacity: stroke.opacity, transform: 'matrix(' + (stroke.transform || G.IDENTITY).join(' ') + ')', 'data-stroke-id': stroke.id, 'data-annotation-locked': stroke.locked === true });
      if (['highlighter', 'text-highlight'].includes(stroke.tool)) group.style.mixBlendMode = 'multiply';
      const points = stroke.points;
      if (isText(stroke)) {
        const box = shapeBox(stroke), width = box.right - box.left, height = box.bottom - box.top, text = window.ProperNotesText;
        if (!text) throw new Error('text-appearance.js must be loaded before editing text annotations');
        if (stroke.raster && stroke.raster.key === text.key(stroke)) {
          group.append(element('image', { x: box.left, y: box.top, width, height, href: 'data:image/png;base64,' + stroke.raster.data, preserveAspectRatio: 'none' }));
        } else {
          const inset = Math.min(stroke.width / 2, width / 2, height / 2);
          group.append(element('rect', { x: box.left + inset, y: box.top + inset, width: Math.max(0, width - inset * 2), height: Math.max(0, height - inset * 2), fill: stroke.fill, stroke: stroke.color, 'stroke-width': stroke.width }));
          const clipId = 'propernotes-text-clip-' + (++sequence), clip = element('clipPath', { id: clipId, clipPathUnits: 'userSpaceOnUse' });
          clip.append(element('rect', { x: box.left, y: box.top, width, height }));group.append(clip);
          const content = element('g', { 'clip-path': 'url(#' + clipId + ')' }), layout = text.layout(stroke);
          for (const line of layout.lines) {
            const node = element('text', { x: box.left + line.x, y: box.top + line.y, fill: stroke.color });node.style.font = layout.fontCSS;node.style.whiteSpace = 'pre';node.textContent = line.text;content.append(node);
          }
          group.append(content);
        }
      } else if (isReview(stroke)) {
        if (stroke.tool === 'text-highlight') for (const quad of G.reviewQuads(points)) group.append(element('polygon', { points: quad.map(xy).join(' '), fill: stroke.color }));
        else for (const path of G.reviewPaths(stroke.tool, points, stroke.width)) group.append(element('polyline', { points: path.points.map(xy).join(' '), fill: 'none', stroke: stroke.color, 'stroke-width': stroke.width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
      } else if (isShape(stroke)) {
        const [a, b] = points;
        const attributes = { fill: shapeFill(stroke.tool, stroke.fill), stroke: stroke.color, 'stroke-width': stroke.width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' };
        if (stroke.tool === 'line' || stroke.tool === 'arrow') {
          group.append(element('line', { ...attributes, x1: a[0], y1: a[1], x2: b[0], y2: b[1] }));
          if (stroke.tool === 'arrow') group.append(element('polygon', { points: arrowHead(stroke).map(xy).join(' '), fill: stroke.color }));
        } else {
          const box = shapeBox(stroke), width = box.right - box.left, height = box.bottom - box.top;
          group.append(stroke.tool === 'rectangle'
            ? element('rect', { ...attributes, x: box.left, y: box.top, width, height })
            : element('ellipse', { ...attributes, cx: box.left + width / 2, cy: box.top + height / 2, rx: width / 2, ry: height / 2 }));
        }
      } else if (points.length === 1 || points.every((point) => Math.hypot(point[0] - points[0][0], point[1] - points[0][1]) < 0.1)) {
        const radius = stroke.width * (stroke.pressure && stroke.tool !== 'highlighter' ? 0.25 + 1.25 * points[0][2] : 1) / 2;
        group.append(element('circle', { cx: points[0][0], cy: points[0][1], r: Math.max(0.3, radius), fill: stroke.color }));
      } else if (stroke.pressure && stroke.tool !== 'highlighter') {
        group.append(element('path', { d: outlinedPath(points, stroke.width), fill: stroke.color }));
        const first = points[0], last = points[points.length - 1];
        [first, last].forEach((point) => group.append(element('circle', { cx: point[0], cy: point[1], r: stroke.width * (0.25 + 1.25 * point[2]) / 2, fill: stroke.color })));
      } else {
        group.append(element('path', { d: smoothPath(points), fill: 'none', stroke: stroke.color, 'stroke-width': stroke.width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
      }
      return group;
    }

    function renderOverlay() {
      overlayLayer.replaceChildren();
      const selected = selectionState();
      if (selected.bounds) {
        const screen = svg.getScreenCTM(), screenScale = screen ? Math.max(0.01, Math.hypot(screen.a, screen.b)) : 1;
        const bounds = selected.bounds, pad = 5 / screenScale;
        overlayLayer.append(element('rect', { x: bounds.x - pad, y: bounds.y - pad, width: bounds.width + pad * 2, height: bounds.height + pad * 2, rx: 2, fill: 'none', stroke: '#3b82f6', 'stroke-width': 1.3, 'stroke-dasharray': '4 3', 'vector-effect': 'non-scaling-stroke' }));
        const handles = tool === 'select' ? overlayHandles() : [];
        for (const handle of handles) {
          if (handle.kind === 'rotate') overlayLayer.append(element('line', { x1: handle.cx, y1: selected.editableBounds.y, x2: handle.point[0], y2: handle.point[1], stroke: '#3b82f6', 'stroke-width': 1, 'vector-effect': 'non-scaling-stroke' }));
          overlayLayer.append(element('circle', { cx: handle.point[0], cy: handle.point[1], r: handle.radius / 2, fill: handle.kind === 'rotate' ? '#3b82f6' : '#ffffff', stroke: '#3b82f6', 'stroke-width': 1.2, 'vector-effect': 'non-scaling-stroke' }));
        }
      }
      if (gesture && ['box', 'text-region'].includes(gesture.kind)) {
        overlayLayer.append(element('rect', { x: Math.min(gesture.start[0], gesture.last[0]), y: Math.min(gesture.start[1], gesture.last[1]), width: Math.abs(gesture.start[0] - gesture.last[0]), height: Math.abs(gesture.start[1] - gesture.last[1]), fill: '#3b82f6', 'fill-opacity': 0.07, stroke: '#3b82f6', 'stroke-width': 1, 'stroke-dasharray': '4 3', 'vector-effect': 'non-scaling-stroke' }));
      }
      if (gesture?.kind === 'lasso') overlayLayer.append(element('polyline', { points: gesture.points.map(xy).join(' '), fill: '#3b82f6', 'fill-opacity': 0.07, stroke: '#3b82f6', 'stroke-width': 1, 'stroke-dasharray': '4 3', 'vector-effect': 'non-scaling-stroke' }));
    }

    function renderNow() {
      if (destroyed) return;
      const ids = new Set();
      strokes.forEach((stroke, index) => {
        ids.add(stroke.id);
        let cached = renderedStrokes.get(stroke.id);
        if (!cached || cached.stroke !== stroke || cached.pointCount !== stroke.points.length) {
          const node = renderStroke(stroke);
          if (cached && cached.node.parentNode === inkLayer) inkLayer.replaceChild(node, cached.node);
          cached = { stroke, pointCount: stroke.points.length, node };
          renderedStrokes.set(stroke.id, cached);
        }
        if (inkLayer.children[index] !== cached.node) inkLayer.insertBefore(cached.node, inkLayer.children[index] || null);
      });
      for (const [id, cached] of renderedStrokes) {
        if (!ids.has(id)) { cached.node.remove(); renderedStrokes.delete(id); }
      }
      renderOverlay();
    }

    function renderSoon() {
      if (frame) return;
      frame = requestAnimationFrame(() => { frame = 0; renderNow(); });
    }

    function flushRender() {
      if (frame) cancelAnimationFrame(frame);
      frame = 0;
      renderNow();
    }

    function renderInputPreview() {
      if (gesture?.kind === 'draw' && gesture.input.response === 'immediate') flushRender();
      else renderSoon();
    }

    function processedInputPoint(point, settings, pressureEnabled, previous) {
      const result = point.slice();
      if (pressureEnabled) result[2] = result[2] ** (2 ** ((50 - settings.pressureSensitivity) / 50));
      // A causal sample filter, rather than a guessed future pen position. Zero
      // is exactly the existing input path. The filtered samples are saved as
      // ordinary annotation points so later preference changes cannot reshape it.
      const alpha = 1 - 0.85 * settings.smoothing / 100;
      if (previous && alpha < 1) {
        result[0] = previous[0] + (result[0] - previous[0]) * alpha;
        result[1] = previous[1] + (result[1] - previous[1]) * alpha;
        if (pressureEnabled) result[2] = previous[2] + (result[2] - previous[2]) * alpha;
      }
      return result;
    }

    function commit(before) {
      if (JSON.stringify(before) === JSON.stringify(strokes)) return false;
      undoStack.push(before);
      trimHistory(undoStack);
      redoStack = [];
      flushRender();
      notifyChange();
      return true;
    }

    function pointOf(event, pointerType) {
      const matrix = svg.getScreenCTM();
      let x, y;
      if (matrix) {
        const point = svg.createSVGPoint();
        point.x = event.clientX;
        point.y = event.clientY;
        const local = point.matrixTransform(matrix.inverse());
        x = local.x; y = local.y;
      } else {
        const box = svg.getBoundingClientRect();
        x = (event.clientX - box.left) / Math.max(1, box.width) * pageWidth;
        y = (event.clientY - box.top) / Math.max(1, box.height) * pageHeight;
      }
      const pressure = (pointerType || event.pointerType) === 'pen' ? clamp(number(event.pressure, 0.5), 0, 1) : 0.5;
      return [clamp(x, 0, pageWidth), clamp(y, 0, pageHeight), pressure];
    }

    function hitLocalStroke(point, stroke, tolerance) {
      const points = stroke.points;
      const limit = (tolerance === undefined ? 5 : tolerance) + stroke.width / 2;
      if (isText(stroke)) { const box = shapeBox(stroke);return point[0] >= box.left - limit && point[0] <= box.right + limit && point[1] >= box.top - limit && point[1] <= box.bottom + limit; }
      if (isReview(stroke)) {
        if (stroke.tool === 'text-highlight') return G.reviewQuads(points).some(quad => triangleContains(point, [quad[0], quad[1], quad[2]]) || triangleContains(point, [quad[0], quad[2], quad[3]]) || quad.some((a, index) => distanceToSegment(point, a, quad[(index + 1) % 4]) <= limit));
        return G.reviewPaths(stroke.tool, points, stroke.width).some(path => path.points.some((a, index) => index && distanceToSegment(point, path.points[index - 1], a) <= limit));
      }
      if (isShape(stroke)) {
        return shapeContains(point, stroke) || shapeOutline(stroke).some(([a, b]) => distanceToSegment(point, a, b) <= limit + (stroke.tool === 'ellipse' ? 0.3 / G.scaleOf(stroke.transform) : 0));
      }
      const padding = tolerance === undefined ? 5 : tolerance;
      if (points.length === 1) return Math.hypot(point[0] - points[0][0], point[1] - points[0][1]) <= padding + strokeRadius(stroke, points[0]);
      for (let i = 1; i < points.length; i++) if (distanceToSegment(point, points[i - 1], points[i]) <= padding + Math.max(strokeRadius(stroke, points[i - 1]), strokeRadius(stroke, points[i]))) return true;
      return false;
    }
    function hitStroke(point, stroke, tolerance = 5) {
      const bounds = boundsOf([stroke]);
      if (!bounds || point[0] < bounds.x - tolerance || point[0] > bounds.x + bounds.width + tolerance || point[1] < bounds.y - tolerance || point[1] > bounds.y + bounds.height + tolerance) return false;
      return hitLocalStroke(G.apply(point, G.inverse(stroke.transform)), stroke, tolerance / G.scaleOf(stroke.transform));
    }

    function topHit(point) {
      for (let i = strokes.length - 1; i >= 0; i--) if (hitStroke(point, strokes[i])) return strokes[i];
      return null;
    }

    function lassoCheck(budget) {
      if (++budget.checks > MAX_LASSO_CHECKS) throw new RangeError('套索与批注的几何检查过多，已保留原选择；请缩小选择范围。');
    }
    function polygonContains(point, polygon, budget) {
      let inside = false;
      for (let index = 0; index < polygon.length; index++) {
        lassoCheck(budget);const a = polygon[index], b = polygon[(index + 1) % polygon.length];
        if (distanceToSegment(point, a, b) <= 1e-7) return true;
        if ((a[1] > point[1]) !== (b[1] > point[1]) && point[0] < (b[0] - a[0]) * (point[1] - a[1]) / (b[1] - a[1]) + a[0]) inside = !inside;
      }
      return inside;
    }
    function lassoHitsStroke(stroke, polygon, box, budget) {
      const bounds = boundsOf([stroke]);
      if (!bounds || bounds.x > box.right || bounds.x + bounds.width < box.left || bounds.y > box.bottom || bounds.y + bounds.height < box.top) return false;
      const transform = stroke.transform || G.IDENTITY, scale = G.scaleOf(transform);
      function pathHit(points, { closed = false, filled = false, radius = stroke.width / 2, pressure = false, dot = false } = {}) {
        if (!points.length) return false;
        const world = points.map(point => G.apply(point, transform));
        const radiusAt = index => (pressure ? strokeRadius(stroke, points[index]) : radius) * scale;
        for (let index = 0; index < world.length; index++) {
          const point = world[index], padding = dot ? Math.max(0.3 * scale, radiusAt(index)) : radiusAt(index);
          if (polygonContains(point, polygon, budget)) return true;
          for (let edge = 0; edge < polygon.length; edge++) {
            lassoCheck(budget);if (distanceToSegment(point, polygon[edge], polygon[(edge + 1) % polygon.length]) <= padding) return true;
          }
          if (!index && !closed) continue;
          const previousIndex = (index + world.length - 1) % world.length, previous = world[previousIndex], segmentPadding = Math.max(padding, radiusAt(previousIndex));
          for (let edge = 0; edge < polygon.length; edge++) {
            lassoCheck(budget);if (segmentDistance(previous, point, polygon[edge], polygon[(edge + 1) % polygon.length]) <= segmentPadding) return true;
          }
        }
        return filled && polygonContains(polygon[0], world, budget);
      }
      if (isText(stroke)) return pathHit(localControlPoints(stroke), { closed: true, filled: true });
      if (stroke.tool === 'text-highlight') return G.reviewQuads(stroke.points).some(points => pathHit(points, { closed: true, filled: true, radius: 0 }));
      if (isReview(stroke)) return G.reviewPaths(stroke.tool, stroke.points, stroke.width).some(path => pathHit(path.points));
      if (stroke.tool === 'arrow') return pathHit(stroke.points) || pathHit(arrowHead(stroke), { closed: true, filled: true, radius: 0 });
      if (isShape(stroke)) {
        if (stroke.tool === 'line') return pathHit(stroke.points);
        return pathHit(shapeOutline(stroke).map(segment => segment[0]), { closed: true, filled: stroke.fill !== 'none' });
      }
      const first = stroke.points[0], dot = stroke.points.length === 1 || stroke.points.every(point => Math.hypot(point[0] - first[0], point[1] - first[1]) < 0.1);
      return pathHit(dot ? [first] : stroke.points, { pressure: stroke.pressure && stroke.tool === 'pen', dot });
    }
    function addLassoPoint(event) {
      if (!gesture || gesture.kind !== 'lasso') return;
      const point = pointOf(event), previous = gesture.points[gesture.points.length - 1];
      gesture.last = point;
      if (Math.hypot(point[0] - previous[0], point[1] - previous[1]) < gesture.sampleDistance) return;
      if (gesture.points.length >= MAX_LASSO_POINTS) { gesture.tooComplex = true;return; }
      gesture.points.push(point);
    }
    function startLasso(event, activity) {
      const point = pointOf(event), matrix = svg.getScreenCTM(), scale = matrix ? Math.max(0.01, Math.hypot(matrix.a, matrix.b)) : 1;
      gesture = { kind: 'lasso', pointerId: event.pointerId, inputSource: gestureInputSource(event, 'lasso'), start: point, last: point, before: strokes, points: [point], originalSelection: new Set(selectedIds), additiveIds: event.shiftKey ? new Set(selectedIds) : new Set(), sampleDistance: 2 / scale, minimumSize: 3 / scale, tooComplex: false };
      event.preventDefault();event.stopPropagation();
      try { svg.setPointerCapture(event.pointerId); } catch (_) { /* The caller may route viewport events if capture is refused. */ }
      invoke('onActivity', activity);renderSoon();return true;
    }
    function beginLasso(event) {
      // Keep the external middle-button entry separate from the selected tool.
      if (destroyed || gesture || !event || event.pointerType !== 'mouse' || event.button !== 1 || event.isPrimary === false) return false;
      return startLasso(event, '中键拖出闭合套索选择批注；Shift 可追加选择');
    }

    function eraseBetween(from, to) {
      const radius = clamp(defaults.width * 1.5, 7, 36);
      let changed = false;
      const next = [];
      strokes.forEach((stroke) => {
        const bounds = boundsOf([stroke]);
        const left = Math.min(from[0], to[0]) - radius;
        const right = Math.max(from[0], to[0]) + radius;
        const top = Math.min(from[1], to[1]) - radius;
        const bottom = Math.max(from[1], to[1]) + radius;
        if (bounds && (bounds.x > right || bounds.x + bounds.width < left || bounds.y > bottom || bounds.y + bounds.height < top)) { next.push(stroke); return; }
        const inverse = G.inverse(stroke.transform), localFrom = G.apply(from, inverse), localTo = G.apply(to, inverse), localRadius = radius / G.scaleOf(stroke.transform);
        if (isText(stroke) || isReview(stroke)) {
          const polygons = isText(stroke) ? [localControlPoints(stroke)] : stroke.tool === 'text-highlight' ? G.reviewQuads(stroke.points) : [];
          const paths = polygons.length ? polygons.map(points => points.concat([points[0]])) : G.reviewPaths(stroke.tool, stroke.points, stroke.width).map(path => path.points);
          const hit = polygons.some(quad => triangleContains(localFrom, [quad[0], quad[1], quad[2]]) || triangleContains(localFrom, [quad[0], quad[2], quad[3]]) || triangleContains(localTo, [quad[0], quad[1], quad[2]]) || triangleContains(localTo, [quad[0], quad[2], quad[3]]))
            || paths.some(points => points.some((point, index) => index && segmentDistance(localFrom, localTo, points[index - 1], point) <= localRadius + stroke.width / 2));
          if (stroke.locked) { if (hit && gesture) gesture.lockedSkipped = true;next.push(stroke);return; }
          if (hit) changed = true;else next.push(stroke);return;
        }
        if (isShape(stroke)) {
          const hit = shapeContains(localFrom, stroke) || shapeContains(localTo, stroke)
            || shapeOutline(stroke).some(([a, b]) => segmentDistance(localFrom, localTo, a, b) <= localRadius + stroke.width / 2 + (stroke.tool === 'ellipse' ? 0.3 / G.scaleOf(stroke.transform) : 0));
          if (stroke.locked) {
            if (hit && gesture) gesture.lockedSkipped = true;
            next.push(stroke);return;
          }
          if (hit) changed = true;
          else next.push(stroke);
          // Shape objects remain shapes: even partial erasing removes one whole object.
          return;
        }
        const samples = densePoints(stroke.points, 2.4 / G.scaleOf(stroke.transform));
        const cut = samples.map((point) => distanceToSegment(point, localFrom, localTo) <= localRadius + strokeRadius(stroke, point));
        if (!cut.some(Boolean)) { next.push(stroke); return; }
        if (stroke.locked) { if (gesture) gesture.lockedSkipped = true;next.push(stroke);return; }
        changed = true;
        if (defaults.eraserMode !== 'partial') return;
        let segment = [];
        function keepSegment() {
          if (segment.length) next.push({ ...stroke, id: freshId(), points: segment });
          segment = [];
        }
        samples.forEach((point, index) => { if (cut[index]) keepSegment(); else segment.push(point); });
        keepSegment();
      });
      if (changed) {
        strokes = next;
        renderSoon();
      }
      return changed;
    }

    function begin(event) {
      if (destroyed || gesture || event.pointerType === 'touch' || event.isPrimary === false) return;
      const hardwareEraser = event.pointerType === 'pen' && ((event.buttons & 32) !== 0 || event.button === 5);
      const effectiveTool = hardwareEraser ? 'eraser' : tool;
      if (effectiveTool === 'pan') return;
      if (event.button !== 0 && !hardwareEraser) return;
      if (DRAWING_TOOLS.includes(effectiveTool) && strokes.length >= MAX_STROKES) { invoke('onActivity', '本页批注数量过多，请先保存。'); return; }
      if (effectiveTool === 'lasso') {
        // ProperNotes policy: primary lasso always draws a selection region.
        // Moving, handles and text double-click editing belong to Select.
        startLasso(event, '拖出闭合套索选择批注；Shift 可追加选择');return;
      }
      const inputSource = gestureInputSource(event, effectiveTool, hardwareEraser);
      event.preventDefault();
      event.stopPropagation();
      const point = pointOf(event);
      const before = clone(strokes);
      if (effectiveTool === 'select') {
        const handle = overlayHandles().find(handle => Math.hypot(point[0] - handle.point[0], point[1] - handle.point[1]) <= handle.radius);
        if (handle) {
          startTransform(handle, point, event, before, inputSource);
          try { svg.setPointerCapture(event.pointerId); } catch (_) { /* Host can refuse capture. */ }
          renderSoon();return;
        }
        const hit = topHit(point);
        if (hit) {
          if (event.shiftKey) {
            if (selectedIds.has(hit.id)) selectedIds.delete(hit.id);
            else selectedIds.add(hit.id);
          } else if (!selectedIds.has(hit.id)) selectedIds = new Set([hit.id]);
          gesture = moveGesture(point, event.pointerId, before, inputSource);
          invoke('onActivity', selectionActivity('拖动或修改属性'));
        } else {
          const selectedBounds = boundsOf(selectedStrokes());
          if (selectedBounds && !event.shiftKey && point[0] >= selectedBounds.x - 5 && point[0] <= selectedBounds.x + selectedBounds.width + 5 && point[1] >= selectedBounds.y - 5 && point[1] <= selectedBounds.y + selectedBounds.height + 5) {
            gesture = moveGesture(point, event.pointerId, before, inputSource);
            invoke('onActivity', selectionActivity('拖动或修改属性'));
          } else {
            if (!event.shiftKey) selectedIds.clear();
            gesture = { kind: 'box', pointerId: event.pointerId, inputSource, start: point, last: point, before, additiveIds: new Set(selectedIds) };
            invoke('onActivity', '拖出矩形框选择笔迹');
          }
        }
        notifySelection();
      } else if (TEXT_TOOLS.includes(effectiveTool)) {
        selectedIds.clear();notifySelection();gesture = { kind: 'text-region', pointerId: event.pointerId, inputSource, start: point, last: point, before, tool: effectiveTool };
        invoke('onActivity', '拖出文字框，或点击使用默认大小');
      } else if (effectiveTool === 'eraser') {
        selectedIds.clear();
        notifySelection();
        gesture = { kind: 'erase', pointerId: event.pointerId, inputSource, start: point, last: point, before };
        eraseBetween(point, point);
        invoke('onActivity', defaults.eraserMode === 'partial' ? '局部擦除' : '整笔擦除');
      } else if (SHAPES.includes(effectiveTool)) {
        selectedIds.clear();notifySelection();
        const start = [point[0], point[1], 0.5];
        const stroke = { id: freshId(), tool: effectiveTool, color: defaults.color, width: defaults.width,
          opacity: defaults.opacity, pressure: false, locked: false, fill: shapeFill(effectiveTool, defaults.fill), points: [start, start.slice()] };
        strokes.push(stroke);
        gesture = { kind: 'shape', pointerId: event.pointerId, inputSource, start, last: start, before, stroke };
        invoke('onActivity', '拖动绘制形状');
      } else {
        selectedIds.clear();
        notifySelection();
        const settings = { ...inputSettings }, pressureEnabled = defaults.pressure && event.pointerType === 'pen' && effectiveTool !== 'highlighter';
        const start = processedInputPoint(point, settings, pressureEnabled);
        const stroke = {
          id: freshId(), tool: effectiveTool === 'highlighter' ? 'highlighter' : 'pen', color: defaults.color,
          width: defaults.width, opacity: defaults.opacity, pressure: pressureEnabled, locked: false, points: [start]
        };
        strokes.push(stroke);
        gesture = { kind: 'draw', pointerId: event.pointerId, inputSource, start, last: start, before, stroke, input: settings, filteredPoint: start };
        invoke('onActivity', stroke.tool === 'highlighter' ? '正在标记' : '正在书写');
      }
      try { svg.setPointerCapture(event.pointerId); } catch (_) { /* Older embedded hosts can refuse capture. */ }
      renderInputPreview();
    }

    function move(event) {
      if (!gesture || event.pointerId !== gesture.pointerId) return;
      event.preventDefault();
      const point = pointOf(event);
      if (gesture.kind === 'draw') {
        let samples = typeof event.getCoalescedEvents === 'function' ? event.getCoalescedEvents() : [];
        if (!samples.length) samples = [event];
        samples.forEach((sample) => {
          const next = processedInputPoint(pointOf(sample, event.pointerType), gesture.input, gesture.stroke.pressure, gesture.filteredPoint);
          gesture.filteredPoint = next;
          const previous = gesture.stroke.points[gesture.stroke.points.length - 1];
          if (gesture.stroke.points.length < MAX_POINTS && Math.hypot(next[0] - previous[0], next[1] - previous[1]) >= 0.35) gesture.stroke.points.push(next);
        });
      } else if (gesture.kind === 'lasso') {
        addLassoPoint(event);
      } else if (gesture.kind === 'shape') {
        const next = { ...gesture.stroke, points: [gesture.start.slice(), [point[0], point[1], 0.5]] };
        strokes[strokes.length - 1] = next;gesture.stroke = next;
      } else if (gesture.kind === 'erase') {
        eraseBetween(gesture.last, point);
      } else if (gesture.kind === 'move') {
        let dx = point[0] - gesture.start[0], dy = point[1] - gesture.start[1];
        if (gesture.bounds) {
          let minX = -gesture.bounds.x, maxX = pageWidth - gesture.bounds.x - gesture.bounds.width;
          let minY = -gesture.bounds.y, maxY = pageHeight - gesture.bounds.y - gesture.bounds.height;
          if (gesture.visualBounds) {
            const visual = gesture.visualBounds;
            // A pre-existing edge-clipped stroke must not jump merely on selection.
            // Keep all control points in range and avoid moving its visible bounds farther out.
            minX = Math.max(minX, Math.min(0, -visual.x));
            maxX = Math.min(maxX, Math.max(0, pageWidth - visual.x - visual.width));
            minY = Math.max(minY, Math.min(0, -visual.y));
            maxY = Math.min(maxY, Math.max(0, pageHeight - visual.y - visual.height));
          }
          dx = clamp(dx, minX, maxX);dy = clamp(dy, minY, maxY);
        }
        strokes = gesture.original.map((stroke) => gesture.moveIds.has(stroke.id) ? { ...stroke, transform: G.normalizeMatrix(G.multiply(G.translation(dx, dy), stroke.transform || G.IDENTITY)) } : stroke);
        notifySelection();
      } else if (gesture.kind === 'transform') {
        const scale = gesture.action === 'scale' ? Math.hypot(point[0] - gesture.cx, point[1] - gesture.cy) / gesture.startDistance : 1;
        const rotation = gesture.action === 'rotate' ? (Math.atan2(point[1] - gesture.cy, point[0] - gesture.cx) - gesture.startAngle) * 180 / Math.PI : 0;
        try { strokes = transformedSelection(gesture.original, gesture.ids, G.around(gesture.cx, gesture.cy, scale, rotation));gesture.error = '';notifySelection(); }
        catch (error) { gesture.error = error.message; }
      }
      gesture.last = point;
      renderInputPreview();
    }

    function end(event) {
      if (!gesture || (event && event.pointerId !== gesture.pointerId)) return;
      if (event?.type === 'pointerup' && ['move', 'transform', 'text-region', 'box', 'lasso'].includes(gesture.kind)) move(event);
      if (event?.type === 'pointerup' && gesture.kind === 'draw' && gesture.input.smoothing > 0) {
        // Filtering may trail the last measured location. Finish at the actual
        // release location, retaining the last sampled pressure rather than the
        // browser's pen-up zero. Zero smoothing leaves the old end path intact.
        const endpoint = pointOf(event), previous = gesture.stroke.points[gesture.stroke.points.length - 1];endpoint[2] = gesture.filteredPoint[2];
        if (gesture.stroke.points.length < MAX_POINTS && Math.hypot(endpoint[0] - previous[0], endpoint[1] - previous[1]) >= 0.01) gesture.stroke.points.push(endpoint);
      }
      const finished = gesture;
      gesture = null;
      try { if (svg.hasPointerCapture(finished.pointerId)) svg.releasePointerCapture(finished.pointerId); } catch (_) { /* Host may already have released capture. */ }
      if (event && (event.type === 'pointercancel' || event.type === 'lostpointercapture') && finished.kind !== 'draw') {
        strokes = finished.before;
        if (finished.kind === 'lasso') selectedIds = new Set(finished.originalSelection);else selectedIds.clear();
        flushRender();
        notifySelection();
        notifyGestureEnd(finished, event, 'rolled-back', { changed: false, cancelled: true, count: strokes.length });
        return;
      }
      if (finished.kind === 'lasso') {
        selectedIds = new Set(finished.originalSelection);
        try {
          if (finished.tooComplex) throw new RangeError('套索采样过多，已保留原选择；请缩小选择范围。');
          const polygon = finished.points, box = { left: Math.min(...polygon.map(point => point[0])), right: Math.max(...polygon.map(point => point[0])), top: Math.min(...polygon.map(point => point[1])), bottom: Math.max(...polygon.map(point => point[1])) };
          if (polygon.length < 3 || box.right - box.left < finished.minimumSize || box.bottom - box.top < finished.minimumSize) invoke('onActivity', '套索范围过小，已保留原选择');
          else {
            const next = new Set(finished.additiveIds), budget = { checks: 0 };
            for (const stroke of strokes) if (lassoHitsStroke(stroke, polygon, box, budget)) next.add(stroke.id);
            selectedIds = next;invoke('onActivity', selectionActivity('拖动或修改属性'));
          }
        } catch (error) { invoke('onActivity', error.message || '套索选择未完成，已保留原选择'); }
        flushRender();notifySelection();notifyGestureEnd(finished, event, 'unchanged', { changed: false, count: strokes.length });return;
      }
      if (finished.kind === 'text-region') {
        let left = Math.min(finished.start[0], finished.last[0]), top = Math.min(finished.start[1], finished.last[1]);
        let width = Math.abs(finished.last[0] - finished.start[0]), height = Math.abs(finished.last[1] - finished.start[1]);
        if (width < 8 || height < 8) { width = Math.min(180, pageWidth);height = Math.min(96, pageHeight);left = Math.min(left, pageWidth - width);top = Math.min(top, pageHeight - height); }
        flushRender();notifySelection();
        if (width >= 8 && height >= 8) invoke('onCreateText', { tool: finished.tool, points: [[left, top, 0.5], [left + width, top + height, 0.5]] });
        else invoke('onActivity', '页面过小，无法添加文字框');
        notifyGestureEnd(finished, event, width >= 8 && height >= 8 ? 'deferred-text' : 'unchanged', { changed: false, count: strokes.length });return;
      }
      if (finished.kind === 'transform' && finished.error) {
        strokes = finished.before;flushRender();notifySelection();invoke('onActivity', finished.error);
        notifyGestureEnd(finished, event, 'rolled-back', { changed: false, cancelled: true, count: strokes.length });return;
      }
      if (finished.kind === 'shape') {
        if (event && event.type === 'pointerup') {
          const point = pointOf(event);
          const updated = { ...finished.stroke, points: [finished.start.slice(), [point[0], point[1], 0.5]] };
          strokes[strokes.length - 1] = updated;finished.stroke = updated;
        }
        const [a, b] = finished.stroke.points;
        const dx = Math.abs(b[0] - a[0]), dy = Math.abs(b[1] - a[1]);
        const tooSmall = ['rectangle', 'ellipse'].includes(finished.stroke.tool) ? dx < 3 || dy < 3 : Math.hypot(dx, dy) < 3;
        if (tooSmall) {
          strokes = finished.before;flushRender();notifySelection();
          invoke('onActivity', '形状过小，未添加');
          notifyGestureEnd(finished, event, 'rolled-back', { changed: false, cancelled: true, count: strokes.length });
          return;
        }
      }
      if (finished.kind === 'box') {
        const left = Math.min(finished.start[0], finished.last[0]), right = Math.max(finished.start[0], finished.last[0]);
        const top = Math.min(finished.start[1], finished.last[1]), bottom = Math.max(finished.start[1], finished.last[1]);
        selectedIds = new Set(finished.additiveIds);
        if (right - left > 3 || bottom - top > 3) {
          strokes.forEach((stroke) => {
            const bounds = boundsOf([stroke]);
            if (bounds && bounds.x + bounds.width >= left && bounds.x <= right && bounds.y + bounds.height >= top && bounds.y <= bottom) selectedIds.add(stroke.id);
          });
        }
        invoke('onActivity', selectedIds.size ? selectionActivity('拖动或修改属性') : '未选中批注');
      } else {
        commit(finished.before);
        if (finished.kind === 'draw') invoke('onActivity', '笔迹已添加');
        if (finished.kind === 'shape') invoke('onActivity', '形状已添加');
        if (finished.kind === 'erase') invoke('onActivity', finished.lockedSkipped ? '擦除完成；已跳过锁定批注' : '擦除完成');
        if (finished.kind === 'transform') invoke('onActivity', finished.error || (finished.action === 'rotate' ? '批注旋转已应用' : '批注等比缩放已应用'));
      }
      flushRender();
      notifySelection();
      const changed = JSON.stringify(finished.before) !== JSON.stringify(strokes);
      notifyGestureEnd(finished, event, changed ? 'committed' : 'unchanged', { changed, count: strokes.length });
    }

    function selectNone() {
      selectedIds.clear();
      renderOverlay();
      notifySelection();
    }
    function editText(event) {
      if (destroyed || tool !== 'select' || gesture) return;
      const hit = topHit(pointOf(event));if (!hit || !isText(hit)) return;
      event.preventDefault();event.stopPropagation();selectedIds = new Set([hit.id]);flushRender();notifySelection();
      if (hit.locked) invoke('onActivity', '文字批注已锁定，请先解锁');else invoke('onEditText', { id: hit.id });
    }

    function preparePaste(input, offsets) {
      if (!Array.isArray(input)) throw new TypeError('待粘贴批注必须为数组。');
      if (!input.length) return { annotations: [], scaled: false };
      if (input.length + strokes.length > MAX_STROKES) throw new RangeError('粘贴后本页批注数量超过上限。');
      const currentPoints = strokes.reduce((total, stroke) => total + stroke.points.length, 0);
      const incomingPoints = input.reduce((total, stroke) => total + (Array.isArray(stroke && stroke.points) ? stroke.points.length : 0), 0);
      if (currentPoints + incomingPoints > MAX_PAGE_POINTS) throw new RangeError('粘贴后本页采样点数量超过上限。');
      // Normalize against the supported maximum rather than the target page: clamping
      // to a smaller target here would distort copied geometry before group fitting.
      let annotations = input.map((stroke) => {
        if (!stroke || !Array.isArray(stroke.points) || stroke.points.some(point => !Array.isArray(point) || point[0] < 0 || point[0] > 100000 || point[1] < 0 || point[1] > 100000)) throw new TypeError('待粘贴批注坐标无效。');
        const normalized = normalizeStroke(stroke, 100000, 100000);
        if (!normalized) throw new TypeError('待粘贴批注数据无效。');
        // A locked original may be copied; each new independent copy starts unlocked.
        return { ...normalized, locked: false };
      });
      function combinedBounds(values) {
        const visual = boundsOf(values), controls = pointBounds(values);if (!visual || !controls) return null;
        const x = Math.min(visual.x, controls.x), y = Math.min(visual.y, controls.y), right = Math.max(visual.x + visual.width, controls.x + controls.width), bottom = Math.max(visual.y + visual.height, controls.y + controls.height);
        return { x, y, width: right - x, height: bottom - y };
      }
      let scaled = false, bounds = combinedBounds(annotations);
      for (let attempt = 0; bounds && (bounds.width > pageWidth || bounds.height > pageHeight) && attempt < 16; attempt++) {
        const scale = Math.min(1, pageWidth / bounds.width, pageHeight / bounds.height) * 0.999;
        if (!Number.isFinite(scale) || scale <= 0) throw new RangeError('目标页太小，无法容纳批注。');
        const matrix = G.around(bounds.x, bounds.y, scale, 0);
        annotations = annotations.map(stroke => ({ ...stroke, transform: G.normalizeMatrix(G.multiply(matrix, stroke.transform || G.IDENTITY)) }));
        scaled = true;bounds = combinedBounds(annotations);
      }
      if (!bounds || bounds.width > pageWidth || bounds.height > pageHeight) throw new RangeError('目标页太小，无法容纳批注。');
      const dx = clamp(number(offsets?.offsetX, 16), -bounds.x, pageWidth - bounds.x - bounds.width);
      const dy = clamp(number(offsets?.offsetY, 16), -bounds.y, pageHeight - bounds.y - bounds.height);
      const ids = new Set(strokes.map(stroke => stroke.id));
      annotations = annotations.map(stroke => {
        let id;do { id = freshId(); } while (ids.has(id));ids.add(id);
        return { ...stroke, id, transform: G.normalizeMatrix(G.multiply(G.translation(dx, dy), stroke.transform || G.IDENTITY)) };
      });
      if (!fitsOnPage(annotations)) throw new RangeError('目标页无法完整容纳批注，未插入副本。');
      return { annotations, scaled };
    }
    function insertAnnotation(input, allowedTools) {
      if (!input || !allowedTools.includes(input.tool)) throw new TypeError('批注类型无效。');
      if (gesture) end();
      if (strokes.length >= MAX_STROKES) throw new RangeError('本页批注数量超过当前上限。');
      const annotation = normalizeStroke({ ...input, id: freshId(), locked: false, pressure: false, transform: input.transform === undefined ? G.IDENTITY.slice() : input.transform }, pageWidth, pageHeight);
      if (!annotation || !fitsOnPage([annotation])) throw new RangeError('批注将超出页面，未添加。');
      if (strokes.reduce((total, stroke) => total + stroke.points.length, annotation.points.length) > MAX_PAGE_POINTS) throw new RangeError('本页批注采样点超过当前上限。');
      const before = clone(strokes);strokes = strokes.concat(annotation);selectedIds = new Set([annotation.id]);commit(before);notifySelection();
      return annotation.id;
    }

    svg.addEventListener('pointerdown', begin);
    svg.addEventListener('pointermove', move);
    svg.addEventListener('pointerup', end);
    svg.addEventListener('pointercancel', end);
    svg.addEventListener('lostpointercapture', end);
    svg.addEventListener('dblclick', editText);

    const api = {
      setPageSize(width, height) {
        const nextWidth = Number(width), nextHeight = Number(height);
        if (!Number.isFinite(nextWidth) || !Number.isFinite(nextHeight) || nextWidth <= 0 || nextHeight <= 0 || nextWidth > 100000 || nextHeight > 100000) throw new RangeError('PDF 页面尺寸无效。');
        if (gesture) end();
        pageWidth = nextWidth;
        pageHeight = nextHeight;
        svg.setAttribute('viewBox', '0 0 ' + pageWidth + ' ' + pageHeight);
        selectNone();
      },
      getPageSize() { return { width: pageWidth, height: pageHeight }; },
      hasActiveGesture() { return gesture !== null; },
      finishGesture() { if (gesture) end(); },
      cancelGesture() {
        if (!gesture) return false;
        // Reuse pointer cancellation exactly: object edits roll back, while the
        // existing freehand cancellation rule keeps any captured partial ink.
        const pointerId = gesture.pointerId;end({ type: 'pointercancel', pointerId });return true;
      },
      setTool(nextTool) {
        if (![...DRAWING_TOOLS, 'eraser', 'select', 'lasso', 'pan'].includes(nextTool)) return;
        if (gesture) end();
        tool = nextTool;
        if (!['select', 'lasso'].includes(tool)) selectNone();else renderOverlay();
        svg.style.cursor = tool === 'pan' ? 'grab' : tool === 'select' ? 'default' : 'crosshair';
      },
      setStyle(style) {
        style = style || {};
        if (validColor(style.color)) defaults.color = style.color;
        if (style.width !== undefined) defaults.width = clamp(number(style.width, defaults.width), 0.1, 100);
        if (style.opacity !== undefined) defaults.opacity = clamp(number(style.opacity, defaults.opacity), 0, 1);
        if (style.pressure !== undefined) defaults.pressure = Boolean(style.pressure);
        if (style.fill === 'none' || validColor(style.fill)) defaults.fill = style.fill;
        if (style.eraserMode !== undefined) defaults.eraserMode = style.eraserMode === 'partial' ? 'partial' : 'stroke';
      },
      setInputSettings(patch = {}) {
        if (!patch || typeof patch !== 'object' || Array.isArray(patch) || Object.keys(patch).some(key => !['pressureSensitivity', 'smoothing', 'response'].includes(key))) throw new TypeError('墨迹响应设置无效。');
        const next = { ...inputSettings };
        for (const key of ['pressureSensitivity', 'smoothing']) if (patch[key] !== undefined) {
          if (typeof patch[key] !== 'number' || !Number.isFinite(patch[key]) || patch[key] < 0 || patch[key] > 100) throw new RangeError('压感灵敏度与平滑应在 0–100 之间。');
          next[key] = patch[key];
        }
        if (patch.response !== undefined) {
          if (!['frame', 'immediate'].includes(patch.response)) throw new TypeError('书写响应模式无效。');
          next.response = patch.response;
        }
        inputSettings = next;return { ...inputSettings };
      },
      getInputSettings() { return { ...inputSettings }; },
      beginLasso,
      moveLasso(event) {
        if (!gesture || gesture.kind !== 'lasso' || !event || event.pointerId !== gesture.pointerId) return false;
        move(event);return true;
      },
      endLasso(event, { cancelled = false } = {}) {
        if (!gesture || gesture.kind !== 'lasso' || (event && event.pointerId !== gesture.pointerId)) return false;
        if (cancelled) end({ type: 'pointercancel', pointerId: gesture.pointerId });else end(event);
        return true;
      },
      loadPage(nextStrokes) {
        if (gesture) {
          const pointerId = gesture.pointerId;
          gesture = null;
          try { if (svg.hasPointerCapture(pointerId)) svg.releasePointerCapture(pointerId); } catch (_) { /* Ignore detached hosts. */ }
        }
        const next = normalizePageData(nextStrokes);
        strokes = next;
        selectedIds.clear();
        undoStack = [];
        redoStack = [];
        flushRender();
        notifySelection();
        notifyChange();
      },
      getStrokes() { return clone(strokes); },
      getAnnotation(id) { const annotation = strokes.find(stroke => stroke.id === id);return annotation ? clone(annotation) : null; },
      getSelection() { return { ...selectionState(), ids: Array.from(selectedIds) }; },
      addTextAnnotation(input) { const id = insertAnnotation(input, TEXT_TOOLS);invoke('onActivity', '文字批注已添加');return id; },
      addReviewAnnotation(input) { const id = insertAnnotation(input, REVIEW_TOOLS);invoke('onActivity', '文字标记已添加');return id; },
      updateTextAnnotation(id, patch) {
        if (gesture) end();
        const annotation = strokes.find(stroke => stroke.id === id);
        if (!annotation || !isText(annotation)) throw new TypeError('要编辑的文字批注已不存在。');
        if (annotation.locked) { invoke('onActivity', '文字批注已锁定，请先解锁');return false; }
        if (!patch || typeof patch !== 'object' || Array.isArray(patch)) throw new TypeError('文字批注修改内容无效。');
        const supplied = {};
        for (const field of ['text', 'fontSize', 'fontFamily', 'bold', 'italic', 'color', 'fill', 'width', 'opacity', 'points', 'transform']) if (Object.prototype.hasOwnProperty.call(patch, field)) supplied[field] = patch[field];
        const next = normalizeStroke({ ...annotation, ...supplied, raster: undefined }, pageWidth, pageHeight);
        if (!fitsOnPage([next])) throw new RangeError('修改后的文字框将超出页面，未应用。');
        if (JSON.stringify({ ...annotation, raster: undefined }) === JSON.stringify(next)) return false;
        const before = clone(strokes);strokes = strokes.map(stroke => stroke.id === id ? next : stroke);const changed = commit(before);notifySelection();
        if (changed) invoke('onActivity', '文字批注已更新');return changed;
      },
      transformSelection({ scale = 1, rotation = 0 } = {}) {
        if (gesture) end();
        if (![scale, rotation].every(value => typeof value === 'number' && Number.isFinite(value)) || scale <= 0) throw new TypeError('请输入有效的缩放倍率和旋转角度。');
        const editable = editableSelectedStrokes();if (!editable.length) { invoke('onActivity', '所选批注已锁定或没有可变换对象');return false; }
        if (scale === 1 && rotation % 360 === 0) return false;
        const bounds = boundsOf(editable), matrix = G.around(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, scale, rotation), ids = new Set(editable.map(stroke => stroke.id));
        const next = transformedSelection(strokes, ids, matrix), before = clone(strokes);strokes = next;const changed = commit(before);notifySelection();
        if (changed) invoke('onActivity', '已变换 '+editable.length+' 条未锁定批注');return changed;
      },
      selectAnnotations(ids) {
        if (!Array.isArray(ids) || ids.some(id => typeof id !== 'string')) throw new TypeError('请选择有效的批注标识。');
        if (gesture) end();
        const requested = new Set(ids);
        selectedIds = new Set(strokes.filter(stroke => requested.has(stroke.id)).map(stroke => stroke.id));
        flushRender();notifySelection();
        if (selectedIds.size) invoke('onActivity', selectionActivity('拖动或修改属性'));
        return { ...selectionState(), ids: Array.from(selectedIds) };
      },
      setSelectionLocked(locked) {
        if (typeof locked !== 'boolean') throw new TypeError('批注锁定状态必须为布尔值。');
        if (gesture) end();
        if (!selectedIds.size) return false;
        const before = clone(strokes);
        strokes = strokes.map(stroke => selectedIds.has(stroke.id) && stroke.locked !== locked ? { ...stroke, locked } : stroke);
        const changed = commit(before);notifySelection();
        if (changed) invoke('onActivity', locked ? '选中批注已锁定' : '选中批注已解锁');
        return changed;
      },
      copySelection() {
        if (gesture) end();
        return clone(selectedStrokes());
      },
      pasteAnnotations(input, offsets = {}) {
        if (gesture) end();
        const prepared = preparePaste(input, offsets);
        if (!prepared.annotations.length) return [];
        const before = clone(strokes);
        strokes = strokes.concat(prepared.annotations);
        selectedIds = new Set(prepared.annotations.map(stroke => stroke.id));
        commit(before);notifySelection();
        invoke('onActivity', '已粘贴 ' + prepared.annotations.length + ' 条批注' + (prepared.scaled ? '，已等比缩小以适合当前页' : '') + '；副本均为未锁定状态');
        return Array.from(selectedIds);
      },
      exportHistory() { return { version: 1, width: pageWidth, height: pageHeight, undo: clone(undoStack), redo: clone(redoStack) }; },
      importHistory(history) {
        if (!history || history.version !== 1 || history.width !== pageWidth || history.height !== pageHeight || !Array.isArray(history.undo) || !Array.isArray(history.redo) || history.undo.length > 45 || history.redo.length > 45) throw new TypeError('笔迹历史与当前页面不匹配。');
        const nextUndo = history.undo.map(normalizePageData), nextRedo = history.redo.map(normalizePageData);
        trimHistory(nextUndo);
        trimHistory(nextRedo);
        undoStack = nextUndo;
        redoStack = nextRedo;
        notifyChange();
      },
      undo() {
        if (gesture) end();
        if (!undoStack.length) return false;
        redoStack.push(clone(strokes));
        trimHistory(redoStack);
        strokes = undoStack.pop();
        selectedIds.clear();
        flushRender();
        notifySelection();
        notifyChange();
        invoke('onActivity', '已撤销');
        return true;
      },
      redo() {
        if (gesture) end();
        if (!redoStack.length) return false;
        undoStack.push(clone(strokes));
        trimHistory(undoStack);
        strokes = redoStack.pop();
        selectedIds.clear();
        flushRender();
        notifySelection();
        notifyChange();
        invoke('onActivity', '已重做');
        return true;
      },
      canUndo() { return undoStack.length > 0; },
      canRedo() { return redoStack.length > 0; },
      clearSelection() { selectNone(); },
      deleteSelection() {
        if (gesture) end();
        if (!selectedIds.size) return false;
        const lockedCount = selectedStrokes().filter(stroke => stroke.locked).length;
        if (!editableSelectedStrokes().length) { invoke('onActivity', '选中批注已锁定，请先解锁');return false; }
        const before = clone(strokes);
        strokes = strokes.filter((stroke) => !selectedIds.has(stroke.id) || stroke.locked);
        const retainedIds = new Set(strokes.map(stroke => stroke.id));
        selectedIds = new Set(Array.from(selectedIds).filter(id => retainedIds.has(id)));
        const changed = commit(before);
        notifySelection();
        if (changed) invoke('onActivity', '选中批注已删除' + (lockedCount ? '；已保留 ' + lockedCount + ' 条锁定批注' : ''));
        return changed;
      },
      previewSelectionStyle(style) { return clone(selectionStyleCandidates(style)); },
      updateSelection(style) {
        // Candidate construction and validation do not mutate current objects or
        // caches. Mixed selections apply each field only where it is supported.
        const prepared = selectionStyleCandidates(style);
        if (!selectedIds.size) return false;
        const lockedCount = selectedStrokes().filter(stroke => stroke.locked).length;
        if (!editableSelectedStrokes().length) { invoke('onActivity', '选中批注已锁定，请先解锁');return false; }
        if (!prepared.annotations.length) return false;
        const replacements = new Map(prepared.annotations.map(annotation => [annotation.id, annotation]));
        const before = clone(strokes);
        strokes = strokes.map(stroke => replacements.get(stroke.id) || stroke);
        const changed = commit(before);
        notifySelection();
        if (changed) invoke('onActivity', '选中批注属性已更新' + (lockedCount ? '；已跳过 ' + lockedCount + ' 条锁定批注' : ''));
        return changed;
      },
      destroy() {
        destroyed = true;
        if (frame) cancelAnimationFrame(frame);
        gesture = null;
        svg.removeEventListener('pointerdown', begin);
        svg.removeEventListener('pointermove', move);
        svg.removeEventListener('pointerup', end);
        svg.removeEventListener('pointercancel', end);
        svg.removeEventListener('lostpointercapture', end);
        svg.removeEventListener('dblclick', editText);
        inkLayer.remove();
        overlayLayer.remove();
        renderedStrokes.clear();
        svg.style.touchAction = originalTouchAction;
        svg.style.cursor = originalCursor;
      }
    };
    if (options.inputSettings !== undefined) api.setInputSettings(options.inputSettings);
    api.setTool('pen');
    api.setPageSize(options.width || DEFAULT_PAGE_WIDTH, options.height || DEFAULT_PAGE_HEIGHT);
    renderNow();
    return api;
  };
  window.createInkPDFEngine = window.createPrototypeInkEngine;
})();
