/* Similarity transforms shared by annotation editing and PDF appearance code. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.ProperNotesGeometry = factory();
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const IDENTITY = Object.freeze([1, 0, 0, 1, 0, 0]);
  function normalizeMatrix(value) {
    if (value === undefined) return IDENTITY.slice();
    if (!Array.isArray(value) || value.length !== 6 || value.some(number => typeof number !== 'number' || !Number.isFinite(number))) throw new TypeError('批注变换矩阵无效。');
    const [a, b, c, d, x, y] = value, scale = Math.hypot(a, b), tolerance = Math.max(1e-9, scale * 1e-7);
    if (scale < 0.0001 || scale > 10000 || Math.abs(a - d) > tolerance || Math.abs(b + c) > tolerance || a * d - b * c <= 0 || Math.abs(x) > 1e9 || Math.abs(y) > 1e9) throw new RangeError('批注仅支持有效的等比缩放、旋转和平移。');
    // Return the supplied numbers unchanged: appearance verification must not
    // silently round a saved transform into a different matrix.
    return value.slice();
  }
  function apply(point, matrix) {
    const m = matrix || IDENTITY;
    return [m[0] * point[0] + m[2] * point[1] + m[4], m[1] * point[0] + m[3] * point[1] + m[5], ...(point.length > 2 ? [point[2]] : [])];
  }
  function multiply(left, right) {
    return [left[0] * right[0] + left[2] * right[1], left[1] * right[0] + left[3] * right[1],
      left[0] * right[2] + left[2] * right[3], left[1] * right[2] + left[3] * right[3],
      left[0] * right[4] + left[2] * right[5] + left[4], left[1] * right[4] + left[3] * right[5] + left[5]];
  }
  function inverse(value) {
    const m = normalizeMatrix(value), determinant = m[0] * m[3] - m[1] * m[2];
    return [m[3] / determinant, -m[1] / determinant, -m[2] / determinant, m[0] / determinant,
      (m[2] * m[5] - m[3] * m[4]) / determinant, (m[1] * m[4] - m[0] * m[5]) / determinant];
  }
  function scaleOf(value) { const m = value || IDENTITY;return Math.hypot(m[0], m[1]); }
  function translation(x, y) {
    if (![x, y].every(value => typeof value === 'number' && Number.isFinite(value))) throw new TypeError('批注平移距离无效。');
    return [1, 0, 0, 1, x, y];
  }
  function around(x, y, scale = 1, degrees = 0) {
    if (![x, y, scale, degrees].every(value => typeof value === 'number' && Number.isFinite(value)) || scale <= 0) throw new TypeError('批注缩放或旋转数值无效。');
    const angle = (degrees % 360) * Math.PI / 180, a = Math.cos(angle) * scale, b = Math.sin(angle) * scale;
    return normalizeMatrix([a, b, -b, a, x - a * x + b * y, y - b * x - a * y]);
  }
  function isIdentity(value) { return value === undefined || value.every((number, index) => number === IDENTITY[index]); }
  function pointBounds(points) {
    if (!points.length) return null;
    let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
    for (const point of points) { left = Math.min(left, point[0]);top = Math.min(top, point[1]);right = Math.max(right, point[0]);bottom = Math.max(bottom, point[1]); }
    return { x: left, y: top, width: right - left, height: bottom - top };
  }
  function reviewQuads(points) {
    const result = [];for (let index = 0; index < points.length; index += 4) result.push(points.slice(index, index + 4));return result;
  }
  function reviewPaths(tool, points, width) {
    const quads = reviewQuads(points), segmentLimit = Math.max(4, Math.min(1024, Math.floor(64000 / Math.max(1, quads.length))));
    return quads.map(quad => {
      let from = quad[3], to = quad[2];
      if (tool === 'text-strike') { from = [(quad[0][0] + quad[3][0]) / 2, (quad[0][1] + quad[3][1]) / 2];to = [(quad[1][0] + quad[2][0]) / 2, (quad[1][1] + quad[2][1]) / 2]; }
      if (tool !== 'text-squiggle') return { points: [from.slice(0, 2), to.slice(0, 2)], closed: false };
      const dx = to[0] - from[0], dy = to[1] - from[1], length = Math.hypot(dx, dy);
      if (!length) return { points: [from.slice(0, 2), to.slice(0, 2)], closed: false };
      const amplitude = Math.max(0.75, width), wavelength = Math.max(4, width * 4), cycles = Math.max(1, Math.min(Math.floor(segmentLimit / 4), Math.round(length / wavelength)));
      const count = Math.min(segmentLimit, Math.max(4, cycles * 4)), nx = -dy / length, ny = dx / length;
      return { points: Array.from({ length: count + 1 }, (_, index) => {
        const t = index / count, wave = Math.sin(t * cycles * Math.PI * 2) * amplitude;
        return [from[0] + dx * t + nx * wave, from[1] + dy * t + ny * wave];
      }), closed: false };
    });
  }
  return Object.freeze({ IDENTITY, normalizeMatrix, apply, multiply, inverse, scaleOf, translation, around, isIdentity, pointBounds, reviewQuads, reviewPaths });
});
