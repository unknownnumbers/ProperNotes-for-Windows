const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const base = __dirname;
const uiOnly = process.argv.includes('--ui-only');
const portableSources=['host-window.js','Start-ProperNotes.cmd','Start-ProperNotes.ps1','Start-ProperNotesNative.ps1','Start-ProperNotesBrowser.cmd','Start-ProperNotesBrowser.ps1'];
for(const file of portableSources)if(!fs.existsSync(path.join(base,file)))throw new Error('Missing portable source: '+file);
if (uiOnly && process.argv.includes('--runtime')) throw new Error('--ui-only cannot refresh the runtime.');
for (const file of ['app-main.js','render-page.js','document-workspace.js','browser-workspace.js','workspace-store.cjs','input-controls.js','annotation-menu.js','input-preferences.js','keyboard-shortcuts.js','ui.template.html','ink-engine.js','pdf-storage.js','annotation-geometry.js','text-appearance.js','document-view.js','text-selection.js','pdf-bookmarks.js','preferences.js','favourite-tools.js','pdf-text-search.js','backup-client.js','backup-store.cjs','file-api.js','native-files.cjs','Select-ProperNotesDocument.ps1','server.cjs']) {
  if (!fs.existsSync(path.join(base,file))) throw new Error('Missing application source: '+file);
}
if (uiOnly) {
  for (const file of ['vendor/pdf-lib.min.js','vendor/lucide.min.js','vendor/pdfjs/build/pdf.mjs','vendor/pdfjs/build/pdf.worker.mjs']) {
    if (!fs.existsSync(path.join(base,file))) throw new Error('Offline package is incomplete: '+file);
  }
  for (const directory of ['cmaps','standard_fonts','wasm','iccs']) {
    if (!fs.existsSync(path.join(base,'vendor/pdfjs',directory))) throw new Error('Missing offline PDF resources: '+directory);
  }
}
// Dependency refresh uses explicit, existing inputs. UI-only builds never read
// a developer's private cache or install/download dependencies.
const dependencyOption = process.argv.indexOf('--dependency-root');
const dependencyArgument = dependencyOption < 0 ? null : process.argv[dependencyOption + 1];
if (dependencyOption >= 0 && (!dependencyArgument || dependencyArgument.startsWith('--'))) {
  throw new Error('--dependency-root requires an existing directory containing node_modules.');
}
if (!uiOnly && !dependencyArgument) {
  throw new Error('Use --ui-only for existing vendor files, or explicitly provide --dependency-root for an existing dependency input.');
}
const dependencies = dependencyArgument ? path.resolve(dependencyArgument) : null;
const modules = dependencies ? path.join(dependencies, 'node_modules') : null;
const template = fs.readFileSync(path.join(base, 'ui.template.html'), 'utf8');
let markup = template.slice(0, template.indexOf('<style>'));
let css = template.match(/<style>([\s\S]*?)<\/style>/)[1];
markup = markup.replace('PDF 手写批注界面原型', 'ProperNotes for Windows')
  .replace('aria-label="切换示例文档"', 'aria-label="打开 PDF"')
  .replace('阅读与批注.pdf', '打开 PDF…')
  .replace('>交互原型<', '>本地 PDF<')
  .replace('>保存草稿<', '>保存 PDF<')
  .replace('aria-label="打开示例文档"', 'aria-label="打开 PDF"')
  .replace('data-tooltip="示例文档"', 'data-tooltip="打开 PDF · Ctrl+O"')
  .replace('PDF 示例阅读区', 'PDF 阅读区')
  .replace('可用鼠标或手写笔批注的示例页面', '可用鼠标或手写笔批注的 PDF 页面')
  .replace('<span>示例文档</span>', '<span>本地文件</span>')
  .replace('>示例草稿<', '>尚未打开文件<');
markup = markup.replace(/  <dialog class="ip-dialog"[\s\S]*?<\/dialog>/, '');
markup = markup.replace('  <div class="ip-toast"', '  <input type="file" accept="application/pdf,.pdf" data-input="pdf-file" hidden>\n  <div class="ip-toast"');
markup = markup.replace('    <button class="ip-button ip-save"', '    <button class="ip-button ip-save-as" data-action="save-as"><span>另存为</span></button>\n    <button class="ip-button ip-save"');
markup = markup.replace('    <main class="ip-stage" aria-label="PDF 阅读区">', `    <main class="ip-stage" aria-label="PDF 阅读区">
      <div class="ip-empty" data-ui="empty"><span class="ip-empty-symbol"><i data-lucide="file-pen-line" aria-hidden="true"></i></span><h1>把想法写进 PDF</h1><p>打开本地文件，开始阅读和手写批注。</p><button class="ip-button ip-save" data-action="open"><i data-lucide="folder-open" aria-hidden="true"></i><span>打开 PDF</span></button><small>也可以把 PDF 拖到这里</small></div>
      <div class="ip-load-indicator" data-ui="loading" role="status" hidden>正在打开 PDF…</div>
      <div class="ip-error" data-ui="error" role="alert" hidden><span data-ui="error-message"></span><button class="ip-icon-button" data-action="error-close" aria-label="关闭错误提示"><i data-lucide="x" aria-hidden="true"></i></button></div>`);
