/* Literal search over PDF.js text content only. No OCR, geometry or persistent cache. */
const MAX_RESULTS = 5000;
const MAX_PAGE_CHARACTERS = 4 * 1024 * 1024;
const MAX_CASE_EXPANSIONS = 100000;
const FOLD_YIELD_INTERVAL = 32768;

function abortError() {
  if (typeof DOMException === 'function') return new DOMException('搜索已取消。', 'AbortError');
  const error = new Error('搜索已取消。');error.name = 'AbortError';return error;
}
function checkAborted(signal) {
  if (signal?.aborted) throw abortError();
}

// PDF.js getPage/getTextContent do not accept a caller AbortSignal. Stop waiting
// without destroying the shared PDF document or leaving a rejected promise unhandled.
function abortable(promise, signal) {
  if (!signal) return Promise.resolve(promise);
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (callback, value) => {
      if (settled) return;
      settled = true;signal.removeEventListener('abort', onAbort);callback(value);
    };
    const onAbort = () => finish(reject, abortError());
    signal.addEventListener('abort', onAbort, { once: true });
    Promise.resolve(promise).then(value => finish(resolve, value), error => finish(reject, error));
    if (signal.aborted) onAbort();
  });
}
function yieldTask(signal) {
  return abortable(new Promise(resolve => setTimeout(resolve, 0)), signal);
}

function pageText(content) {
  if (!content || !Array.isArray(content.items)) throw new TypeError('本页文字层数据无法读取。');
  const parts = [];let length = 0;
  for (const item of content.items) {
    // Marked-content records need not have str; do not turn them into text.
    if (!item || typeof item.str !== 'string') continue;
    length += item.str.length;
    if (item.hasEOL && !item.str.endsWith('\n')) length++;
    if (length > MAX_PAGE_CHARACTERS) throw new RangeError('本页文字量超过当前搜索处理范围，未完成本页搜索。');
    parts.push(item.str);
    if (item.hasEOL && !item.str.endsWith('\n')) parts.push('\n');
  }
  return parts.join('');
}

// Lowercase by Unicode code point, recording only length-changing conversions.
// This is locale-independent lowercase matching, not locale collation or a full
// Unicode compatibility/case-folding table. The offsets always refer to raw text.
async function lowercaseText(text, signal, withOffsets) {
  const parts = [], expansions = [];
  let chunk = '', originalOffset = 0, foldedOffset = 0, lastYield = 0;
  for (const character of text) {
    const lower = character.toLowerCase();
    if (withOffsets && lower.length !== character.length) {
      if (expansions.length >= MAX_CASE_EXPANSIONS) throw new RangeError('本页文字的大小写映射过于复杂，未完成本页搜索。');
      expansions.push({
        foldedStart: foldedOffset, foldedEnd: foldedOffset + lower.length,
        originalStart: originalOffset, originalEnd: originalOffset + character.length,
        deltaAfter: foldedOffset + lower.length - originalOffset - character.length,
      });
    }
    chunk += lower;originalOffset += character.length;foldedOffset += lower.length;
    if (originalOffset - lastYield >= FOLD_YIELD_INTERVAL) {
      parts.push(chunk);chunk = '';lastYield = originalOffset;
      await yieldTask(signal);checkAborted(signal);
    }
  }
  parts.push(chunk);checkAborted(signal);
  return { text: parts.join(''), expansions };
}

function originalOffset(foldedOffset, expansions, end) {
  // Find the final expansion whose start is at or before this position.
  let low = 0, high = expansions.length - 1, index = -1;
  while (low <= high) {
    const middle = (low + high) >>> 1;
    if (expansions[middle].foldedStart <= foldedOffset) { index = middle;low = middle + 1; }
    else high = middle - 1;
  }
  if (index < 0) return foldedOffset;
  const item = expansions[index];
  if (foldedOffset < item.foldedEnd) {
    return end && foldedOffset > item.foldedStart ? item.originalEnd : item.originalStart;
  }
  return foldedOffset - item.deltaAfter;
}