const html = '<!doctype html>\n<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><meta http-equiv="Content-Security-Policy" content="default-src \'self\'; script-src \'self\' \'wasm-unsafe-eval\'; worker-src \'self\' blob:; style-src \'self\' \'unsafe-inline\'; img-src \'self\' blob: data:; font-src \'self\' data:; connect-src \'self\' blob:; object-src \'none\'; base-uri \'self\'"><title>ProperNotes for Windows</title><link rel="stylesheet" href="styles.css"></head><body>\n'+markup+'\n<script src="vendor/pdf-lib.min.js"></script><script src="vendor/lucide.min.js"></script><script src="annotation-geometry.js"></script><script src="text-appearance.js"></script><script src="ink-engine.js"></script><script src="pdf-bookmarks.js"></script><script src="pdf-storage.js"></script><script type="module" src="app.js"></script></body></html>';
css += `
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:#202125}
#ip-prototype{height:100dvh;min-height:420px;border:0;border-radius:0}
#ip-prototype .ip-titlebar{padding-right:17px}
#ip-prototype .ip-doc-tab{max-width:480px;flex-shrink:1}
#ip-prototype .ip-save-as{font-size:12px;padding:7px 10px}
#ip-prototype .ip-document{background:#fff;width:100%;height:100%}
#ip-prototype .ip-document canvas{display:block;background:#fff}
#ip-prototype .ip-empty{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:15px;z-index:2;color:var(--ip-text);background:var(--ip-work);padding:20px;text-align:center}
#ip-prototype .ip-empty h1{font-size:24px;font-weight:500;margin:5px 0 0}
#ip-prototype .ip-empty p{font-size:13px;color:var(--ip-sub);margin:0}
#ip-prototype .ip-empty small{font-size:11px;color:var(--ip-sub)}
#ip-prototype .ip-empty-symbol{display:flex;align-items:center;justify-content:center;width:70px;height:80px;color:var(--ip-accent);background:var(--ip-panel);border:1px solid var(--ip-line);border-radius:9px}
#ip-prototype .ip-empty-symbol svg{width:34px;height:34px}
#ip-prototype .ip-empty .ip-button{margin-top:10px;padding:10px 19px}
#ip-prototype .ip-load-indicator{position:absolute;top:15px;left:50%;transform:translateX(-50%);padding:9px 17px;border-radius:7px;background:var(--ip-panel);color:var(--ip-text);border:1px solid var(--ip-line);box-shadow:0 4px 15px #00000015;font-size:12px;z-index:8;white-space:nowrap}
#ip-prototype.is-loading .ip-ink-surface{pointer-events:none}
#ip-prototype .ip-error{position:absolute;left:14px;right:14px;bottom:15px;background:var(--ip-panel);border:1px solid var(--ip-accent);border-radius:7px;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;color:var(--ip-text);font-size:12px;line-height:1.7;z-index:10;overflow-wrap:anywhere}
#ip-prototype .ip-error .ip-icon-button{flex-shrink:0}
#ip-prototype .ip-thumb-paper{padding:0;aspect-ratio:auto}
#ip-prototype .ip-thumb-paper canvas{display:block;width:100%;height:100%}
#ip-prototype .ip-thumb-paper.is-pending{background:repeating-linear-gradient(0deg,var(--ip-paper) 0,var(--ip-paper) 19px,#e7e7e7 20px,var(--ip-paper) 21px)}
#ip-prototype .ip-sidebar-empty{font-size:11px;color:var(--ip-sub);line-height:1.9;padding:8px 2px}
#ip-prototype.is-dragover .ip-stage{outline:3px dashed var(--ip-accent);outline-offset:-8px}
#ip-prototype[data-tool=pan] .ip-viewport{cursor:grab}
#ip-prototype .ip-toast{white-space:normal;text-align:center}
@media(max-width:720px){#ip-prototype{height:100dvh;border-radius:0}#ip-prototype .ip-titlebar{padding-right:12px}#ip-prototype .ip-save-as{display:none}}
`;
// The application owns favourites management; the template keeps its old demo script.
const sharedNames=['iconRefresh','renderPalette','renderTool','updateLayout'];
const shared=sharedNames.map(name=>{let line=template.split(/\r?\n/).find(line=>line.trimStart().startsWith('function '+name+'('));if(!line)throw new Error('Missing shared UI function '+name);return line.trimStart();}).join('\n');
const main=fs.readFileSync(path.join(base,'app-main.js'),'utf8');
const pageRenderer=fs.readFileSync(path.join(base,'render-page.js'),'utf8');
const documentWorkspace=fs.readFileSync(path.join(base,'document-workspace.js'),'utf8');
const inputControls=fs.readFileSync(path.join(base,'input-controls.js'),'utf8');
const annotationMenu=fs.readFileSync(path.join(base,'annotation-menu.js'),'utf8');
for(const marker of ['/* SHARED_UI_INSERT */','/* PAGE_RENDERER_INSERT */','/* DOCUMENT_WORKSPACE_INSERT */','/* INPUT_CONTROLS_INSERT */','/* ANNOTATION_MENU_INSERT */']) {
  if(main.split(marker).length!==2)throw new Error('Missing or duplicate source marker: '+marker);
}
const application=main.replace('/* SHARED_UI_INSERT */',()=>shared).replace('/* PAGE_RENDERER_INSERT */',()=>pageRenderer).replace('/* DOCUMENT_WORKSPACE_INSERT */',()=>documentWorkspace).replace('/* INPUT_CONTROLS_INSERT */',()=>inputControls).replace('/* ANNOTATION_MENU_INSERT */',()=>annotationMenu);
function copy(source, destination) { fs.mkdirSync(path.dirname(destination), { recursive: true }); fs.copyFileSync(source,destination); }
if(!uiOnly){
for(const file of ['pdf.mjs','pdf.worker.mjs'])copy(path.join(modules,'pdfjs-dist/build',file),path.join(base,'vendor/pdfjs/build',file));
for(const dir of ['cmaps','standard_fonts','wasm','iccs'])fs.cpSync(path.join(modules,'pdfjs-dist',dir),path.join(base,'vendor/pdfjs',dir),{recursive:true});
copy(path.join(modules,'pdf-lib/dist/pdf-lib.min.js'),path.join(base,'vendor/pdf-lib.min.js'));
copy(path.join(modules,'lucide/dist/umd/lucide.min.js'),path.join(base,'vendor/lucide.min.js'));
for(const pkg of ['pdf-lib','pdfjs-dist','lucide'])for(const license of ['LICENSE','LICENSE.md'])if(fs.existsSync(path.join(modules,pkg,license)))copy(path.join(modules,pkg,license),path.join(base,'licenses',pkg+'.txt'));
if(process.argv.includes('--runtime')){
 const exe=path.join(dependencies,'bin/node.exe');copy(exe,path.join(base,'runtime/node.exe'));
 if(!fs.existsSync(path.join(base,'licenses/node.txt')))throw new Error('Missing licenses/node.txt for the bundled Node.js runtime');
}
}
const instance=crypto.createHash('sha256').update(path.resolve(base).toLowerCase()).digest('hex');
const revisionHash=crypto.createHash('sha256');
for(const file of portableSources)revisionHash.update(file+'\0').update(fs.readFileSync(path.join(base,file)));
// Host binaries are published separately; hash the native sources so an old
// running service cannot advertise the current portable revision after edits.
function hostSourceFiles(directory,relative='windows-host'){
  return fs.readdirSync(directory,{withFileTypes:true}).flatMap(entry=>{
    if(entry.name==='bin'||entry.name==='obj')return[];
    const file=path.join(directory,entry.name),name=relative+'/'+entry.name;
    if(entry.isSymbolicLink())throw new Error('Native host source links are not supported: '+name);
    return entry.isDirectory()?hostSourceFiles(file,name):/\.(?:cs|csproj|manifest|json)$/i.test(entry.name)?[name]:[];
  });
}
for(const file of hostSourceFiles(path.join(base,'windows-host')).sort())revisionHash.update(file+'\0').update(fs.readFileSync(path.join(base,file)));
for(const file of ['app-main.js','render-page.js','document-workspace.js','browser-workspace.js','workspace-store.cjs','input-controls.js','annotation-menu.js','input-preferences.js','keyboard-shortcuts.js','ui.template.html','ink-engine.js','pdf-storage.js','annotation-geometry.js','text-appearance.js','document-view.js','text-selection.js','pdf-bookmarks.js','preferences.js','favourite-tools.js','pdf-text-search.js','backup-client.js','backup-store.cjs','file-api.js','native-files.cjs','Select-ProperNotesDocument.ps1','server.cjs','prepare.cjs'])revisionHash.update(file+'\0').update(fs.readFileSync(path.join(base,file)));
const buildInfo=JSON.stringify({app:'inkpdf-local',instance,revision:revisionHash.digest('hex')});
// Publish the marker last; a failed preparation never proceeds to opening the app.
for(const [file,content] of [['index.html',html],['styles.css',css],['app.js',application],['build-info.json',buildInfo]]){
  const target=path.join(base,file),temporary=path.join(base,file+'.tmp-'+process.pid);
  try{fs.writeFileSync(temporary,content,'utf8');fs.renameSync(temporary,target);}
  finally{if(fs.existsSync(temporary))fs.unlinkSync(temporary);}
}
console.log(uiOnly?'Prepared ProperNotes for Windows from local sources; no downloads or dependency installation.':'Prepared ProperNotes for Windows UI and offline dependencies.');