function excerptOf(text, start, length) {
  let from = Math.max(0, start - 48), to = Math.min(text.length, Math.max(start + Math.min(length, 100) + 48, from + 144));
  // Avoid cutting a supplementary character in half at a preview boundary.
  if (from > 0 && /[\uDC00-\uDFFF]/.test(text[from]) && /[\uD800-\uDBFF]/.test(text[from - 1])) from--;
  if (to < text.length && /[\uD800-\uDBFF]/.test(text[to - 1]) && /[\uDC00-\uDFFF]/.test(text[to])) to++;
  const value = text.slice(from, to).replace(/\s+/gu, ' ').trim();
  return (from ? '…' : '') + value + (to < text.length ? '…' : '');
}
function errorMessage(error) {
  try { return String(error?.message || '读取本页文字失败。').slice(0, 300); }
  catch { return '读取本页文字失败。'; }
}
function reportProgress(callback, value) {
  if (typeof callback !== 'function') return;
  try {
    // An asynchronous UI callback must not create an unhandled rejection either.
    Promise.resolve(callback(value)).catch(() => {});
  } catch { /* A presentation callback must not change search results. */ }
}

/**
 * Search extracted page text literally; results do not contain PDF text quads.
 * start/length are conservative UTF-16 ranges in the page's joined original text.
 * Matching is non-overlapping and never crosses pages. hasEOL stays a newline.
 * searchedPages counts pages whose text was read and searched, excluding failedPages.
 * truncated means the result cap stopped a potentially incomplete search.
 */
export async function searchPDFText(pdf, query, { caseSensitive = false, signal, onProgress, maxResults = 1000 } = {}) {
  if (typeof query !== 'string') throw new TypeError('搜索内容必须是文字。');
  const count = Number.isSafeInteger(pdf?.numPages) && pdf.numPages >= 0 ? pdf.numPages : 0;
  const result = { query, results: [], truncated: false, searchedPages: 0, pageCount: count, failedPages: [] };
  if (!query) return result;
  if (signal && (typeof signal.addEventListener !== 'function' || typeof signal.removeEventListener !== 'function')) throw new TypeError('搜索取消信号无效。');
  checkAborted(signal);
  if (!pdf || typeof pdf.getPage !== 'function' || !Number.isSafeInteger(pdf.numPages) || pdf.numPages < 0) throw new TypeError('没有可搜索的 PDF 文档。');
  const limit = Number.isFinite(maxResults) ? Math.min(MAX_RESULTS, Math.max(1, Math.floor(maxResults))) : 1000;
  const needle = caseSensitive ? query : (await lowercaseText(query, signal, false)).text;
  let processedPages = 0;
  for (let pageIndex = 0; pageIndex < count; pageIndex++) {
    checkAborted(signal);
    try {
      const page = await abortable(pdf.getPage(pageIndex + 1), signal);
      checkAborted(signal);
      // Parameters match the bundled PDF.js getTextContent implementation.
      const content = await abortable(page.getTextContent({ includeMarkedContent: false, disableNormalization: false }), signal);
      checkAborted(signal);
      const raw = pageText(content);
      const folded = caseSensitive ? { text: raw, expansions: [] } : await lowercaseText(raw, signal, true);
      let offset = 0, found;
      while ((found = folded.text.indexOf(needle, offset)) !== -1) {
        checkAborted(signal);
        const start = caseSensitive ? found : originalOffset(found, folded.expansions, false);
        const end = caseSensitive ? found + needle.length : originalOffset(found + needle.length, folded.expansions, true);
        result.results.push({ pageIndex, start, length: Math.max(1, end - start), excerpt: excerptOf(raw, start, Math.max(1, end - start)) });
        offset = found + Math.max(1, needle.length);
        if (result.results.length >= limit) {
          result.truncated = folded.text.indexOf(needle, offset) !== -1 || pageIndex + 1 < count;
          break;
        }
      }
      result.searchedPages++;
    } catch (error) {
      if (signal?.aborted || error?.name === 'AbortError') throw abortError();
      result.failedPages.push({ pageIndex, message: errorMessage(error) });
    }
    processedPages++;
    reportProgress(onProgress, {
      pageIndex, processedPages, searchedPages: result.searchedPages, pageCount: count,
      resultCount: result.results.length, failedPages: result.failedPages.map(page => ({ ...page })),
    });
    checkAborted(signal);
    if (result.truncated || result.results.length >= limit) break;
    // Yield even when PDF.js returns already cached promises, so cancellation and
    // progress rendering remain possible across a large cached document.
    if (pageIndex + 1 < count) await yieldTask(signal);
  }
  return result;
}
