import * as pdfjsLib from './vendor/pdfjs/build/pdf.mjs';
import { loadPreferences, savePreferences, MAX_FAVOURITES } from './preferences.js';
import { createFavourite, updateFavourite, duplicateFavourite, removeFavourite, moveFavourite, ensureFavouriteIds, favouriteMatchesStyle, FAVOURITE_TOOLS, defaultFavouriteStyle, favouriteStyle } from './favourite-tools.js';
import { searchPDFText } from './pdf-text-search.js';
import { createBackupClient, createBackupScheduler } from './backup-client.js';
import { createFileClient } from './file-api.js';
import { createDocumentView } from './document-view.js';
import { selectedTextQuads } from './text-selection.js';
import { createBrowserWorkspace } from './browser-workspace.js';
import { DEFAULT_INK_PREFERENCES, DEFAULT_NAVIGATION_PREFERENCES, normalizeInkPreferences, normalizeNavigationPreferences } from './input-preferences.js';
import { SHORTCUT_COMMANDS, DEFAULT_SHORTCUTS, normalizeShortcuts, validateShortcuts, bindingFromEvent, formatBinding, commandForEvent } from './keyboard-shortcuts.js';
import { connectNativeWindow } from './host-window.js';

pdfjsLib.GlobalWorkerOptions.workerSrc = new URL('./vendor/pdfjs/build/pdf.worker.mjs',import.meta.url).href;
const root = document.getElementById('ip-prototype');
let documentView=null;
const pageSurfaceNames=new Set(['page-frame','paper','document','ink','pdf-text']);
const ui = name => (pageSurfaceNames.has(name)?documentView?.getActiveSurface(name):null)||root.querySelector('[data-ui="'+name+'"]');
const $ = selector => root.querySelector(selector);
const $$ = selector => Array.from(root.querySelectorAll(selector));
const clone = value => JSON.parse(JSON.stringify(value));
const names={pen:'手写笔',highlighter:'荧光笔',line:'直线',arrow:'箭头',rectangle:'矩形',ellipse:'椭圆',textbox:'文本框',note:'便笺','text-highlight':'文本高亮','text-underline':'下划线','text-squiggle':'波浪线','text-strike':'删除线',eraser:'橡皮擦',select:'选择批注',lasso:'套索选择',text:'选择文字',pan:'平移浏览'};
const icons={pen:'pen-line',highlighter:'highlighter',line:'minus',arrow:'arrow-up-right',rectangle:'square',ellipse:'circle',textbox:'text-cursor-input',note:'sticky-note','text-highlight':'highlighter','text-underline':'underline','text-squiggle':'waves','text-strike':'strikethrough',eraser:'eraser',select:'mouse-pointer-2',lasso:'lasso-select',text:'type',pan:'hand'};
const drawingTools=['pen','highlighter','line','arrow','rectangle','ellipse'];
const shapeTools=['line','arrow','rectangle','ellipse'];
const textTools=['textbox','note'],reviewTools=['text-highlight','text-underline','text-squiggle','text-strike'];
const annotationTools=[...drawingTools,...textTools,...reviewTools];
const layouts=['single-continuous','double-continuous','single-page','double-page'];
const palette=[['#24262b','墨黑'],['#e25752','珊瑚红'],['#3c78bb','蓝色'],['#4a9878','绿色'],['#8b65aa','紫色'],['#f3ce46','黄色']];
const state={tool:'pen',zoom:1,zoomMode:'fit',sidebar:'pages',showSidebar:true,showProperties:true,dock:'left',dirty:false,focus:false,selection:0,eraserMode:'stroke',styles:{pen:{color:'#24262b',width:2.5,opacity:1,pressure:true},highlighter:{color:'#f3ce46',width:15,opacity:.36,pressure:false},eraser:{color:'#24262b',width:12,opacity:1,pressure:false},select:{color:'#e25752',width:2.5,opacity:1,pressure:true},pan:{color:'#24262b',width:2.5,opacity:1,pressure:false}},favourites:[{tool:'pen',color:'#24262b',width:2.5,opacity:1,pressure:true,name:'黑色细笔'},{tool:'pen',color:'#e25752',width:3,opacity:1,pressure:true,name:'红色批注笔'},{tool:'highlighter',color:'#f3ce46',width:15,opacity:.36,pressure:false,name:'黄色荧光笔'},{tool:'pen',color:'#3c78bb',width:2,opacity:1,pressure:true,name:'蓝色细笔'}],pdf:null,sourceBytes:null,filename:'',handle:null,pages:[],inkPages:[],pageIndex:0,targetIndex:0,saving:false,opening:false,rendering:false,revision:0,histories:new Map(),outline:[]};
let engine,restoring=false,spacePan=false,toastTimer,persistTimer,renderTask=null,renderGeneration=0,documentGeneration=0,thumbnailObserver=null;
let resolutionTimer,resolutionTask=null,resolutionGeneration=0;
let favouriteEditorId=null,favouriteDraftDirty=false,favouriteNotice='',panelDrag=null;
let pageTextTask=null,pageTextNotice='',searchController=null,searchGeneration=0;
let backupScheduler=null,backupSettings=null,backupNotice='正在读取本地备份设置…',backupDocuments=[],backupSettingsDirty=false,backupSettingsSaving=false,backupListGeneration=0;
let backupRestoreSerial=0,backupRestoreBusy=false,backupRestoreOpening=false;
const backupClient=createBackupClient();
const fileClient=createFileClient();
let nativeDialogBusy=false,nativeFilesAvailable=null,fileOpenSerial=0;
let annotationClipboard=[],annotationPasteCount=0,annotationFilter={tool:'all',lock:'all',currentPage:false};
let annotationListOffset=0;
let navigationPast=[],navigationFuture=[],navigationRestoring=false,navigationSerial=0,annotationVisibilityBusy=false;
const panelSizes={sidebar:{field:'sidebarWidth',min:112,max:360},properties:{field:'propertiesWidth',min:180,max:420}};
const activePenPointers=new Set();
Object.assign(state.styles,Object.fromEntries(shapeTools.map(tool=>[tool,{color:'#24262b',width:2.5,opacity:1,pressure:false,fill:'none'}])));
state.selectionTools=[];
Object.assign(state,{bookmarks:[],bookmarksEditable:false,bookmarksMessage:'',bookmarkEdits:[]});
let bookmarkSelectedId=null,bookmarkEditorDraft=null;
let readonlyBookmarkExpansion=new Map();
let textEditorContext=null,textEditorDirty=false,layoutBusy=false;
const documentTabs=[];
let activeTabId=null,tabBusy=false,workspaceTimer=null,workspaceTail=Promise.resolve(),workspaceReady=false,workspaceRestoring=false;
let workspaceSnapshot=null,workspaceNotice='正在读取文档与会话设置…',restoreSession=true,workspaceInteraction=0;
const browserWorkspace=createBrowserWorkspace(preferenceStorage());
let browserWorkspaceSnapshot=browserWorkspace.read();
let workspaceLoadSerial=0;
let pendingBrowserSelection=null;
let workspacePreferenceTouched=false;
let shortcutCapture=null,shortcutNotice='',middleLasso=null;
let favouriteEditorTool='pen',selectionMenuFrame=0;
let nativeWindowCloseInProgress=false,nativeWindowCloseAllowed=false,nativeWindowCloseTimer=null,nativeWindowBackupsPaused=false;
let nativeWindowCompositionActive=false,nativeWindowBridge=null;
const nativeWindowInertChildren=new Map();
state.reviewTool='text-highlight';
let touching=new Map(),panStart=null,pinchStart=null,lastNarrow=root.clientWidth<=720,widePanels={sidebar:true,properties:true},focusPanels=null;
const viewport=ui('viewport');
const frameTemplate=ui('page-frame').cloneNode(true),paperTray=$('.ip-paper-tray');
for(const tool of textTools)state.styles[tool]={color:'#24262b',width:1,opacity:1,pressure:false,fill:tool==='note'?'#fff2a8':'none',fontSize:16,fontFamily:'sans',bold:false,italic:false};
for(const tool of reviewTools)state.styles[tool]=defaultFavouriteStyle(tool);
function modalDialogs(){return ['settings-dialog','favourites-dialog','search-dialog','backup-dialog','text-editor-dialog','recent-dialog'].map(ui);}
function activePresetTool(){return state.tool==='text'?state.reviewTool:state.tool;}
function isAnnotationSelectionTool(tool=state.tool){return tool==='select'||tool==='lasso';}
function currentStyle(){if(state.selection&&engine){const selection=engine.getSelection();return selection.editableStyle||selection.style||state.styles.select;}return state.styles[isAnnotationSelectionTool()?'select':activePresetTool()]||state.styles.pan;}
function toolForEngine(tool){return spacePan||tool==='text'||!state.annotationsVisible?'pan':tool;}
function annotationRenderMode(){return state.annotationsVisible?pdfjsLib.AnnotationMode.ENABLE:pdfjsLib.AnnotationMode.DISABLE;}
function canEditAnnotations(){return !!state.pdf&&state.annotationsVisible&&!state.opening&&!state.rendering&&!annotationVisibilityBusy&&!layoutBusy;}
function syncTextMode(){
  const layer=ui('pdf-text');if(!layer)return;
  if(state.tool!=='text'){
    const selection=window.getSelection();
    if(selection?.rangeCount){const range=selection.getRangeAt(0),layers=documentView?.getRecords().map(record=>record.els['pdf-text'])||[layer];if(layers.some(item=>item.contains(range.startContainer)||item.contains(range.endContainer)))selection.removeAllRanges();}
  }
  layer.hidden=state.tool!=='text';documentView?.setTextSelectable(state.tool==='text');
}
function setTool(tool){
  if(!names[tool]||state.opening||state.rendering)return;
  if(annotationPendingFields.size&&!commitPendingAnnotationField())return;
  if(!state.annotationsVisible&&!['text','pan'].includes(tool)){toast('请先在设置中显示批注，再编辑批注');return;}
  state.tool=tool;if(!isAnnotationSelectionTool(tool))engine.clearSelection();
  engine.setTool(toolForEngine(tool));engine.setStyle({...currentStyle(),eraserMode:state.eraserMode});
  documentView?.syncTools();
  renderTool();syncTextMode();schedulePersist();
  if(tool==='text'&&pageTextNotice)ui('input-status').textContent=pageTextNotice;
}
function toast(message){ui('toast').textContent=message;ui('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>ui('toast').hidden=true,2700);}
function showError(error){ui('error-message').textContent=error?.message||String(error);ui('error').hidden=false;console.error(error);}
function newSessionId(){try{if(globalThis.crypto?.randomUUID)return globalThis.crypto.randomUUID();}catch{/* Session identity does not require a security token. */}return Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);}
function hideError(){ui('error').hidden=true;}
const preferenceDefaults={theme:'system',layout:'single-continuous',dock:state.dock,sidebar:state.sidebar,showSidebar:state.showSidebar,showProperties:state.showProperties,sidebarWidth:143,propertiesWidth:205,favouritesCollapsed:false,zoomLocked:false,annotationsVisible:true,ink:clone(DEFAULT_INK_PREFERENCES),navigation:clone(DEFAULT_NAVIGATION_PREFERENCES),shortcuts:clone(DEFAULT_SHORTCUTS),styles:clone(state.styles),favourites:clone(state.favourites)};
function preferenceStorage(){try{return window.localStorage;}catch{return null;}}
let preferenceResult=loadPreferences(preferenceStorage(),preferenceDefaults);
Object.assign(state,preferenceResult.preferences);
if(state.zoomLocked)state.zoomMode='custom';
state.favourites=ensureFavouriteIds(state.favourites);
widePanels={sidebar:state.showSidebar,properties:state.showProperties};
function preferenceSnapshot(){return{theme:state.theme,layout:state.layout,dock:state.dock,sidebar:state.sidebar,showSidebar:widePanels.sidebar,showProperties:widePanels.properties,sidebarWidth:panelDrag?.panel==='sidebar'?panelDrag.original:state.sidebarWidth,propertiesWidth:panelDrag?.panel==='properties'?panelDrag.original:state.propertiesWidth,favouritesCollapsed:state.favouritesCollapsed,zoomLocked:state.zoomLocked,annotationsVisible:state.annotationsVisible,ink:state.ink,navigation:state.navigation,shortcuts:state.shortcuts,styles:state.styles,favourites:state.favourites};}
function renderPreferenceStatus(){
  const status=ui('preferences-status');if(status)status.textContent=preferenceResult.message;
  const favouriteStatus=ui('favourite-status');if(favouriteStatus)favouriteStatus.textContent=[favouriteNotice,preferenceResult.message].filter(Boolean).join(' ');
}
function flushPreferences(){
  clearTimeout(persistTimer);persistTimer=null;
  preferenceResult=savePreferences(preferenceStorage(),preferenceSnapshot(),preferenceDefaults);
  renderPreferenceStatus();
}
function schedulePersist(){
  clearTimeout(persistTimer);
  if(preferenceResult.canWrite){preferenceResult={...preferenceResult,status:'pending',message:'偏好已应用，正在保存到本机…'};renderPreferenceStatus();}
  persistTimer=setTimeout(flushPreferences,350);
}
function applyTheme(){
  const scheme=state.theme==='light'?'light':state.theme==='dark'?'dark':'light dark';
  root.style.colorScheme=scheme;document.documentElement.style.colorScheme=scheme;
  root.dataset.theme=state.theme;
}
function renderSettings(){
  renderInputSettings();
  $('[data-setting=restore-session]').checked=restoreSession;
  ui('workspace-status').textContent=workspaceNotice;
  $('[data-setting=theme]').value=state.theme;
  $('[data-setting=show-sidebar]').checked=widePanels.sidebar;
  $('[data-setting=show-properties]').checked=widePanels.properties;
  $('[data-setting=sidebar]').value=state.sidebar;
  $('[data-setting=sidebar-width]').value=state.sidebarWidth;
  $('[data-setting=properties-width]').value=state.propertiesWidth;
  $('[data-setting=zoom-locked]').checked=state.zoomLocked;
  $('[data-setting=annotations-visible]').checked=state.annotationsVisible;
  $('[data-setting=annotations-visible]').disabled=state.opening||state.rendering||annotationVisibilityBusy;
  renderPreferenceStatus();
}
function leaveFocus(){
  if(!state.focus)return;
  state.focus=false;
  const panels=focusPanels||widePanels;
  state.showSidebar=panels.sidebar;state.showProperties=panels.properties;
  if(lastNarrow&&state.showSidebar&&state.showProperties)state.showProperties=false;
  focusPanels=null;
}
function setPanelVisibility(panel,visible){
  leaveFocus();
  const field=panel==='sidebar'?'showSidebar':'showProperties';
  state[field]=visible;widePanels[panel]=visible;
  if(lastNarrow&&visible)state[panel==='sidebar'?'showProperties':'showSidebar']=false;
  updateLayout();applyPanelWidths();renderSettings();schedulePersist();
}
function activateSidebar(panel){
  leaveFocus();
  const visible=state.sidebar===panel?!state.showSidebar:true;
  state.sidebar=panel;renderSidebar();setPanelVisibility('sidebar',visible);
}
function toggleFocus(){
  if(state.focus)leaveFocus();
  else{focusPanels={sidebar:state.showSidebar,properties:state.showProperties};state.focus=true;state.showSidebar=false;state.showProperties=false;}
  updateLayout();applyPanelWidths();
}
function openSettings(){
  saveCurrentPage();clearCanvasNavigation();
  if(spacePan){spacePan=false;engine?.setTool(toolForEngine(state.tool));root.classList.remove('is-panning');ui('mode-status').textContent=state.annotationsVisible?names[state.tool]:'批注已隐藏 · 浏览';}
  renderSettings();
  renderBackupSettings();
  const dialog=ui('settings-dialog');
  if(!dialog.open)dialog.showModal();
}
applyTheme();

function applyPanelWidths(){
  let sidebar=state.sidebarWidth,properties=state.propertiesWidth;
  if(!lastNarrow){
    const rail=$('.ip-rail')?.getBoundingClientRect().width||45;
    let overflow=Math.max(0,(state.showSidebar?sidebar:0)+(state.showProperties?properties:0)-(root.clientWidth-rail-240));
    // Keep a reading area at smaller desktop widths without changing remembered sizes.
    if(overflow&&state.showProperties){const reduction=Math.min(overflow,properties-panelSizes.properties.min);properties-=reduction;overflow-=reduction;}
    if(overflow&&state.showSidebar)sidebar-=Math.min(overflow,sidebar-panelSizes.sidebar.min);
  }
  root.style.setProperty('--ip-sidebar-width',sidebar+'px');
  root.style.setProperty('--ip-properties-width',properties+'px');
  for(const [panel,limits] of Object.entries(panelSizes)){
    const separator=$('[data-resize-panel='+panel+']');
    const shown=Math.round(panel==='sidebar'?sidebar:properties);
    separator.setAttribute('aria-valuenow',String(shown));
    separator.setAttribute('aria-valuetext',shown===state[limits.field]?shown+' 像素':shown+' 像素，记住的宽度为 '+state[limits.field]+' 像素');
    separator.setAttribute('aria-disabled',String(lastNarrow));
    separator.tabIndex=lastNarrow?-1:0;
  }
}
function setPanelWidth(panel,value,{persist=true}={}){
  const limits=panelSizes[panel];if(!limits||!Number.isFinite(value))return;
  state[limits.field]=Math.round(Math.max(limits.min,Math.min(limits.max,value)));
  applyPanelWidths();renderSettings();
  if(persist)schedulePersist();
}

function renderFavourites(){
  const container=ui('favourites');container.replaceChildren();
  for(const favourite of state.favourites){
    const button=document.createElement('button');
    button.className='ip-favourite'+(favouriteMatchesStyle(favourite,activePresetTool(),currentStyle())?' is-active':'');
    button.style.setProperty('--ink',favourite.color);button.dataset.favouriteId=favourite.id;
    button.setAttribute('aria-label',favourite.name);button.dataset.tooltip=favourite.name+' · '+favourite.width+' px';
    const icon=document.createElement('i');icon.dataset.lucide=icons[favourite.tool];icon.setAttribute('aria-hidden','true');
    button.append(icon);container.append(button);
  }
  const bar=$('.ip-favourites');bar.classList.toggle('is-right',state.dock==='right');
  bar.classList.toggle('is-collapsed',state.favouritesCollapsed);
  const collapse=$('[data-action=favourites-collapse]');
  collapse.setAttribute('aria-expanded',String(!state.favouritesCollapsed));
  collapse.setAttribute('aria-label',state.favouritesCollapsed?'展开收藏工具栏':'折叠收藏工具栏');
  collapse.dataset.tooltip=state.favouritesCollapsed?'展开收藏':'折叠收藏';
  container.hidden=state.favouritesCollapsed;iconRefresh();
}
function colorInputValue(value,fallback='#24262b'){
  if(typeof value!=='string'||!/^#(?:[a-f\d]{3}|[a-f\d]{6})$/i.test(value))return fallback;
  return value.length===4?'#'+value.slice(1).split('').map(character=>character+character).join(''):value;
}
function renderProperties(){
  const info=engine?.getSelection(),selected=state.selection>0,tool=state.tool,s=selected?(info?.editableStyle||info?.style||currentStyle()):currentStyle();
  const previewTool=selected?(state.selectionTools.length===1?state.selectionTools[0]:'pen'):tool;
  const fillable=selected?info?.applicableFields?.includes('fill'):['rectangle','ellipse',...textTools].includes(tool);
  const details={pen:s.pressure?'自由书写 · 压感开启':'自由书写 · 固定粗细',highlighter:'半透明标记',line:'拖动创建直线',arrow:'拖动创建箭头',rectangle:'拖动创建矩形',ellipse:'拖动创建椭圆',textbox:'拖出文字框后输入',note:'拖出便笺后输入',eraser:state.eraserMode==='partial'?'局部擦除笔迹 · 其他对象整件删除':'整件擦除',select:'点击或拖框选择',lasso:'用鼠标或笔圈选批注',text:'选择 PDF 已有文本层',pan:'拖动页面浏览'};
  ui('properties-heading').textContent=selected?'批注属性':'工具属性';
  ui('tool-name').textContent=selected?'已选 '+state.selection+' 条批注':tool==='text'?names[state.reviewTool]+'默认样式':names[tool];
  ui('tool-detail').textContent=selected?'只修改所选对象的适用属性':tool==='text'?'选正文后点审阅按钮应用':details[tool];
  const icon=document.createElement('i');icon.dataset.lucide=icons[tool];icon.setAttribute('aria-hidden','true');ui('current-icon').replaceChildren(icon);
  ui('quick-width').textContent=s.width??1;ui('width-label').textContent=s.width??1;ui('opacity-label').textContent=Math.round((s.opacity??1)*100);
  ui('width-caption').textContent=selected?'基础粗细':'粗细';
  $('[data-input=width]').value=s.width??1;$('[data-input=opacity]').value=(s.opacity??1)*100;
  $('[data-input=pressure]').checked=s.pressure;$('[data-input=color]').value=colorInputValue(s.color);
  const filled=s.fill&&s.fill!=='none';$('[data-input=shape-filled]').checked=!!filled;
  $('[data-input=fill-color]').value=colorInputValue(s.fill,'#f3ce46');$('[data-input=fill-color]').disabled=!filled;
  const preview=ui('stroke-preview');
  const paths={line:'M20 27 L160 27',arrow:'M20 27 L155 27 M135 16 L155 27 L135 38',rectangle:'M25 12 H155 V42 H25 Z',ellipse:'M155 27 A65 18 0 1 1 25 27 A65 18 0 1 1 155 27'};
  preview.setAttribute('d',paths[previewTool]||'M15 37 C35 8 57 47 79 24 S116 12 132 28 S157 45 168 19');
  preview.setAttribute('stroke',s.color||'#24262b');preview.setAttribute('stroke-width',s.width??1);preview.setAttribute('opacity',s.opacity??1);
  preview.setAttribute('fill',fillable&&filled?s.fill:'none');
  for(const section of $$('[data-style-section]')){
    const kind=section.dataset.styleSection;
    section.hidden=selected&&['color','width','opacity'].includes(kind)&&!info.applicableFields?.includes(kind)?true:kind==='eraser'?tool!=='eraser':kind==='pressure'?tool!=='pen'||selected:kind==='fill'?!fillable:kind==='width'&&tool==='text'&&state.reviewTool==='text-highlight'?true:tool==='eraser'||tool==='pan'||(isAnnotationSelectionTool(tool)&&!selected);
  }
  $('.ip-preview').hidden=textTools.includes(tool)||selected&&state.selectionTools.some(type=>textTools.includes(type)||reviewTools.includes(type))||tool==='eraser'||tool==='pan'||tool==='text'||(isAnnotationSelectionTool(tool)&&!selected);
  $('[data-action=favourite-add].ip-property-action').hidden=selected||!FAVOURITE_TOOLS.includes(activePresetTool());
  $$('[data-width]').forEach(button=>button.classList.toggle('is-active',Number(button.dataset.width)===s.width));
  $$('[data-eraser]').forEach(button=>{const active=button.dataset.eraser===state.eraserMode;button.classList.toggle('is-active',active);button.setAttribute('aria-pressed',active);});
  const hints={pen:'在页面上直接书写。\n按住空格可临时平移。',highlighter:'在想标记的内容上划过。\n颜色和透明度可随时调整。',line:'从起点拖到终点创建直线。',arrow:'从起点拖到箭头终点。',rectangle:'拖出矩形；可设置边框和填充。',ellipse:'拖出椭圆；可设置边框和填充。',eraser:'划过对象擦除。\n形状以整件删除，笔迹可选局部擦除。',select:'点击或拖框选择对象；Shift 可加选／减选。\n选中后可移动或修改属性。',lasso:'用鼠标左键或笔拖出套索，Shift 追加选择。\n切到“选择”可拖动对象和控制点，选区会保留。',text:'拖选文字后按 Ctrl+C 复制。\n扫描页没有文本层时无法选择。',pan:'拖动页面或使用滚轮浏览。'};
  ui('tool-hint').textContent=selected?(tool==='lasso'?'继续圈选会替换选区，Shift 可追加。\n可用就地菜单编辑，切到“选择”可拖动和变换。':'修改所选对象不会改写工具或收藏预设。\n在“选择”工具中拖动控制点变换，单个文字可编辑正文。'):tool==='text'?'拖选 PDF 正文，再点对应审阅按钮应用当前样式。\n点击收藏只准备样式，不会立即添加标记。':textTools.includes(tool)?'拖出批注框，填写文字并应用。\n双击已有文字可再次编辑。':hints[tool];
  renderTextProperties(info);
  renderPalette();iconRefresh();syncTextMode();
  if(tool==='text'&&pageTextNotice)ui('input-status').textContent=pageTextNotice;
  updateAnnotationActions();
}
function updateAnnotationActions(){
  const editable=canEditAnnotations(),locked=state.selectionLocked||0,allLocked=state.selection>0&&locked===state.selection;
  ui('selection-transform').hidden=!state.selection;
  $$('[data-transform],[data-action=transform-selection],[data-action=rotate-left],[data-action=rotate-right]').forEach(control=>control.disabled=!editable||allLocked||!state.selection);
  const editableText=state.selection===1&&textTools.includes(state.selectionTools[0]);$('[data-action=edit-text]').hidden=!editableText;$('[data-action=edit-text]').disabled=!editable||allLocked;
  $$('[data-review]').forEach(button=>button.disabled=!editable||state.tool!=='text');
  for(const action of ['copy-selection','lock-selection','unlock-selection','delete']){
    const disabled=!editable||!state.selection||(action==='lock-selection'&&allLocked)||(action==='unlock-selection'&&!locked)||(action==='delete'&&allLocked);
    $$('[data-action='+action+']').forEach(button=>button.disabled=disabled);
  }
  $$('[data-action=paste-annotations]').forEach(button=>button.disabled=!editable||!annotationClipboard.length);
  $$('[data-action=select-all]').forEach(button=>button.disabled=!editable||hasInkGesture());
  $$('[data-tool]').forEach(button=>button.disabled=!state.annotationsVisible&&!['pan','text'].includes(button.dataset.tool));
  const status=ui('selection-lock-status');status.hidden=!state.selection;
  status.textContent=locked?(locked+' 条已锁定；编辑只作用于未锁定对象。'):'所选批注均未锁定。';
  const styleDisabled=!state.annotationsVisible||state.opening||state.rendering||allLocked;
  $$('[data-style-section] input,[data-width],[data-color],[data-eraser]').forEach(control=>control.disabled=styleDisabled);
  $('[data-input=fill-color]').disabled=styleDisabled||!$('[data-input=shape-filled]').checked;
  if(!state.annotationsVisible){ui('input-status').textContent='批注已隐藏，显示后可编辑';ui('mode-status').textContent='批注已隐藏 · 浏览';}
  renderSelectionMenu();
}
function selectAllAnnotations(){
  if(!canEditAnnotations()||hasInkGesture()||!commitPendingAnnotationField())return;
  if(!isAnnotationSelectionTool())setTool('select');
  if(isAnnotationSelectionTool())engine.selectAnnotations(engine.getStrokes().map(stroke=>stroke.id));
}
function copySelectedAnnotations(){
  if(!canEditAnnotations()||!state.selection)return;
  engine.finishGesture();annotationClipboard=engine.copySelection();annotationPasteCount=0;
  updateAnnotationActions();toast('已复制 '+annotationClipboard.length+' 条批注，可在本窗口跨页粘贴');
}
function pasteAnnotations(){
  if(!canEditAnnotations()||!annotationClipboard.length)return;
  try{
    assertAnnotationBudgets(annotationClipboard);
    setTool('select');const offset=16*(annotationPasteCount+1);
    const ids=engine.pasteAnnotations(annotationClipboard,{offsetX:offset,offsetY:offset});
    if(ids.length){annotationPasteCount++;saveCurrentPage();renderSidebar();updateButtons();toast('已粘贴 '+ids.length+' 条批注；副本未锁定');}
  }catch(error){showError(error);}
}
function lockSelectedAnnotations(locked){
  if(!canEditAnnotations())return;
  if(engine.setSelectionLocked(locked)){saveCurrentPage();renderSidebar();updateButtons();}
}
function syncAnnotationVisibility(){
  ui('ink').style.visibility=state.annotationsVisible?'visible':'hidden';
  ui('ink').style.pointerEvents=state.annotationsVisible?'':'none';
  engine?.setTool(toolForEngine(state.tool));updateAnnotationActions();
}
async function setAnnotationsVisible(visible){
  if(state.opening||state.rendering||annotationVisibilityBusy||nativeDialogBusy||backupRestoreBusy||navigationRestoring||layoutBusy){renderSettings();return;}
  if(visible===state.annotationsVisible)return;
  engine?.finishGesture();engine?.clearSelection();
  const previous=state.annotationsVisible;state.annotationsVisible=visible;annotationVisibilityBusy=true;
  syncAnnotationVisibility();renderSettings();updateButtons();setBusy(true,'正在更新批注显示…');
  try{
    if(documentView)await documentView.setAnnotationsVisible(visible);
    syncAnnotationVisibility();renderSidebar();
    schedulePersist();
  }catch(error){state.annotationsVisible=previous;syncAnnotationVisibility();renderSidebar();showError(error);}
  finally{annotationVisibilityBusy=false;setBusy(state.opening||state.rendering);renderSettings();renderTool();updateButtons();}
}
async function setReadingLayout(value){
  if(!layouts.includes(value)||layoutBusy||state.opening||state.rendering||annotationVisibilityBusy||navigationRestoring)return;
  const previous=state.layout;layoutBusy=true;saveCurrentPage();updateButtons();
  try{if(documentView)await documentView.setLayout(value);state.layout=value;fitIfNeeded();schedulePersist();renderSidebar();}
  catch(error){state.layout=previous;showError(error);}
  finally{layoutBusy=false;$('[data-input=layout]').value=state.layout;updateButtons();}
}
async function toggleFullscreen(){
  try{
    if(document.fullscreenElement)await document.exitFullscreen();
    else if(root.requestFullscreen)await root.requestFullscreen();
    else toast('当前浏览器没有可用的全屏接口');
  }catch(error){toast('无法切换全屏：'+(error.message||String(error)));}
}
function changeStyle(patch,options={}){
  if(state.opening||state.rendering||!state.annotationsVisible)return false;
  if(state.selection)return applySelectedStyle(patch,options);
  const tool=activePresetTool();if(!FAVOURITE_TOOLS.includes(tool))return false;
  try{state.styles[tool]=favouriteStyle(tool,{...currentStyle(),...patch});engine.setStyle({...currentStyle(),eraserMode:state.eraserMode});documentView?.syncTools();renderProperties();renderFavourites();schedulePersist();return true;}
  catch(error){showError(error);renderProperties();return false;}
}
function transformSelected(scale,rotation){
  if(!canEditAnnotations()||!state.selection)return;
  try{if(engine.transformSelection({scale,rotation})){saveCurrentPage();renderSidebar();updateButtons();}}
  catch(error){showError(error);}
}
function applyTransformFields(){
  const scale=$('[data-transform=scale]'),rotation=$('[data-transform=rotation]');
  if(!scale.reportValidity()||!rotation.reportValidity()||!scale.value||!rotation.value)return;
  transformSelected(Number(scale.value)/100,Number(rotation.value));
}
function assertAnnotationBudgets(extra=[],replaceId=null,pageIndex=state.pageIndex){
  saveCurrentPage();let points=0,review=0,characters=0,pixels=0,rasterBytes=0;
  const replacing=replaceId instanceof Set?replaceId:new Set(Array.isArray(replaceId)?replaceId:replaceId?[replaceId]:[]);
  function add(stroke){
    points+=stroke.points.length;
    if(reviewTools.includes(stroke.tool)&&stroke.tool!=='text-highlight')review+=window.ProperNotesGeometry.reviewPaths(stroke.tool,stroke.points,stroke.width).reduce((sum,path)=>sum+path.points.length,0);
    if(textTools.includes(stroke.tool)){
      characters+=stroke.text.length;const size=window.ProperNotesText.dimensions(stroke);pixels+=size.width*size.height;
      if(stroke.raster?.key===window.ProperNotesText.key(stroke))rasterBytes+=window.ProperNotesText.validateRaster(stroke,{required:true}).bytes.length;
    }
    if(points>window.InkPDFStorage.limits.points||review>2000000)throw new Error('此操作会超过整份 PDF 的笔迹或标记路径上限，未添加批注。');
    if(characters>window.ProperNotesText.limits.totalText||pixels>window.ProperNotesText.limits.totalPixels||rasterBytes>window.ProperNotesText.limits.totalBytes)throw new Error('此操作会超过整份 PDF 的文字或外观预算，未添加批注。');
  }
  state.inkPages.forEach((page,index)=>page.forEach(stroke=>{if(index!==pageIndex||!replacing.has(stroke.id))add(stroke);}));extra.forEach(add);
}
async function addTextReview(tool){
  if(!canEditAnnotations()||state.tool!=='text'||!reviewTools.includes(tool))return;
  state.reviewTool=tool;
  try{
    const generation=documentGeneration,records=documentView?.getRecords()||[],selected=selectedTextQuads(records,state.zoom);
    if(selected.pageIndex!==state.pageIndex&&!await renderPage(selected.pageIndex,{preserveView:true}))return;
    if(generation!==documentGeneration||!canEditAnnotations())return;
    const annotation={...favouriteStyle(tool,state.styles[tool]),tool,points:selected.points,locked:false};
    assertAnnotationBudgets([annotation]);const id=engine.addReviewAnnotation(annotation);
    if(id){setTool('select');engine.selectAnnotations([id]);saveCurrentPage();renderSidebar();toast('文字标记已添加，可选择、移动和继续编辑');}
  }catch(error){showError(error);renderTool();}
}
function openTextEditor(record,details={}){
  if(!canEditAnnotations()||record!==documentView?.getActiveRecord())return;
  if(nativeDialogBusy||backupRestoreBusy){toast('请等待当前文件操作完成后编辑文字');return;}
  const annotation=details.id?record.engine.getAnnotation(details.id):null;
  if(annotation?.locked){toast('请先解锁此文字批注');return;}
  const tool=annotation?.tool||details.tool||'textbox',style=state.styles[tool];if(!style)return;
  const value=annotation||{...style,tool,points:details.points,text:''};
  textEditorContext={record,pageIndex:record.index,generation:documentGeneration,id:annotation?.id||null,value};textEditorDirty=false;
  for(const field of $$('[data-text-field]')){
    const key=field.dataset.textField;
    if(key==='boxWidth')field.value=Math.abs(value.points[1][0]-value.points[0][0]);
    else if(key==='boxHeight')field.value=Math.abs(value.points[1][1]-value.points[0][1]);
    else if(key==='filled')field.checked=value.fill!=='none';
    else if(key==='fill')field.value=colorInputValue(value.fill,'#fff2a8');
    else if(field.type==='checkbox')field.checked=!!value[key];else field.value=value[key]??'';
  }
  ui('text-editor-status').textContent='这里设置基础字号和框大小，对象的整体缩放仍会生效。文字过长时可扩大框或减小字号。';
  ui('text-editor-dialog').showModal();$('[data-text-field=text]').focus();
}
function discardTextEditor(){if(textEditorDirty&&!window.confirm('文字编辑尚未应用。确定放弃这些输入吗？'))return false;textEditorDirty=false;return true;}
function closeTextEditor(){if(discardTextEditor())ui('text-editor-dialog').close();}
function applyTextEditor(){
  const context=textEditorContext;if(!context||!ui('text-editor-form').reportValidity())return;
  if(context.generation!==documentGeneration||context.record!==documentView?.getActiveRecord()){ui('text-editor-status').textContent='文档或页面已变化，请重新选择文字批注。';return;}
  try{
    const get=key=>$('[data-text-field='+key+']'),patch={text:get('text').value,fontSize:Number(get('fontSize').value),fontFamily:get('fontFamily').value,bold:get('bold').checked,italic:get('italic').checked,color:get('color').value,fill:get('filled').checked?get('fill').value:'none'};
    const left=Math.min(...context.value.points.map(point=>point[0])),top=Math.min(...context.value.points.map(point=>point[1]));
    patch.points=[[left,top,.5],[left+Number(get('boxWidth').value),top+Number(get('boxHeight').value),.5]];
    if(!patch.text.trim())throw new Error('请输入文字。');
    const candidate={...context.value,...patch};
    const layout=window.ProperNotesText.layout(candidate);
    if(layout.overflow&&!window.confirm('文字超出批注框，超出部分不会显示，但文字会保留供继续编辑。仍然应用吗？'))return;
    assertAnnotationBudgets([candidate],context.id,context.pageIndex);
    let id=context.id;
    if(id){if(!context.record.engine.updateTextAnnotation(id,patch)){ui('text-editor-status').textContent='文字未改变，或对象已锁定。';return;}}
    else{id=context.record.engine.addTextAnnotation(candidate);}
    if(!context.id)for(const key of ['fontSize','fontFamily','bold','italic','color','fill'])state.styles[candidate.tool][key]=patch[key];
    textEditorDirty=false;ui('text-editor-dialog').close();setTool('select');engine.selectAnnotations([id]);saveCurrentPage();renderSidebar();schedulePersist();
  }catch(error){ui('text-editor-status').textContent=error.message||String(error);}
}
function setFavouriteNotice(message){favouriteNotice=message;renderPreferenceStatus();}
function selectedFavourite(){return state.favourites.find(favourite=>favourite.id===favouriteEditorId);}
function discardFavouriteDraft(){
  if(!favouriteDraftDirty)return true;
  if(!window.confirm('收藏修改尚未保存。确定放弃这些修改吗？'))return false;
  favouriteDraftDirty=false;return true;
}
function loadFavouriteEditor(favourite){
  const values={...defaultFavouriteStyle('textbox'),...(favourite||{...defaultFavouriteStyle('pen'),name:'',tool:'pen'})};favouriteEditorTool=values.tool;
  for(const field of $$('[data-favourite-field]')){
    const key=field.dataset.favouriteField;field.disabled=!favourite;
    if(field.type==='checkbox'&&!['fill-enabled'].includes(key))field.checked=!!values[key];
    else if(key==='fill-enabled')field.checked=!!values.fill&&values.fill!=='none';
    else if(key==='fill-color')field.value=colorInputValue(values.fill,'#f3ce46');
    else field.value=key==='opacity'?values.opacity*100:values[key];
  }
  updateFavouriteEditorFields(!!favourite);
  ui('favourite-opacity-value').textContent=Number((values.opacity*100).toFixed(2))+'%';
  favouriteDraftDirty=false;
}
function renderFavouriteManager({loadEditor=true}={}){
  const list=ui('favourites-manager-list');list.replaceChildren();
  if(!selectedFavourite())favouriteEditorId=state.favourites[0]?.id||null;
  for(const favourite of state.favourites){
    const row=document.createElement('button');row.type='button';row.className='ip-favourite-manager-row';
    row.dataset.favouriteEdit=favourite.id;row.classList.toggle('is-active',favourite.id===favouriteEditorId);
    row.setAttribute('aria-pressed',String(favourite.id===favouriteEditorId));
    const swatch=document.createElement('span');swatch.className='ip-favourite-manager-swatch';swatch.style.backgroundColor=favourite.color;
    swatch.setAttribute('aria-hidden','true');const label=document.createElement('span'),name=document.createElement('strong'),detail=document.createElement('small');
    name.textContent=favourite.name;detail.textContent=favouriteDescription(favourite);
    label.append(name,detail);row.append(swatch,label);list.append(row);
  }
  const index=state.favourites.findIndex(favourite=>favourite.id===favouriteEditorId),selected=selectedFavourite();
  ui('favourites-empty').hidden=!!state.favourites.length;
  for(const action of ['save','copy','remove'])$('[data-action=favourite-'+action+']').disabled=!selected||(action==='copy'&&state.favourites.length>=MAX_FAVOURITES);
  $('[data-action=favourite-up]').disabled=index<=0;
  $('[data-action=favourite-down]').disabled=index<0||index===state.favourites.length-1;
  $('[data-action=favourite-new]').disabled=state.favourites.length>=MAX_FAVOURITES;
  if(loadEditor)loadFavouriteEditor(selected);
  renderPreferenceStatus();
}
function openFavouriteManager(id){
  engine?.finishGesture();
  if(spacePan){spacePan=false;engine?.setTool(toolForEngine(state.tool));root.classList.remove('is-panning');ui('mode-status').textContent=state.annotationsVisible?names[state.tool]:'批注已隐藏 · 浏览';}
  if(ui('settings-dialog').open)ui('settings-dialog').close();
  favouriteEditorId=id||favouriteEditorId;favouriteNotice='编辑的是收藏预设，不修改当前 PDF 批注。';
  renderFavouriteManager();ui('favourites-dialog').showModal();
}
function favouritesChanged(message,{loadEditor=true}={}){
  state.favourites=ensureFavouriteIds(state.favourites);
  renderFavourites();renderFavouriteManager({loadEditor});setFavouriteNotice(message);schedulePersist();
}
function addFavourite(){
  const tool=activePresetTool();if(!FAVOURITE_TOOLS.includes(tool)){toast('先选择可收藏的笔、形状、文字或文本审阅工具');return;}
  if(state.favourites.length>=MAX_FAVOURITES){toast('收藏已达到当前 '+MAX_FAVOURITES+' 项处理上限');return;}
  try{
    const favourite=createFavourite(tool,currentStyle());
    state.favourites=ensureFavouriteIds([...state.favourites,favourite]);favouriteEditorId=state.favourites.at(-1).id;
    renderFavourites();schedulePersist();toast('已添加到收藏工具栏');
  }catch(error){toast(error.message);}
}
function newManagedFavourite(){
  if(state.favourites.length>=MAX_FAVOURITES||!discardFavouriteDraft())return;
  const tool=FAVOURITE_TOOLS.includes(activePresetTool())?activePresetTool():'pen';
  const favourite=createFavourite(tool,state.styles[tool]);
  state.favourites=ensureFavouriteIds([...state.favourites,favourite]);favouriteEditorId=state.favourites.at(-1).id;
  favouritesChanged('已新增收藏，可修改名称和样式。');
}
function saveManagedFavourite(){
  const selected=selectedFavourite();if(!selected)return;
  if(!ui('favourite-editor').reportValidity())return;
  try{
    const value=key=>$('[data-favourite-field='+key+']').value;
    const tool=value('tool');
    const patch={name:value('name'),tool,color:value('color'),width:tool==='text-highlight'?(selected.tool===tool?selected.width:defaultFavouriteStyle(tool).width):Number(value('width')),opacity:Number(value('opacity'))/100,pressure:tool==='pen'?$('[data-favourite-field=pressure]').checked:tool==='highlighter'&&selected.tool===tool?selected.pressure:false};
    if(['rectangle','ellipse',...textTools].includes(tool))patch.fill=$('[data-favourite-field=fill-enabled]').checked?value('fill-color'):'none';
    if(textTools.includes(tool))Object.assign(patch,{fontSize:Number(value('fontSize')),fontFamily:value('fontFamily'),bold:$('[data-favourite-field=bold]').checked,italic:$('[data-favourite-field=italic]').checked});
    const updated=updateFavourite(selected,patch);
    state.favourites=state.favourites.map(favourite=>favourite.id===updated.id?updated:favourite);
    favouritesChanged('收藏已修改；当前工具和 PDF 批注保持原样。');
  }catch(error){setFavouriteNotice(error.message);}
}
function copyManagedFavourite(){
  if(state.favourites.length>=MAX_FAVOURITES||!discardFavouriteDraft())return;
  const selected=selectedFavourite();if(!selected)return;
  const index=state.favourites.indexOf(selected),copy=duplicateFavourite(selected);
  const next=state.favourites.slice();next.splice(index+1,0,copy);state.favourites=ensureFavouriteIds(next);
  favouriteEditorId=state.favourites[index+1].id;favouritesChanged('已复制收藏，可独立编辑副本。');
}
function removeManagedFavourite(){
  if(!discardFavouriteDraft())return;
  const index=state.favourites.findIndex(favourite=>favourite.id===favouriteEditorId);if(index<0)return;
  state.favourites=removeFavourite(state.favourites,favouriteEditorId);
  favouriteEditorId=state.favourites[Math.min(index,state.favourites.length-1)]?.id||null;
  favouritesChanged('已删除收藏；PDF 批注未改变。');
}
function moveManagedFavourite(offset){
  if(!selectedFavourite())return;
  state.favourites=moveFavourite(state.favourites,favouriteEditorId,offset);
  favouritesChanged(favouriteDraftDirty?'收藏顺序已修改；编辑区的修改尚未保存。':'收藏顺序已修改。',{loadEditor:false});
}

/* SHARED_UI_INSERT */

function setBusy(active,label){root.classList.toggle('is-loading',active);ui('loading').hidden=!active;if(label)ui('loading').textContent=label;updateButtons();}
function updateButtons(){
  const ready=!!state.pdf&&!state.opening;
  $('[data-input=layout]').value=state.layout;$('[data-input=layout]').disabled=state.opening||state.rendering||layoutBusy||annotationVisibilityBusy;
  $$('[data-action=open]').forEach(button=>button.disabled=state.opening||state.saving||nativeDialogBusy||backupRestoreBusy||annotationVisibilityBusy);
  $$('[data-action=save],[data-action=save-as]').forEach(button=>button.disabled=!ready||state.saving||state.rendering||nativeDialogBusy||backupRestoreBusy||annotationVisibilityBusy);
  $('[data-action=previous]').disabled=!ready||navigationRestoring||annotationVisibilityBusy||(documentView?documentView.getAdjacentIndex(-1)===state.pageIndex:state.targetIndex<=0);
  $('[data-action=next]').disabled=!ready||navigationRestoring||annotationVisibilityBusy||(documentView?documentView.getAdjacentIndex(1)===state.pageIndex:state.targetIndex>=state.pages.length-1);
  $('[data-input=page]').disabled=!ready||navigationRestoring||annotationVisibilityBusy;
  $('[data-action=undo]').disabled=!canEditAnnotations()||!engine?.canUndo();
  $('[data-action=redo]').disabled=!canEditAnnotations()||!engine?.canRedo();
  $$('[data-action=zoom-in],[data-action=zoom-out],[data-input=zoom]').forEach(control=>control.disabled=state.zoomLocked||state.opening||state.rendering||annotationVisibilityBusy);
  $('[data-action=nav-back]').disabled=!ready||state.rendering||navigationRestoring||annotationVisibilityBusy||!navigationPast.length;
  $('[data-action=nav-forward]').disabled=!ready||state.rendering||navigationRestoring||annotationVisibilityBusy||!navigationFuture.length;
  updateBackupRestoreControls();updateAnnotationActions();updateAnnotationRows();updateBookmarkControls();
  renderDocumentTabs();
}
function updateDirtyLabel(label){ui('dirty-dot').hidden=!state.dirty;ui('save-status').classList.toggle('is-dirty',state.dirty);ui('save-status').textContent=label||(state.dirty?'有未保存的更改':state.pdf?'已打开本地文件':'尚未打开文件');document.title=(state.dirty?'● ':'')+(state.filename?state.filename+' — ':'')+'ProperNotes for Windows';renderDocumentTabs();}
function markDirty(){if(restoring||!state.pdf)return;state.revision++;state.dirty=true;updateDirtyLabel();backupScheduler?.markChanged({documentId:state.backupDocumentId||state.documentId,revision:state.sessionId+':'+state.revision});scheduleWorkspacePersist();}
function saveCurrentPage(){if(!state.pdf||!engine)return;if(documentView){documentView.finishAll();return;}engine.finishGesture();state.inkPages[state.pageIndex]=engine.getStrokes();state.histories.set(state.pageIndex,engine.exportHistory());}
/* DOCUMENT_WORKSPACE_INSERT */
/* INPUT_CONTROLS_INSERT */
/* ANNOTATION_MENU_INSERT */
function activatePageRecord(record,index){
  if(!record?.engine||record!==documentView?.getActiveRecord())return;
  engine=record.engine;state.pageIndex=index;state.targetIndex=index;pageTextNotice=record.textNotice||'';
  const selection=engine.getSelection();state.selection=selection.count;state.selectionLocked=selection.lockedCount||0;state.selectionTools=selection.tools||[];
  if(selection.style&&selection.count)Object.assign(state.styles.select,selection.style);
  updatePageLabels();renderTool();
  if(state.sidebar==='annotations'&&annotationFilter.currentPage)renderSidebar();
  else $$('[data-page]').forEach(button=>{const current=Number(button.dataset.page)===index;button.classList.toggle('is-active',current);button.setAttribute('aria-current',current?'page':'false');});
}
function createPageEngine(svg,index,record,isOwner){
  const belongs=()=>isOwner()&&documentView?.ownsRecord(record)&&!record.initializing;
  const instance=window.createInkPDFEngine(svg,{
    onChange:info=>{if(!belongs()||!state.pdf||restoring||!record.engine)return;state.inkPages[index]=record.engine.getStrokes();state.histories.set(index,record.engine.exportHistory());markDirty();if(index===state.pageIndex){ui('ink-count').textContent=info.count+' 条批注';updateButtons();}if(state.sidebar==='annotations')renderSidebar();},
    onSelection:info=>{if(!belongs()||record!==documentView.getActiveRecord())return;state.selection=info.count;state.selectionLocked=info.lockedCount||0;state.selectionTools=info.tools||[];if(info.style&&info.count)Object.assign(state.styles.select,info.style);renderTool();updateAnnotationRows();},
    onActivity:message=>{if(belongs()&&record===documentView.getActiveRecord()&&message)ui('input-status').textContent=message;},
    onCreateText:details=>{if(belongs())openTextEditor(record,details);},
    onEditText:details=>{if(belongs())openTextEditor(record,details);},
    onGestureEnd:()=>{if(belongs()){if(state.sidebar==='annotations')renderSidebar();updateButtons();}}
  });instance.setInputSettings(state.ink);return instance;
}
function makeDocumentView(){
  const pdf=state.pdf;let owner;
  const isOwner=()=>documentView===owner&&state.pdf===pdf;
  owner=createDocumentView({viewport,tray:paperTray,templateFrame:frameTemplate,pdf,metrics:state.pages,inkPages:state.inkPages,histories:state.histories,pdfjsLib,
    zoom:state.zoom,layout:state.layout,annotationsVisible:state.annotationsVisible,textSelectable:state.tool==='text',
    createEngine:(svg,index,record)=>createPageEngine(svg,index,record,isOwner),getTool:()=>toolForEngine(state.tool),getStyle:()=>({...currentStyle(),eraserMode:state.eraserMode}),renderDensity,
    canActivate:()=>!state.opening&&!state.rendering&&!layoutBusy&&!annotationVisibilityBusy&&!engine?.hasActiveGesture()&&!modalDialogs().some(dialog=>dialog.open),
    onActivate:(record,index)=>{if(isOwner())activatePageRecord(record,index);},
    onReady:record=>{if(isOwner()&&record===documentView?.getActiveRecord()){pageTextNotice=record.textNotice||'';renderTool();}},
    onError:(error,index,meta)=>{if(isOwner()&&!meta?.background&&error?.name!=='AbortError')showError(error);},
    onStatus:message=>{if(isOwner()&&typeof message==='string')ui('input-status').textContent=message;}
  });
  return owner;
}
function getState(){if(state.pdf&&!state.opening&&!state.rendering)saveCurrentPage();return{filename:state.filename,pageIndex:state.pageIndex,pageCount:state.pages.length,pages:clone(state.pages),inkPages:clone(state.inkPages),width:state.pages[state.pageIndex]?.width||0,height:state.pages[state.pageIndex]?.height||0,tool:state.tool,layout:state.layout,zoom:state.zoom,zoomLocked:state.zoomLocked,annotationsVisible:state.annotationsVisible,selection:state.selection,selectionLocked:state.selectionLocked||0,clipboardCount:annotationClipboard.length,navigationBack:navigationPast.length,navigationForward:navigationFuture.length,strokeCount:engine.getStrokes().length,strokes:engine.getStrokes(),dirty:state.dirty,saving:state.saving,opening:state.opening,rendering:state.rendering,sidebarVisible:state.showSidebar,propertiesVisible:state.showProperties,dock:state.dock};}
function updatePageLabels(){const total=state.pages.length;ui('filename').textContent=state.filename||'打开 PDF…';$('[data-input=page]').max=total||1;$('[data-input=page]').value=state.pdf?state.targetIndex+1:1;ui('page-denominator').textContent='/ '+total;ui('page-total').textContent=total+' 页';ui('ink-count').textContent=(state.inkPages[state.pageIndex]?.length||0)+' 条批注';updateButtons();}
function setZoom(value,mode='custom',{force=false,anchor=null}={}){
  if(state.zoomLocked&&!force)return false;
  const metric=state.pages[state.pageIndex]||{width:640,height:860},old=state.zoom;
  const ax=(viewport.scrollLeft+viewport.clientWidth/2)/old,ay=(viewport.scrollTop+viewport.clientHeight/2)/old;
  const next=Math.max(.1,Math.min(4,Number(value)||1));
  const fixedPoint=anchor?captureZoomPoint(anchor):null;
  try{documentView?.setZoom(next,{preserveAnchor:!fixedPoint});}catch(error){showError(error);return false;}
  state.zoom=next;state.zoomMode=mode;
  if(!documentView){ui('paper').style.transform='scale('+state.zoom+')';ui('page-frame').style.width=(metric.width*state.zoom)+'px';ui('page-frame').style.height=(metric.height*state.zoom)+'px';}
  const select=$('[data-input=zoom]');select.querySelector('[value=custom]').textContent=Math.round(state.zoom*100)+'%';
  select.value=['fit','whole','height'].includes(mode)?mode:String(state.zoom);if(select.selectedIndex<0)select.value='custom';
  if(!documentView){viewport.scrollLeft=Math.max(0,ax*state.zoom-viewport.clientWidth/2);viewport.scrollTop=Math.max(0,ay*state.zoom-viewport.clientHeight/2);}
  if(fixedPoint){const bounds=fixedPoint.element.getBoundingClientRect();viewport.scrollLeft+=bounds.left+fixedPoint.x*state.zoom-fixedPoint.clientX;viewport.scrollTop+=bounds.top+fixedPoint.y*state.zoom-fixedPoint.clientY;}
  clearTimeout(resolutionTimer);resolutionTimer=setTimeout(refreshResolution,180);scheduleWorkspacePersist();scheduleSelectionMenuPosition();return true;
}
function fitZoom(){if(!state.pdf||state.zoomLocked)return;const p=documentView?.getFitMetrics()||state.pages[state.pageIndex],padding=root.clientWidth<=720?40:68,fit=(viewport.clientWidth-padding-(p.gap||0))/p.width,height=(viewport.clientHeight-50)/p.height;setZoom(state.zoomMode==='whole'?Math.min(fit,height):state.zoomMode==='height'?height:fit,state.zoomMode);}
function fitIfNeeded(){if(['fit','whole','height'].includes(state.zoomMode))fitZoom();}
function renderDensity(view,zoom){return Math.min((window.devicePixelRatio||1)*Math.max(1,zoom),4,Math.sqrt(16000000/(view.width*view.height)),16384/Math.max(view.width,view.height));}
async function refreshResolution(){if(!state.opening&&!state.rendering)documentView?.refresh();}
function pageLabel(i){return '第 '+(i+1)+' 页';}
function renderSidebar(){
  thumbnailObserver?.disconnect();const container=ui('sidebar-content');
  ui('sidebar-heading').textContent={pages:'页面',bookmarks:'书签与目录',annotations:'批注'}[state.sidebar];container.replaceChildren();
  if(!state.pdf){const text=document.createElement('div');text.className='ip-sidebar-empty';text.textContent='打开 PDF 后显示文档内容。';container.append(text);return;}
  if(state.sidebar==='annotations'){renderAnnotationSidebar(container);return;}
  if(state.sidebar==='bookmarks'){renderBookmarksSidebar(container);return;}
  for(let index=0;index<state.pages.length;index++){
    const button=document.createElement('button');button.dataset.page=String(index);button.setAttribute('aria-label',pageLabel(index));
    button.className='ip-thumbnail'+(index===state.targetIndex?' is-active':'');button.setAttribute('aria-current',index===state.targetIndex?'page':'false');
    const paper=document.createElement('div');paper.className='ip-thumb-paper is-pending';paper.style.height=(91*state.pages[index].height/state.pages[index].width)+'px';paper.dataset.thumbnail=String(index);
    const label=document.createElement('span');label.className='ip-thumb-label';label.append(String(index+1));
    if(state.inkPages[index]?.length){const dot=document.createElement('span');dot.className='ip-thumb-dot';label.append(dot);}
    button.append(paper,label);container.append(button);
  }
  const generation=documentGeneration;thumbnailObserver=new IntersectionObserver(entries=>{
    for(const entry of entries)if(entry.isIntersecting){thumbnailObserver.unobserve(entry.target);renderThumbnail(Number(entry.target.dataset.thumbnail),entry.target,generation);}
  },{root:container,rootMargin:'160px'});container.querySelectorAll('[data-thumbnail]').forEach(element=>thumbnailObserver.observe(element));
}
function renderAnnotationSidebar(container){
  const filters=document.createElement('div');filters.className='ip-annotation-filters';
  function addFilter(title,key,options){
    const label=document.createElement('label'),caption=document.createElement('span'),select=document.createElement('select');caption.textContent=title;
    for(const [value,text] of options){const option=document.createElement('option');option.value=value;option.textContent=text;select.append(option);}
    select.value=annotationFilter[key];select.setAttribute('aria-label',title);select.addEventListener('change',()=>{annotationFilter[key]=select.value;annotationListOffset=0;renderRows();});
    label.append(caption,select);filters.append(label);
  }
  addFilter('批注类型','tool',[['all','全部类型'],...annotationTools.map(tool=>[tool,names[tool]])]);
  addFilter('锁定状态','lock',[['all','全部状态'],['locked','已锁定'],['unlocked','未锁定']]);
  const scopeLabel=document.createElement('label'),scope=document.createElement('input'),caption=document.createElement('span');scope.type='checkbox';scope.checked=annotationFilter.currentPage;caption.textContent='仅当前页';
  scope.addEventListener('change',()=>{annotationFilter.currentPage=scope.checked;annotationListOffset=0;renderRows();});scopeLabel.append(caption,scope);filters.append(scopeLabel);
  const summary=document.createElement('p');summary.className='ip-annotation-summary';const rows=document.createElement('div');rows.className='ip-annotation-list';
  container.append(filters,summary,rows);
  function renderRows(){
    rows.replaceChildren();let total=0;const visible=[],limit=100;
    for(let pageIndex=0;pageIndex<state.inkPages.length;pageIndex++){
      if(annotationFilter.currentPage&&pageIndex!==state.pageIndex)continue;
      for(const [index,stroke] of state.inkPages[pageIndex].entries()){
        if(annotationFilter.tool!=='all'&&stroke.tool!==annotationFilter.tool)continue;
        if(annotationFilter.lock==='locked'&&!stroke.locked||annotationFilter.lock==='unlocked'&&stroke.locked)continue;
        if(total>=annotationListOffset&&visible.length<limit)visible.push({pageIndex,index,stroke});total++;
      }
    }
    if(total&&annotationListOffset>=total){annotationListOffset=Math.floor((total-1)/limit)*limit;renderRows();return;}
    summary.textContent='本软件自有批注：筛选后 '+total+' 条'+(total?'；显示 '+(annotationListOffset+1)+'–'+(annotationListOffset+visible.length):'。');
    if(!state.annotationsVisible)summary.append(document.createTextNode('。批注已隐藏，显示后可定位编辑。'));
    for(const {pageIndex,index,stroke} of visible){
      const button=document.createElement('button');button.type='button';button.className='ip-annotation-row';button.dataset.annotationId=stroke.id;button.dataset.annotationPage=String(pageIndex);
      const title=document.createElement('strong'),detail=document.createElement('span');title.textContent=pageLabel(pageIndex)+' · '+(names[stroke.tool]||'批注')+' '+(index+1);
      detail.textContent=(stroke.locked?'已锁定 · ':'')+stroke.color+' · '+stroke.width+' px';button.append(title,detail);button.disabled=!state.annotationsVisible||state.opening||state.rendering;
      button.addEventListener('click',()=>locateAnnotation(pageIndex,stroke.id));rows.append(button);
    }
    if(total>limit){
      for(const [text,offset,disabled] of [['上一组',-limit,annotationListOffset===0],['下一组',limit,annotationListOffset+limit>=total]]){
        const button=document.createElement('button');button.type='button';button.className='ip-button';button.textContent=text;button.disabled=disabled;
        button.addEventListener('click',()=>{annotationListOffset=Math.max(0,annotationListOffset+offset);renderRows();});rows.append(button);
      }
    }
    updateAnnotationRows();
  }
  renderRows();
}
function updateAnnotationRows(){
  const ids=new Set(engine?.getSelection().ids||[]);
  $$('[data-annotation-id]').forEach(row=>{const selected=Number(row.dataset.annotationPage)===state.pageIndex&&ids.has(row.dataset.annotationId);row.classList.toggle('is-selected',selected);row.setAttribute('aria-pressed',String(selected));row.disabled=!canEditAnnotations()||navigationRestoring;});
}
async function locateAnnotation(pageIndex,id){
  if(!canEditAnnotations()||navigationRestoring)return;
  const generation=documentGeneration,before=captureNavigation(),samePage=pageIndex===state.pageIndex;
  ++navigationSerial;
  if(!samePage&&!await gotoPage(pageIndex))return;
  if(generation!==documentGeneration||pageIndex!==state.pageIndex||!canEditAnnotations())return;
  setTool('select');const selection=engine.selectAnnotations([id]);
  if(!selection.count){toast('此批注已不存在');return;}
  const bounds=selection.bounds,paper=ui('paper').getBoundingClientRect(),area=viewport.getBoundingClientRect();
  viewport.scrollLeft+=paper.left+(bounds.x+bounds.width/2)*state.zoom-(area.left+viewport.clientWidth/2);
  viewport.scrollTop+=paper.top+(bounds.y+bounds.height/2)*state.zoom-(area.top+viewport.clientHeight/2);
  if(samePage)recordNavigation(before);updateAnnotationRows();
}
function renderBookmarksSidebar(container){
  const notice=document.createElement('p');notice.className='ip-annotation-summary';notice.textContent=state.bookmarksMessage||(state.bookmarksEditable?'修改会随 PDF 保存到标准书签目录。':'本文档的书签暂不可编辑。');container.append(notice);
  const selected=findBookmark(bookmarkSelectedId),form=document.createElement('form');form.className='ip-bookmark-editor';form.setAttribute('aria-label','书签编辑');
  function inputField(text,input){const label=document.createElement('label'),caption=document.createElement('span');caption.textContent=text;label.append(caption,input);form.append(label);input.dataset.bookmarkEdit='';}
  const draft=bookmarkEditorDraft?.selectionId===bookmarkSelectedId?bookmarkEditorDraft:null;
  const title=document.createElement('input');title.type='text';title.required=true;title.maxLength=512;title.value=draft?.title??selected?.title??pageLabel(state.pageIndex);inputField('书签标题',title);
  const initialPage=draft?.pageBase??String(selected?.pageIndex!==null&&Number.isInteger(selected?.pageIndex)?selected.pageIndex+1:state.pageIndex+1);
  const page=document.createElement('input');page.type='number';page.required=true;page.min='1';page.max=String(state.pages.length);page.step='1';page.value=draft?.page??initialPage;inputField(selected?'目标页（明确修改时生效）':'新书签目标页',page);
  if(selected){const targetHint=document.createElement('small');targetHint.textContent='仅修改标题会保留原目标；“修改目标页”会改为所填页的整页视图。';form.append(targetHint);}
  const draftStatus=document.createElement('small');draftStatus.textContent=draft?.dirty?'编辑尚未应用到书签。':'';form.append(draftStatus);
  form.addEventListener('input',()=>{++navigationSerial;bookmarkEditorDraft={selectionId:bookmarkSelectedId,title:title.value,page:page.value,pageBase:initialPage,dirty:true};draftStatus.textContent='编辑尚未应用到书签。';});
  function action(text,handler,needsSelection=false){
    const button=document.createElement('button');button.type='button';button.className='ip-button';button.textContent=text;button.dataset.bookmarkEdit='';if(needsSelection)button.dataset.bookmarkNeedsSelection='';
    button.addEventListener('click',handler);form.append(button);
  }
  function add(parentId){if(!form.reportValidity())return;const id=window.ProperNotesBookmarks.createId();applyBookmarkEdit({type:'add',id,parentId,title:title.value,pageIndex:Number(page.value)-1},id);}
  function renameSelected(){
    if(!selected||!title.reportValidity())return;
    const remaining=page.value!==initialPage?{selectionId:selected.id,title:title.value.trim(),page:page.value,pageBase:initialPage,dirty:true}:null;
    applyBookmarkEdit({type:'rename',id:selected.id,title:title.value},selected.id,{draft:remaining});
  }
  function retargetSelected(){
    if(!selected||!page.reportValidity())return;
    const pageIndex=Number(page.value)-1;
    if(selected.targetType!=='page'&&!window.confirm('将书签“'+selected.title+'”的现有目标替换为第 '+(pageIndex+1)+' 页整页视图？仅此书签的目标会改变。'))return;
    const remaining=title.value!==selected.title?{selectionId:selected.id,title:title.value,page:String(pageIndex+1),pageBase:String(pageIndex+1),dirty:true}:null;
    applyBookmarkEdit({type:'set-target',id:selected.id,pageIndex},selected.id,{draft:remaining});
  }
  action('添加到顶层',()=>add(null));action('添加为子项',()=>add(bookmarkSelectedId),true);
  action('修改标题',renameSelected,true);action('修改目标页',retargetSelected,true);
  action('放弃输入',()=>{bookmarkEditorDraft=null;renderSidebar();});
  action('删除所选',()=>{const node=findBookmark(bookmarkSelectedId);if(node&&window.confirm('删除书签“'+node.title+'”'+(node.children.length?'及其子书签':'')+'？PDF 页面与批注会保留。'))applyBookmarkEdit({type:'delete',id:node.id},null);},true);
  form.noValidate=true;
  form.addEventListener('keydown',event=>{
    if(event.key!=='Enter'||event.repeat||event.isComposing||event.keyCode===229||event.ctrlKey||event.altKey||event.metaKey||event.shiftKey||![title,page].includes(event.target))return;
    event.preventDefault();if(!canEditBookmarks())return;
    if(selected){if(event.target===page)retargetSelected();else renameSelected();}else add(null);
  });
  form.addEventListener('submit',event=>{event.preventDefault();if(selected){if(document.activeElement===page)retargetSelected();else renameSelected();}else add(null);});container.append(form);
  if(state.bookmarks.length){
    const list=document.createElement('ol');list.className='ip-bookmark-tree';
    function append(nodes,parent){
      for(const node of nodes){
        const item=document.createElement('li'),row=document.createElement('div');row.className='ip-bookmark-row';
        const expanded=state.bookmarksEditable?node.expanded:(readonlyBookmarkExpansion.get(node.id)??node.expanded);
        if(node.children.length){const toggle=document.createElement('button');toggle.type='button';toggle.className='ip-icon-button ip-small-icon';toggle.textContent=expanded?'−':'+';toggle.setAttribute('aria-label',(expanded?'折叠':'展开')+'书签 '+node.title);toggle.setAttribute('aria-expanded',String(expanded));
          if(state.bookmarksEditable){toggle.dataset.bookmarkEdit='';toggle.addEventListener('click',()=>applyBookmarkEdit({type:'set-expanded',id:node.id,expanded:!expanded}));}
          else{toggle.dataset.bookmarkReadToggle='';toggle.addEventListener('click',()=>{readonlyBookmarkExpansion.set(node.id,!expanded);renderSidebar();});}
          row.append(toggle);}
        const button=document.createElement('button');button.type='button';button.className='ip-bookmark-title';button.dataset.bookmarkSelect=node.id;button.setAttribute('aria-pressed',String(node.id===bookmarkSelectedId));
        button.textContent=node.title+(node.pageIndex===null?(node.namedDestination?' · 命名目标':' · 其他目标'):' · '+(node.pageIndex+1));
        button.addEventListener('click',()=>{if(state.opening||state.rendering||navigationRestoring||annotationVisibilityBusy)return;if(bookmarkSelectedId!==node.id&&!discardBookmarkDraft())return;++navigationSerial;bookmarkSelectedId=node.id;renderSidebar();if(node.pageIndex!==null)gotoPage(node.pageIndex);else if(node.namedDestination)navigateDestination(node.namedDestination);else toast('保留了此书签的原目标，当前暂不能跳转');});
        row.append(button);item.append(row);
        if(expanded&&node.children.length){const children=document.createElement('ol');children.className='ip-bookmark-tree';append(node.children,children);item.append(children);}
        parent.append(item);
      }
    }
    append(state.bookmarks,list);container.append(list);
  }else if(!state.bookmarksEditable&&state.outline.length){
    for(const item of state.outline){const button=document.createElement('button');button.className='ip-sidebar-row';button.style.paddingLeft=(8+Math.min(item.depth,3)*8)+'px';button.textContent=item.title;button.dataset.bookmarkSelect='';button.addEventListener('click',()=>navigateDestination(item.dest));container.append(button);}
  }else{const empty=document.createElement('p');empty.className='ip-sidebar-empty';empty.textContent='还没有书签，可为当前页添加。';container.append(empty);}
  updateBookmarkControls();
}
function findBookmark(id,nodes=state.bookmarks){for(const node of nodes){if(node.id===id)return node;const child=findBookmark(id,node.children);if(child)return child;}return null;}
function discardBookmarkDraft(){if(bookmarkEditorDraft?.dirty&&!window.confirm('书签编辑尚未应用。确定放弃这些输入吗？'))return false;bookmarkEditorDraft=null;return true;}
function canEditBookmarks(){return !!state.pdf&&state.bookmarksEditable&&!state.opening&&!state.rendering&&!navigationRestoring&&!annotationVisibilityBusy&&!layoutBusy;}
function updateBookmarkControls(){
  const selected=!!findBookmark(bookmarkSelectedId);
  $$('[data-bookmark-edit]').forEach(control=>control.disabled=!canEditBookmarks()||control.hasAttribute('data-bookmark-needs-selection')&&!selected);
  $$('[data-bookmark-select],[data-bookmark-read-toggle]').forEach(control=>control.disabled=!state.pdf||state.opening||state.rendering||navigationRestoring||annotationVisibilityBusy);
}
function applyBookmarkEdit(edit,selectedId=bookmarkSelectedId,{draft=null}={}){
  if(!canEditBookmarks())return;
  try{
    const edits=[edit];if(edit.type==='add'&&edit.parentId!==null&&!findBookmark(edit.parentId)?.expanded)edits.push({type:'set-expanded',id:edit.parentId,expanded:true});
    const next=window.ProperNotesBookmarks.applyEdits(state.bookmarks,edits,{pageCount:state.pages.length});
    if(['rename','set-expanded'].includes(edit.type)&&JSON.stringify(next)===JSON.stringify(state.bookmarks)){if(edit.type!=='set-expanded')bookmarkEditorDraft=draft;renderSidebar();return;}
    if(state.bookmarkEdits.length+edits.length>window.ProperNotesBookmarks.MAX_EDITS)throw new Error('书签操作记录已达当前上限，请保存并重新打开后继续。');
    engine.finishGesture();state.bookmarks=next;state.bookmarkEdits=[...state.bookmarkEdits,...edits];bookmarkSelectedId=selectedId;if(edit.type!=='set-expanded')bookmarkEditorDraft=draft;markDirty();renderSidebar();
  }catch(error){showError(error);}
}
async function renderThumbnail(index,element,generation){try{const doc=state.pdf,page=await doc.getPage(index+1);if(generation!==documentGeneration||!element.isConnected)return;const v=page.getViewport({scale:91/page.getViewport({scale:1}).width}),canvas=document.createElement('canvas');canvas.width=Math.ceil(v.width*1.5);canvas.height=Math.ceil(v.height*1.5);await page.render({canvasContext:canvas.getContext('2d'),viewport:v,transform:[1.5,0,0,1.5,0,0],annotationMode:annotationRenderMode()}).promise;if(generation!==documentGeneration||!element.isConnected)return;element.replaceChildren(canvas);element.classList.remove('is-pending');}catch{/* Thumbnail failure never prevents reading. */}}
async function navigateDestination(destination){
  if(!state.pdf||state.opening||state.rendering||navigationRestoring||annotationVisibilityBusy||layoutBusy)return;
  const pdf=state.pdf,generation=documentGeneration,serial=++navigationSerial;
  const current=()=>pdf===state.pdf&&generation===documentGeneration&&serial===navigationSerial&&!state.opening&&!navigationRestoring&&!annotationVisibilityBusy&&!layoutBusy;
  try{
    const dest=typeof destination==='string'?await pdf.getDestination(destination):destination;
    if(!current())return;if(!Array.isArray(dest)){toast('无法解析此书签目标，原目标仍保留');return;}
    if(typeof dest[0]==='number'){toast('此目标没有可核对的本地页面引用，原目标仍保留');return;}
    const index=await pdf.getPageIndex(dest[0]);if(!current())return;
    if(!Number.isInteger(index)||index<0||index>=state.pages.length){toast('此书签没有可跳转的本地页面，原目标仍保留');return;}
    await gotoPage(index);
  }catch(error){if(current())showError(error);}
}
async function readOutline(pdf,generation){try{const outline=await pdf.getOutline();if(generation!==documentGeneration)return;const result=[],seen=new Set();let visited=0;function walk(items,depth){if(!Array.isArray(items)||depth>48)return;for(const item of items){if(result.length>=500||++visited>5000)break;if(!item||seen.has(item))continue;seen.add(item);if(item.dest)result.push({title:String(item.title||'未命名目录'),dest:item.dest,depth});walk(item.items,depth+1);}}walk(outline,0);state.outline=result;if(state.sidebar==='bookmarks')renderSidebar();}catch{/* Outline is optional. */}}
async function preparePageText(page,view,generation){
  const container=document.createElement('div');container.className='ip-text-layer textLayer';container.dataset.ui='pdf-text';
  container.hidden=state.tool!=='text';container.setAttribute('aria-label','PDF 正文文字');
  const totalScale=view.scale*view.userUnit;
  container.style.setProperty('--total-scale-factor',String(totalScale));
  container.style.setProperty('--scale-round-x','1px');container.style.setProperty('--scale-round-y','1px');
  let layer;
  try{
    const text=await page.getTextContent({includeMarkedContent:true,disableNormalization:true});
    if(generation!==renderGeneration)return null;
    layer=new pdfjsLib.TextLayer({textContentSource:text,container,viewport:view});pageTextTask=layer;
    // The outer paper applies zoom. TextLayer stays at scale 1, in unrotated raw dimensions.
    container.style.width=(view.rawDims.pageWidth*totalScale)+'px';container.style.height=(view.rawDims.pageHeight*totalScale)+'px';
    await layer.render();
    if(generation!==renderGeneration)return null;
    return{container,notice:text.items.some(item=>typeof item.str==='string'&&/\S/u.test(item.str))?'':'此页没有可选择的文本层；未进行 OCR。'};
  }catch(error){
    if(generation!==renderGeneration)return null;
    container.replaceChildren();return{container,notice:'此页文字层无法读取，仍可阅读页面和批注。'};
  }finally{if(pageTextTask===layer)pageTextTask=null;}
}
/* PAGE_RENDERER_INSERT */
function captureNavigation(){const anchor=documentView?.captureAnchor();return{pageIndex:anchor?.index??state.pageIndex,zoom:state.zoom,zoomMode:state.zoomMode,layout:state.layout,anchor,scrollLeft:viewport.scrollLeft,scrollTop:viewport.scrollTop};}
function sameNavigation(first,second){return first&&second&&first.pageIndex===second.pageIndex&&Math.abs(first.zoom-second.zoom)<.001&&Math.abs(first.scrollLeft-second.scrollLeft)<2&&Math.abs(first.scrollTop-second.scrollTop)<2;}
function recordNavigation(previous){
  if(navigationRestoring||sameNavigation(previous,captureNavigation()))return;
  if(!sameNavigation(navigationPast[navigationPast.length-1],previous))navigationPast.push(previous);
  if(navigationPast.length>100)navigationPast.shift();navigationFuture=[];updateButtons();
}
async function gotoPage(index){
  if(!state.pdf||state.opening||navigationRestoring||annotationVisibilityBusy||layoutBusy)return false;
  const generation=documentGeneration,serial=++navigationSerial,previous=captureNavigation();
  const success=await renderPage(index);
  if(success&&generation===documentGeneration&&serial===navigationSerial)recordNavigation(previous);
  return success&&generation===documentGeneration&&serial===navigationSerial;
}
async function navigateHistory(direction){
  if(!state.pdf||state.opening||state.rendering||navigationRestoring||annotationVisibilityBusy||layoutBusy)return;
  const source=direction==='back'?navigationPast:navigationFuture,target=source[source.length-1];if(!target)return;
  const current=captureNavigation(),generation=documentGeneration;navigationRestoring=true;++navigationSerial;updateButtons();
  try{
    if(target.layout&&target.layout!==state.layout){await documentView.setLayout(target.layout);state.layout=target.layout;}
    if(!await renderPage(target.pageIndex)){if(generation===documentGeneration)throw new Error('无法恢复此历史页面，已保留原阅读位置。');return;}
    if(generation!==documentGeneration)return;
    if(!state.zoomLocked&&!setZoom(target.zoom,target.zoomMode))throw new Error('无法恢复历史倍率，当前页面与批注仍保留。');
    if(target.anchor)documentView.restoreAnchor(target.anchor);
    else if(!target.layout||target.layout===state.layout){const ratio=state.zoom/target.zoom;viewport.scrollLeft=target.scrollLeft*ratio;viewport.scrollTop=target.scrollTop*ratio;}
    source.pop();const destination=direction==='back'?navigationFuture:navigationPast;destination.push(current);if(destination.length>100)destination.shift();
    if(current.layout!==state.layout)schedulePersist();
  }catch(error){
    if(generation===documentGeneration){try{if(state.layout!==current.layout){await documentView.setLayout(current.layout);state.layout=current.layout;}await documentView.show(current.pageIndex,{preserveView:true});if(current.anchor)documentView.restoreAnchor(current.anchor);}catch{/* The original page models remain intact. */}showError(error);}
  }finally{navigationRestoring=false;updateButtons();}
}
function setSearchBusy(busy){
  $('[data-action=search-run]').disabled=busy||!state.pdf;
  $('[data-action=search-stop]').disabled=!busy;
  $('[data-search=query]').disabled=busy;$('[data-search=case-sensitive]').disabled=busy;
}
function stopTextSearch(){
  searchController?.abort();searchController=null;++searchGeneration;setSearchBusy(false);
}
function openTextSearch(){
  if(!commitPendingAnnotationField())return;
  engine?.finishGesture();
  if(spacePan){spacePan=false;engine?.setTool(toolForEngine(state.tool));root.classList.remove('is-panning');ui('mode-status').textContent=state.annotationsVisible?names[state.tool]:'批注已隐藏 · 浏览';}
  const dialog=ui('search-dialog');if(!dialog.open)dialog.showModal();
  if(!state.pdf)ui('search-status').textContent='请先打开 PDF。';
  setSearchBusy(!!searchController);$('[data-search=query]').focus();
}
function renderTextSearchResults(result){
  const list=ui('search-results');list.replaceChildren();
  for(const match of result.results){
    const button=document.createElement('button');button.type='button';button.className='ip-search-result';button.dataset.searchPage=String(match.pageIndex);
    const heading=document.createElement('strong'),excerpt=document.createElement('span');
    heading.textContent='第 '+(match.pageIndex+1)+' 页';excerpt.textContent=match.excerpt;
    button.append(heading,excerpt);list.append(button);
  }
  const failures=result.failedPages||[];
  let message=result.results.length?'找到 '+result.results.length+' 处匹配，点击结果跳到对应页面。':'没有找到匹配文字。';
  if(result.truncated)message+=' 已达到结果数量上限，搜索未继续完成，结果可能不完整。';
  if(failures.length)message+=' 有 '+failures.length+' 页未能读取文字层，搜索不完整（页码：'+failures.slice(0,10).map(item=>item.pageIndex+1).join('、')+(failures.length>10?'…':'')+'）。';
  ui('search-status').textContent=message;
}
async function runTextSearch(){
  if(!state.pdf||state.opening)return;
  const query=$('[data-search=query]').value;
  if(!query.trim()){stopTextSearch();ui('search-results').replaceChildren();ui('search-status').textContent='请输入要搜索的文字。';return;}
  stopTextSearch();const serial=searchGeneration,documentSerial=documentGeneration,pdf=state.pdf;
  const controller=new AbortController();searchController=controller;setSearchBusy(true);ui('search-results').replaceChildren();
  ui('search-status').textContent='正在读取 PDF 已有文本层…';
  try{
    const result=await searchPDFText(pdf,query,{caseSensitive:$('[data-search=case-sensitive]').checked,signal:controller.signal,maxResults:1000,onProgress:progress=>{
      if(serial===searchGeneration&&documentSerial===documentGeneration)ui('search-status').textContent='已处理 '+progress.processedPages+' / '+progress.pageCount+' 页，找到 '+progress.resultCount+' 处。';
    }});
    if(serial===searchGeneration&&documentSerial===documentGeneration)renderTextSearchResults(result);
  }catch(error){if(serial===searchGeneration)ui('search-status').textContent=error?.name==='AbortError'?'搜索已停止。':'搜索失败：'+(error?.message||String(error));}
  finally{if(serial===searchGeneration){searchController=null;setSearchBusy(false);}}
}
async function openBytes(input,name='document.pdf',options={}){
  if(state.opening||state.saving||state.rendering||tabBusy||annotationVisibilityBusy||navigationRestoring||layoutBusy)throw new Error('请等待当前文件操作完成。');
  if(!commitPendingAnnotationField())return false;
  const initialOpenSerial=fileOpenSerial,initialDocumentGeneration=documentGeneration;
  const originalRestoreTab=options.restoreTab;
  const preflightCurrent=()=>initialOpenSerial===fileOpenSerial&&initialDocumentGeneration===documentGeneration&&!state.opening&&!state.saving&&!state.rendering&&!tabBusy&&!annotationVisibilityBusy&&!navigationRestoring&&!layoutBusy;
  const selectedData=input instanceof Uint8Array?input.slice():new Uint8Array(input).slice();
  const verifiedFingerprint=options.browserRecord?await fingerprintBytes(selectedData):null;
  if(!preflightCurrent())throw new Error('已有较新的文件或标签操作，未打开先前选择的 PDF。');
  if(options.browserRecord&&(!verifiedFingerprint||verifiedFingerprint!==options.browserRecord.fingerprint))throw new Error('选择的文件内容与这条浏览器记录不一致，未套用旧阅读状态。请通过普通“打开 PDF”将它打开为新文档。');
  if(options.nativeFileToken){const duplicate=documentTabs.find(tab=>tab.nativeId===options.backupDocumentId&&tab!==options.restoreTab);if(duplicate?.model)return activateExistingDocument(duplicate,options.restoreTab);if(duplicate)options={...options,restoreTab:duplicate,reading:duplicate.reading};}
  if(options.handle){for(const tab of documentTabs){if(tab===options.restoreTab||!tab.model?.handle)continue;const handle=tab.model.handle,same=handle===options.handle||(typeof handle.isSameEntry==='function'&&await handle.isSameEntry(options.handle));if(!preflightCurrent())throw new Error('文档已切换，未打开先前选择的文件。');if(same)return activateExistingDocument(tab,options.restoreTab);}}
  if(!options.restoreTab&&documentTabs.length>=32)throw new Error('当前最多保留 32 个标签，请先关闭一些文件。');
  saveCurrentPage();
  if(!discardBookmarkDraft())return false;
  if(ui('text-editor-dialog').open){if(!discardTextEditor())return false;ui('text-editor-dialog').close();}
  stopTextSearch();ui('search-results').replaceChildren();ui('search-status').textContent='输入文字搜索 PDF 已有文本层。';
  captureActiveTab();clearCanvasNavigation();++workspaceInteraction;hideError();state.opening=true;setBusy(true,'正在读取 PDF…');
  let nextPdf=null,previousModel=null,previousNavigation=null;
  try{
    const data=selectedData;
    const fingerprint=verifiedFingerprint||(!options.nativeFileToken?await fingerprintBytes(data):null);
    const reading=options.reading||options.browserRecord?.reading||(options.nativeFileToken?workspaceRecords().find(entry=>entry.id===options.backupDocumentId)?.reading:null);
    const opened=await window.InkPDFStorage.open(data);
    const task=pdfjsLib.getDocument({data:opened.sourceBytes.slice(),cMapUrl:new URL('./vendor/pdfjs/cmaps/',import.meta.url).href,cMapPacked:true,standardFontDataUrl:new URL('./vendor/pdfjs/standard_fonts/',import.meta.url).href,wasmUrl:new URL('./vendor/pdfjs/wasm/',import.meta.url).href,iccUrl:new URL('./vendor/pdfjs/iccs/',import.meta.url).href,isEvalSupported:false});
    nextPdf=await task.promise;saveCurrentPage();
    previousModel=captureDocumentModel();
    previousNavigation={past:navigationPast,future:navigationFuture,selectedBookmark:bookmarkSelectedId,readonlyExpansion:readonlyBookmarkExpansion,listOffset:annotationListOffset};navigationPast=[];navigationFuture=[];++navigationSerial;
    renderTask?.cancel();resolutionTask?.cancel();pageTextTask?.cancel();++renderGeneration;++documentGeneration;
    state.pdf=nextPdf;state.sourceBytes=opened.sourceBytes;state.pages=opened.pages;state.inkPages=opened.inkPages;
    state.bookmarks=opened.bookmarks||[];state.bookmarksEditable=opened.bookmarksEditable===true;state.bookmarksMessage=opened.bookmarksMessage||'';state.bookmarkEdits=[];bookmarkSelectedId=null;readonlyBookmarkExpansion=new Map();annotationListOffset=0;
    state.filename=String(name||'document.pdf');state.handle=options.handle||null;
    state.documentId=opened.documentId;state.backupDocumentId=options.backupDocumentId||opened.documentId;state.sessionId=newSessionId();state.sourcePath=typeof options.sourcePath==='string'?options.sourcePath:null;state.nativeFileToken=options.nativeFileToken||null;
    // Retain the bytes read from disk, not the rebuilt editing baseline.
    state.fileBaseline=state.handle?data:null;
    state.pageIndex=0;state.targetIndex=0;state.histories=new Map();state.outline=[];state.revision=0;state.dirty=false;state.selection=0;
    state.layout=reading?.layout||state.layout;state.zoom=reading?.zoom||state.zoom;state.zoomMode=state.zoomLocked?'custom':reading?.zoomMode||'fit';
    if(!await renderPage(reading?.pageIndex||0,{newDocument:true}))throw new Error('无法显示此 PDF 的页面，原文件和原有批注仍保留。');
    await restoreReading(reading);
    let browserId=null;
    if(!options.nativeFileToken&&fingerprint){try{const registration=browserWorkspace.register({id:options.browserRecord?.id,filename:state.filename,fingerprint});browserId=registration.entry.id;browserWorkspaceSnapshot=registration.workspace;}catch(error){renderWorkspaceStatus(error.message);}}
    const previouslyUnsaved=!!options.restoreTab?.dirtyOnRestore||!!originalRestoreTab?.dirtyOnRestore;
    const openedTab=commitOpenedTab({nativeId:options.nativeFileToken?options.backupDocumentId:null,browserId,restoreTab:options.restoreTab,reading:captureNavigation(),fingerprint});
    if(originalRestoreTab&&originalRestoreTab!==openedTab&&!originalRestoreTab.model){const index=documentTabs.indexOf(originalRestoreTab);if(index>=0)documentTabs.splice(index,1);renderDocumentTabs();scheduleWorkspacePersist();}
    updateDirtyLabel(opened.restored?'已恢复可编辑笔迹':'已打开本地文件');
    readOutline(nextPdf,documentGeneration);
    if(options.workspaceWarning)renderWorkspaceStatus(options.workspaceWarning);
    if(previouslyUnsaved)toast('已读取磁盘版本；上次未保存的批注请从本地备份核对');else if(opened.restored)toast('已从 PDF 恢复笔迹，可以继续编辑');
    return true;
  }catch(error){
    if(previousModel&&state.pdf===nextPdf){Object.assign(state,previousModel);navigationPast=previousNavigation.past;navigationFuture=previousNavigation.future;bookmarkSelectedId=previousNavigation.selectedBookmark;readonlyBookmarkExpansion=previousNavigation.readonlyExpansion;annotationListOffset=previousNavigation.listOffset;++documentGeneration;if(documentView)activatePageRecord(documentView.getActiveRecord(),documentView.getActiveRecord().index);renderSidebar();renderTool();updatePageLabels();updateDirtyLabel();ui('empty').hidden=!!state.pdf;ui('page-frame').hidden=!state.pdf;$('.ip-favourites').hidden=!state.pdf;}
    if(nextPdf&&nextPdf!==state.pdf)nextPdf.destroy().catch(()=>{});
    showError(error);throw error;
  }finally{state.opening=false;setBusy(state.rendering);updatePageLabels();}
}
async function chooseFile(options={}){
  if(state.opening||state.saving||state.rendering||tabBusy||nativeDialogBusy||backupRestoreBusy||navigationRestoring||annotationVisibilityBusy||layoutBusy)return;
  if(!commitPendingAnnotationField())return;
  pendingBrowserSelection=null;++workspaceInteraction;
  const chosenGeneration=documentGeneration,chosenSerial=++fileOpenSerial;
  hideError();nativeDialogBusy=true;updateButtons();
  try{
    try{
      const selected=await fileClient.open();nativeFilesAvailable=true;
      if(chosenGeneration!==documentGeneration||chosenSerial!==fileOpenSerial){toast('文档已切换，未打开先前选择的文件');return;}
      if(selected)await openBytes(selected.bytes,selected.filename,{...options,sourcePath:selected.sourcePath,backupDocumentId:selected.backupDocumentId,nativeFileToken:selected.token,workspaceWarning:selected.workspaceWarning});
      return;
    }catch(error){
      if(!['NATIVE_DIALOG_UNAVAILABLE'].includes(error.code)&&![404,405].includes(error.status))throw error;
      nativeFilesAvailable=false;
    }
    if(typeof window.showOpenFilePicker==='function'){
      try{
        const [handle]=await window.showOpenFilePicker({multiple:false,types:[{description:'PDF 文档',accept:{'application/pdf':['.pdf']}}]});
        const file=await handle.getFile(),bytes=new Uint8Array(await file.arrayBuffer());
        if(chosenGeneration!==documentGeneration||chosenSerial!==fileOpenSerial){toast('文档已切换，未打开先前选择的文件');return;}
        await openBytes(bytes,file.name,{...options,handle});return;
      }catch(error){if(error?.name==='AbortError')return;if(error?.name!=='SecurityError'&&error?.name!=='NotAllowedError')throw error;}
    }
    pendingBrowserSelection=options;$('[data-input=pdf-file]').click();
  }catch(error){showError(error);}
  finally{nativeDialogBusy=false;updateButtons();}
}
async function openBrowserFile(file,options={}){
  if(!file)return;
  if(nativeDialogBusy||backupRestoreBusy||tabBusy||state.opening||state.saving||state.rendering||navigationRestoring||annotationVisibilityBusy||layoutBusy){toast('请等待当前文件操作完成');return;}
  const serial=++fileOpenSerial,generation=documentGeneration;
  try{
    const bytes=new Uint8Array(await file.arrayBuffer());
    if(serial!==fileOpenSerial||generation!==documentGeneration||nativeDialogBusy||backupRestoreBusy||state.opening||state.saving||navigationRestoring||annotationVisibilityBusy||layoutBusy){toast('已有较新的文件操作，未打开先前读取的文件');return;}
    await openBytes(bytes,file.name,options);
  }catch(error){showError(error);}
}
async function exportBytes(){
  if(!state.pdf||state.opening||state.rendering||layoutBusy)throw new Error('请先打开 PDF 并等待页面显示完成。');saveCurrentPage();
  const snapshot={sourceBytes:state.sourceBytes,documentId:state.documentId,pages:clone(state.pages),bookmarkEdits:clone(state.bookmarkEdits)};
  snapshot.inkPages=await window.ProperNotesText.preparePages(state.inkPages);
  return window.InkPDFStorage.save(snapshot);
}
function suggestedName(){const name=state.filename||'document.pdf';return /\.批注\.pdf$/i.test(name)?name:name.replace(/\.pdf$/i,'')+'.批注.pdf';}
async function assertTargetUnchanged(target){
  if(!state.handle||!state.fileBaseline)return;
  const same=target===state.handle||(typeof target.isSameEntry==='function'&&await target.isSameEntry(state.handle));
  if(!same)return;
  const file=await target.getFile();
  if(file.size!==state.fileBaseline.length)throw new Error('原 PDF 在打开后已被外部修改，已停止覆盖。请另存到不同文件，再重新打开原文件核对外部更改。');
  const current=new Uint8Array(await file.arrayBuffer());
  if(current.length!==state.fileBaseline.length||current.some((value,index)=>value!==state.fileBaseline[index]))throw new Error('原 PDF 在打开后已被外部修改，已停止覆盖。请另存到不同文件，再重新打开原文件核对外部更改。');
}
async function savePdf(saveAs=false){
  if(!state.pdf||state.opening||state.saving||state.rendering||nativeDialogBusy||backupRestoreBusy||layoutBusy)return;
  if(bookmarkEditorDraft?.dirty){toast('书签输入尚未应用；请先应用修改或在书签面板中“放弃输入”，再保存 PDF');return;}
  if(!commitPendingAnnotationField())return;
  saveCurrentPage();captureActiveTab();if(saveAs)persistTabReading(activeDocumentTab());state.saving=true;updateButtons();
  const saveGeneration=documentGeneration;hideError();let target=saveAs?null:state.handle;
  try{
    if(nativeWindowBridge||(nativeFilesAvailable!==false&&(state.nativeFileToken||!state.handle))){
      updateDirtyLabel('正在准备 PDF…');saveCurrentPage();const revision=state.revision;
      const output=await exportBytes();
      if(saveGeneration!==documentGeneration)throw new Error('文档已切换，已取消本次保存。');
      let selected;
      try{selected=await fileClient.save({bytes:output,token:state.nativeFileToken||null,saveAs,filename:suggestedName(),excludeTokens:documentTabs.filter(tab=>tab.id!==activeTabId).map(tab=>tab.model?.nativeFileToken).filter(Boolean),excludeDocumentIds:documentTabs.filter(tab=>tab.id!==activeTabId).map(tab=>tab.nativeId).filter(Boolean)});}
      catch(error){
        if(error.code==='NATIVE_DIALOG_UNAVAILABLE'||[404,405].includes(error.status)){nativeFilesAvailable=false;throw new Error(error.message+(nativeWindowBridge?' 独立窗口的文件保存入口不可用，未保存的批注仍保留；请核对本机文件服务后重试。':' 请再次保存，以使用浏览器文件入口。'));}
        throw error;
      }
      if(!selected){updateDirtyLabel();return;}
      nativeFilesAvailable=true;state.nativeFileToken=selected.token;state.sourcePath=selected.sourcePath;state.backupDocumentId=selected.backupDocumentId;
      state.filename=selected.filename;state.handle=null;state.fileBaseline=null;state.dirty=state.revision!==revision;
      const tab=activeDocumentTab();if(tab){tab.nativeId=selected.backupDocumentId;tab.browserId=null;tab.sourcePath=selected.sourcePath;tab.filename=selected.filename;}
      if(selected.workspaceWarning)renderWorkspaceStatus(selected.workspaceWarning);
      if(state.revision)backupScheduler?.markChanged({documentId:state.backupDocumentId||state.documentId,revision:state.sessionId+':'+state.revision});
      updateDirtyLabel(state.dirty?'已保存，另有新更改未保存':'PDF 已保存');updatePageLabels();toast('PDF 已保存，重新打开可继续编辑');return;
    }
    if(nativeWindowBridge)throw new Error('独立窗口的本机文件保存入口不可用，未保存的批注仍保留；没有改用浏览器下载。');
    if(!target&&typeof window.showSaveFilePicker==='function')target=await window.showSaveFilePicker({suggestedName:suggestedName(),types:[{description:'PDF 文档',accept:{'application/pdf':['.pdf']}}]});
    if(target&&typeof target.queryPermission==='function'){
      let granted=await target.queryPermission({mode:'readwrite'});
      if(granted!=='granted')granted=await target.requestPermission({mode:'readwrite'});
      if(granted!=='granted')throw new Error('没有获得文件写入权限，PDF 尚未保存。');
    }
    updateDirtyLabel('正在保存 PDF…');saveCurrentPage();const revision=state.revision;
    const output=await exportBytes();
    if(saveGeneration!==documentGeneration)throw new Error('文档已切换，已取消本次保存。');
    if(target){
      for(const tab of documentTabs){const other=tab.id!==activeTabId?tab.model?.handle:null;if(other&&(target===other||(typeof target.isSameEntry==='function'&&await target.isSameEntry(other))))throw new Error('目标文件已在另一个标签中打开，请切换到该标签保存，或另存到其他文件。');}
      // Best-effort conflict detection immediately before writing. The browser API
      // cannot make this read/check/write atomic against another application.
      await assertTargetUnchanged(target);
      let writable;
      try{writable=await target.createWritable();await writable.write(output);await writable.close();}
      catch(error){if(writable)await writable.abort().catch(()=>{});throw error;}
      state.handle=target;state.filename=target.name;state.fileBaseline=output.slice();state.nativeFileToken=null;state.sourcePath=null;state.backupDocumentId=state.documentId;
      const tab=activeDocumentTab(),fingerprint=await fingerprintBytes(output);
      if(tab){tab.nativeId=null;tab.browserId=null;tab.fingerprint=fingerprint;tab.sourcePath=null;tab.filename=state.filename;if(fingerprint){try{const registered=browserWorkspace.register({filename:state.filename,fingerprint});browserWorkspaceSnapshot=registered.workspace;tab.browserId=registered.entry.id;}catch(error){renderWorkspaceStatus(error.message);}}}
    }else{
      const blob=new Blob([output],{type:'application/pdf'}),url=URL.createObjectURL(blob),link=document.createElement('a');
      link.href=url;link.download=suggestedName();document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
    }
    if(target)state.dirty=state.revision!==revision;
    updateDirtyLabel(target?(state.dirty?'已保存，另有新更改未保存':'PDF 已保存'):'已发起下载；请确认文件，当前批注仍保留');updatePageLabels();
    toast(target?'PDF 已保存，重新打开可继续编辑笔迹':'已发起 PDF 下载；确认文件后可重新打开继续编辑');
  }catch(error){
    if(error?.name!=='AbortError'){showError(error);updateDirtyLabel(error?.resultUnconfirmed?'保存结果未确认，请核对目标文件；当前批注仍保留':'保存失败，批注仍保留在当前窗口');}
    else updateDirtyLabel();
  }finally{state.saving=false;captureActiveTab({finish:false});scheduleWorkspacePersist();updateButtons();}
}

function renderBackupSettings(){
  if(backupSettings&&!backupSettingsDirty){
    $('[data-backup-setting=interval]').value=backupSettings.intervalMinutes;
    $('[data-backup-setting=count]').value=backupSettings.maxPerDocument;
    $('[data-backup-setting=directory]').value=backupSettings.directory;
  }
  $('[data-action=backup-settings-save]').disabled=!backupSettings||backupSettingsSaving;
  $$('[data-backup-setting]').forEach(control=>control.disabled=backupSettingsSaving||!backupSettings||(control.dataset.backupSetting==='migrate'&&!backupSettings?.capabilities?.migrateExisting));
  ui('backup-status').textContent=(backupSettingsDirty?'备份设置的修改尚未保存。 ':'')+backupNotice;
}
function reportBackupStatus(status){
  backupNotice=(status.documentId&&status.documentId!==(state.backupDocumentId||state.documentId)?'先前文档：':'')+(status.message||'本地备份状态已更新。');
  if(status.settings)backupSettings=status.settings;
  renderBackupSettings();
}
async function makeBackupSnapshot({force=false}={}){
  if(!state.pdf||state.opening||state.rendering||state.saving||layoutBusy||documentView?.getRecords().some(record=>record.engine.hasActiveGesture())||engine?.hasActiveGesture()||(!force&&!state.revision))return null;
  const documentId=state.backupDocumentId||state.documentId,filename=state.filename,sourcePath=state.sourcePath,revision=state.sessionId+':'+state.revision;
  const output=await exportBytes();
  return{bytes:output,documentId,filename,sourcePath,revision};
}
async function saveBackupSettings(){
  if(backupSettingsSaving)return;
  const controls=$$('[data-backup-setting]').filter(control=>!control.disabled);
  if(controls.some(control=>!control.reportValidity()))return;
  const button=$('[data-action=backup-settings-save]');backupSettingsSaving=true;button.disabled=true;renderBackupSettings();
  try{
    const settings=await backupClient.updateSettings({intervalMinutes:Number($('[data-backup-setting=interval]').value),maxPerDocument:Number($('[data-backup-setting=count]').value),directory:$('[data-backup-setting=directory]').value,migrateExisting:$('[data-backup-setting=migrate]').checked});
    backupSettings=settings;backupSettingsDirty=false;$('[data-backup-setting=migrate]').checked=false;
    await Promise.all(documentTabs.map(tab=>tab.scheduler?.reconfigure(settings)).filter(Boolean));
    const warnings=Array.isArray(settings.warnings)?settings.warnings.map(warning=>typeof warning==='string'?warning:warning?.message||'部分备份操作尚未完成。'):[];
    backupNotice=(settings.migration?.status==='partial'?'目录已切换，部分旧备份仍待清理。 ':'')+(warnings.length?warnings.join(' '):'备份设置已保存。');
  }catch(error){backupNotice=error.message;}
  finally{backupSettingsSaving=false;renderBackupSettings();}
}
async function backupNow(){
  if(backupSettings?.maxPerDocument===0){backupNotice='保留数量为 0，未生成新备份。';renderBackupSettings();return;}
  const button=$('[data-action=backup-now]');button.disabled=true;
  try{
    const snapshot=await makeBackupSnapshot({force:true});
    if(!snapshot){backupNotice='请先打开 PDF，并等待当前书写或文件操作结束。';return;}
    const result=await backupClient.createSnapshot(snapshot);
    if(result?.documentId!==snapshot.documentId||!(result.created===true||(result.created===false&&['disabled','unchanged'].includes(result.reason))))throw new Error('服务未确认备份结果，请在管理器中核对，当前 PDF 未被覆盖。');
    backupNotice=result.created?'已创建“'+snapshot.filename+'”的独立备份。':result.reason==='disabled'?'保留数量为 0，未生成新备份。':'“'+snapshot.filename+'”内容未变化，已有备份保留。';
    if(ui('backup-dialog').open)await refreshBackupManager();
  }catch(error){backupNotice=error.message;}
  finally{button.disabled=false;renderBackupSettings();}
}
function readableBytes(value){return value>=1048576?(value/1048576).toFixed(1)+' MiB':Math.max(1,Math.ceil(value/1024))+' KiB';}
async function refreshBackupManager(){
  const generation=++backupListGeneration;
  ui('backup-manager-status').textContent='正在读取备份…';
  try{
    const listing=await backupClient.list();if(generation!==backupListGeneration)return;backupDocuments=listing.documents;
    const list=ui('backup-documents');list.replaceChildren();
    for(const entry of backupDocuments){
      const section=document.createElement('section');section.className='ip-backup-document';
      const label=document.createElement('label'),checkbox=document.createElement('input'),description=document.createElement('span');
      checkbox.type='checkbox';checkbox.dataset.backupDocument=entry.documentId;checkbox.dataset.backupRoot=entry.rootId;
      const name=document.createElement('strong'),origin=document.createElement('small'),summary=document.createElement('small');
      name.textContent=entry.filename;origin.textContent=entry.sourcePath||'浏览器未提供原 PDF 的完整路径';
      summary.textContent=entry.count+' 份 · '+readableBytes(entry.totalBytes)+' · '+(listing.roots.find(item=>item.rootId===entry.rootId)?.directory||entry.rootId);
      description.append(name,origin,summary);label.append(checkbox,description);section.append(label);
      for(const snapshot of entry.snapshots){
        const row=document.createElement('div');row.className='ip-backup-snapshot';const text=document.createElement('span'),button=document.createElement('button');
        text.textContent=new Date(snapshot.createdAt).toLocaleString()+' · '+readableBytes(snapshot.size)+(snapshot.filename&&snapshot.filename!==entry.filename?' · '+snapshot.filename:'')+(snapshot.pendingDelete?' · 待清理，打开前核对完整性':'');
        button.type='button';button.className='ip-button';button.dataset.backupRestore='';button.dataset.backupAvailable=String(snapshot.available!==false);button.disabled=button.dataset.backupAvailable!=='true'||backupRestoreBusy||nativeDialogBusy||state.opening||state.saving;button.textContent=button.dataset.backupAvailable!=='true'?'此备份不可用':'打开备份';
        button.addEventListener('click',()=>restoreBackup(entry,snapshot,button));row.append(text,button);section.append(row);
      }
      list.append(section);
    }
    ui('backup-manager-status').textContent=backupDocuments.length?'共 '+backupDocuments.length+' 个文档位置；选择后可批量清理。':'还没有本地备份。';
  }catch(error){if(generation!==backupListGeneration)return;backupDocuments=[];ui('backup-documents').replaceChildren();ui('backup-manager-status').textContent=error.message;}
}
async function openBackupManager(){
  engine?.finishGesture();
  if(ui('settings-dialog').open)ui('settings-dialog').close();
  const dialog=ui('backup-dialog');if(!dialog.open)dialog.showModal();
  await refreshBackupManager();
}
async function restoreBackup(entry,snapshot,button){
  if(backupRestoreBusy||nativeDialogBusy||state.opening||state.saving||navigationRestoring||annotationVisibilityBusy||layoutBusy)return;
  ++fileOpenSerial;
  const serial=++backupRestoreSerial,previousDocumentGeneration=documentGeneration;backupRestoreBusy=true;updateButtons();
  try{
    const input=await backupClient.readSnapshot({rootId:entry.rootId,documentId:entry.documentId,snapshotId:snapshot.id});
    if(serial!==backupRestoreSerial||!ui('backup-dialog').open)return;
    if(previousDocumentGeneration!==documentGeneration||nativeDialogBusy||state.opening||state.saving){ui('backup-manager-status').textContent='文档已切换或正在处理文件，请重新选择要打开的备份。';return;}
    backupRestoreOpening=true;updateBackupRestoreControls();
    const filename=snapshot.filename||entry.filename;
    const sourcePath=Object.prototype.hasOwnProperty.call(snapshot,'sourcePath')?snapshot.sourcePath:entry.sourcePath;
    const backupDocumentId=/^document-[A-Za-z0-9_-]{1,120}$/.test(entry.documentId)?entry.documentId:undefined;
    if(await openBytes(input,filename,{sourcePath,backupDocumentId})){ui('backup-dialog').close();updateDirtyLabel('已打开备份；保存时请选择目标文件');}
  }catch(error){if(serial===backupRestoreSerial)ui('backup-manager-status').textContent=error.message;}
  finally{if(serial===backupRestoreSerial){backupRestoreBusy=false;backupRestoreOpening=false;updateButtons();}}
}
function updateBackupRestoreControls(){
  $$('[data-backup-restore]').forEach(control=>control.disabled=backupRestoreBusy||nativeDialogBusy||state.opening||state.saving||control.dataset.backupAvailable!=='true');
  $$('[data-action=backup-close]').forEach(control=>control.disabled=backupRestoreOpening);
}
async function cleanupBackups(currentOnly=false){
  let entries;
  if(currentOnly){
    if(!state.documentId){ui('backup-manager-status').textContent='请先打开要清理备份的 PDF。';return;}
    entries=backupDocuments.filter(entry=>entry.documentId===(state.backupDocumentId||state.documentId));
  }else{
    const checked=$$('[data-backup-document]:checked');
    entries=backupDocuments.filter(entry=>checked.some(control=>control.dataset.backupRoot===entry.rootId&&control.dataset.backupDocument===entry.documentId));
  }
  if(!entries.length){ui('backup-manager-status').textContent='没有选中可清理的备份。';return;}
  const count=entries.reduce((sum,entry)=>sum+entry.count,0);
  if(!window.confirm('将删除所选文档的 '+count+' 份备份。原 PDF 不会删除。确定清理吗？'))return;
  const actions=$$('[data-action=backup-clean-current],[data-action=backup-clean-selected]');actions.forEach(button=>button.disabled=true);
  try{
    const roots=new Map();for(const entry of entries){if(!roots.has(entry.rootId))roots.set(entry.rootId,new Set());roots.get(entry.rootId).add(entry.documentId);}
    for(const [rootId,ids] of roots)await backupClient.cleanup({rootId,documentIds:[...ids]});
    await refreshBackupManager();
  }catch(error){await refreshBackupManager();ui('backup-manager-status').textContent='清理未全部完成：'+error.message;}
  finally{actions.forEach(button=>button.disabled=false);}
}

engine=window.createInkPDFEngine(ui('ink'),{
  onChange:info=>{if(!engine||!state.pdf||restoring)return;state.inkPages[state.pageIndex]=engine.getStrokes();ui('ink-count').textContent=info.count+' 条批注';markDirty();updateButtons();if(state.sidebar==='annotations')renderSidebar();},
  onSelection:info=>{state.selection=info.count;state.selectionLocked=info.lockedCount||0;state.selectionTools=info.tools||[];if(info.style&&info.count)Object.assign(state.styles.select,info.style);renderTool();updateAnnotationRows();},
  onActivity:message=>{if(message)ui('input-status').textContent=message;},
  onGestureEnd:()=>{
    if(state.sidebar==='annotations')renderSidebar();
    else{
      const label=ui('sidebar-content').querySelector('[data-page="'+state.pageIndex+'"] .ip-thumb-label');
      if(label){const has=state.inkPages[state.pageIndex]?.length>0;let dot=label.querySelector('.ip-thumb-dot');if(has&&!dot){dot=document.createElement('span');dot.className='ip-thumb-dot';label.append(dot);}if(!has&&dot)dot.remove();}
    }
  }
});
engine.setInputSettings(state.ink);
ui('page-frame').hidden=true;$('.ip-favourites').hidden=true;if(lastNarrow){state.showSidebar=false;state.showProperties=false;}engine.setTool(toolForEngine(state.tool));engine.setStyle({...currentStyle(),eraserMode:state.eraserMode});renderSidebar();renderTool();syncAnnotationVisibility();updateLayout();applyPanelWidths();updatePageLabels();
root.addEventListener('click',event=>{
  const button=event.target.closest('button');if(!button||button.disabled)return;
  if(button.dataset.shortcutAction){beginShortcutCapture(button.dataset.shortcutAction);return;}
  if(button.dataset.shortcutClear){applyShortcutBindings(button.dataset.shortcutClear,[]);return;}
  if(button.dataset.shortcutReset){applyShortcutBindings(button.dataset.shortcutReset,[...DEFAULT_SHORTCUTS[button.dataset.shortcutReset]]);return;}
  if(button.dataset.tabId){activateDocumentTab(button.dataset.tabId);return;}
  if(button.dataset.closeTabId){closeDocumentTab(button.dataset.closeTabId);return;}
  if(button.dataset.recentId){openRecentId(button.dataset.recentId);return;}
  if(button.dataset.removeRecentId){removeRecent(button.dataset.removeRecentId);return;}
  if(button.dataset.review){addTextReview(button.dataset.review);return;}
  if(!state.annotationsVisible&&(button.dataset.color||button.dataset.width||button.dataset.eraser||button.dataset.favouriteId||['undo','redo','delete','copy-selection','paste-annotations','lock-selection','unlock-selection'].includes(button.dataset.action)))return;
  if((state.opening||state.rendering)&&(button.dataset.tool||button.dataset.color||button.dataset.width||button.dataset.eraser||button.dataset.favouriteId||['undo','redo','delete','deselect'].includes(button.dataset.action)))return;
  if(button.dataset.tool){setTool(button.dataset.tool);return;}
  if(button.dataset.color){changeStyle({color:button.dataset.color});return;}
  if(button.dataset.width){changeStyle({width:Number(button.dataset.width)});return;}
  if(button.dataset.eraser){state.eraserMode=button.dataset.eraser;engine.setStyle({eraserMode:state.eraserMode});renderProperties();return;}
  if(button.dataset.favouriteId){applyFavouritePreset(button.dataset.favouriteId);return;}
  if(button.dataset.favouriteEdit){if(button.dataset.favouriteEdit===favouriteEditorId||!discardFavouriteDraft())return;favouriteEditorId=button.dataset.favouriteEdit;setFavouriteNotice('正在编辑收藏预设。');renderFavouriteManager();$$('[data-favourite-edit]').find(row=>row.dataset.favouriteEdit===favouriteEditorId)?.focus();return;}
  if(button.dataset.searchPage!==undefined){const index=Number(button.dataset.searchPage);ui('search-dialog').close();gotoPage(index);return;}
  if(button.dataset.page!==undefined){gotoPage(Number(button.dataset.page));if(lastNarrow){state.showSidebar=false;updateLayout();}return;}
  if(button.dataset.panel){activateSidebar(button.dataset.panel);return;}
  switch(button.dataset.action){
    case'shortcuts-reset':restoreAllShortcuts();break;
    case'shortcut-cancel-capture':cancelShortcutCapture();break;
    case'open':chooseFile();break;
    case'recent-files':openRecentFiles();break;
    case'recent-close':ui('recent-dialog').close();break;
    case'recent-clear':clearRecentFiles();break;
    case'session-retry':{const pending=documentTabs.find(tab=>!tab.model&&tab.nativeId)||documentTabs.find(tab=>!tab.model);if(pending){ui('recent-dialog').close();reopenWorkspaceTab(pending);}else toast('没有等待恢复的标签');break;}
    case'save':savePdf();break;
    case'save-as':savePdf(true);break;
    case'undo':engine.undo();break;
    case'redo':engine.redo();break;
    case'delete':engine.deleteSelection();break;
    case'deselect':engine.clearSelection();break;
    case'select-all':selectAllAnnotations();break;
    case'copy-selection':copySelectedAnnotations();break;
    case'paste-annotations':pasteAnnotations();break;
    case'lock-selection':lockSelectedAnnotations(true);break;
    case'unlock-selection':lockSelectedAnnotations(false);break;
    case'nav-back':navigateHistory('back');break;
    case'nav-forward':navigateHistory('forward');break;
    case'fullscreen':toggleFullscreen();break;
    case'transform-selection':applyTransformFields();break;
    case'rotate-left':transformSelected(1,-90);break;
    case'rotate-right':transformSelected(1,90);break;
    case'edit-text':{const id=engine.getSelection().ids[0];if(id)openTextEditor(documentView.getActiveRecord(),{id});break;}
    case'selection-edit-text':{const selection=engine.getSelection();if(selection.count===1&&textTools.includes(selection.tools[0]))openTextEditor(documentView.getActiveRecord(),{id:selection.ids[0]});break;}
    case'selection-favourite':favouriteSelectedStyle();break;
    case'selection-rotate-left':transformSelected(1,-90);break;
    case'selection-rotate-right':transformSelected(1,90);break;
    case'text-editor-close':closeTextEditor();break;
    case'properties':leaveFocus();setPanelVisibility('properties',!state.showProperties);break;
    case'selected-properties':setPanelVisibility('properties',true);break;
    case'sidebar-close':setPanelVisibility('sidebar',false);break;
    case'focus':toggleFocus();break;
    case'settings':openSettings();break;
    case'settings-close':ui('settings-dialog').close();break;
    case'backup-settings-save':saveBackupSettings();break;
    case'backup-now':backupNow();break;
    case'backup-manage':openBackupManager();break;
    case'backup-close':ui('backup-dialog').close();break;
    case'backup-refresh':refreshBackupManager();break;
    case'backup-select-all':$$('[data-backup-document]').forEach(control=>control.checked=true);break;
    case'backup-clean-current':cleanupBackups(true);break;
    case'backup-clean-selected':cleanupBackups(false);break;
    case'search':openTextSearch();break;
    case'search-close':ui('search-dialog').close();break;
    case'search-stop':stopTextSearch();ui('search-status').textContent='搜索已停止。';break;
    case'search-run':event.preventDefault();runTextSearch();break;
    case'favourites-manage':openFavouriteManager();break;
    case'favourites-close':if(discardFavouriteDraft())ui('favourites-dialog').close();break;
    case'favourites-collapse':state.favouritesCollapsed=!state.favouritesCollapsed;renderFavourites();schedulePersist();break;
    case'favourite-save':event.preventDefault();saveManagedFavourite();break;
    case'favourite-new':newManagedFavourite();break;
    case'favourite-copy':copyManagedFavourite();break;
    case'favourite-remove':removeManagedFavourite();break;
    case'favourite-up':moveManagedFavourite(-1);break;
    case'favourite-down':moveManagedFavourite(1);break;
    case'dock':state.dock=state.dock==='left'?'right':'left';renderFavourites();schedulePersist();break;
    case'favourite-add':addFavourite();break;
    case'previous':gotoPage(documentView?.getAdjacentIndex(-1)??state.targetIndex-1);break;
    case'next':gotoPage(documentView?.getAdjacentIndex(1)??state.targetIndex+1);break;
    case'zoom-in':setZoom(state.zoom+.1);break;
    case'zoom-out':setZoom(state.zoom-.1);break;
    case'error-close':hideError();break;
  }
});
function updateFavouriteEditorFields(enabled=!!selectedFavourite()){
  const tool=$('[data-favourite-field=tool]').value,fillable=['rectangle','ellipse',...textTools].includes(tool),text=textTools.includes(tool),width=tool!=='text-highlight';
  $('[data-favourite-field=pressure]').disabled=!enabled||tool!=='pen';
  ui('favourite-fill').hidden=!fillable;
  $('[data-favourite-field=fill-enabled]').disabled=!enabled||!fillable;
  $('[data-favourite-field=fill-color]').disabled=!enabled||!fillable||!$('[data-favourite-field=fill-enabled]').checked;
  ui('favourite-font').hidden=!text;$$('[data-ui=favourite-font] [data-favourite-field]').forEach(field=>field.disabled=!enabled||!text);
  $('[data-favourite-field=width]').disabled=!enabled||!width;$('[data-favourite-field=width]').closest('label').hidden=!width;
  ui('favourite-width-caption').textContent=text?'文字框边框（px）':'粗细（px）';
}
function markFavouriteDraft(){
  favouriteDraftDirty=true;
  updateFavouriteEditorFields();
  ui('favourite-opacity-value').textContent=Number(Number($('[data-favourite-field=opacity]').value).toFixed(2))+'%';
  setFavouriteNotice('编辑区的修改尚未保存；点击“保存预设”应用到收藏。');
}
root.addEventListener('input',event=>{
  if(event.target.dataset.selectionStyle||event.target.dataset.textStyle){noteAnnotationFieldInput(event.target);return;}
  if(event.target.dataset.inkSetting||event.target.dataset.navigationSetting){updateInputPreference(event.target);return;}
  if(event.target.dataset.backupSetting){backupSettingsDirty=true;ui('backup-status').textContent='备份设置的修改尚未保存。';return;}
  if(event.target.dataset.favouriteField){markFavouriteDraft();return;}
  if(state.opening||state.rendering)return;
  const type=event.target.dataset.input;
  if(type==='width')changeStyle({width:Number(event.target.value)});
  if(type==='opacity')changeStyle({opacity:Number(event.target.value)/100});
  if(type==='color')changeStyle({color:event.target.value});
  if(type==='pressure')changeStyle({pressure:event.target.checked});
  if(type==='shape-filled')changeStyle({fill:event.target.checked?$('[data-input=fill-color]').value:'none'});
  if(type==='fill-color')changeStyle({fill:event.target.value});
});
root.addEventListener('change',event=>{
  if(event.target.dataset.selectionStyle){applySelectionField(event.target);return;}
  if(event.target.dataset.textStyle){applyTextPropertyField(event.target);return;}
  if(event.target.dataset.favouriteField==='tool')seedFavouriteToolFields();
  if(event.target.dataset.inkSetting||event.target.dataset.navigationSetting){updateInputPreference(event.target);return;}
  if(event.target.dataset.favouriteField){markFavouriteDraft();return;}
  const setting=event.target.dataset.setting;
  if(setting){
    if(setting==='restore-session'){workspacePreferenceTouched=true;++workspaceInteraction;restoreSession=event.target.checked;try{browserWorkspaceSnapshot=browserWorkspace.update({restoreSession});}catch(error){renderWorkspaceStatus(error.message);}enqueueWorkspaceWrite({restoreSession}).catch(()=>{});scheduleWorkspacePersist();renderSettings();return;}
    if(setting==='annotations-visible'){setAnnotationsVisible(event.target.checked);return;}
    if(setting==='zoom-locked'){state.zoomLocked=event.target.checked;if(state.zoomLocked)state.zoomMode='custom';setZoom(state.zoom,state.zoomMode,{force:true});updateButtons();schedulePersist();}
    if(setting==='theme'&&['system','light','dark'].includes(event.target.value)){state.theme=event.target.value;applyTheme();schedulePersist();}
    if(setting==='show-sidebar')setPanelVisibility('sidebar',event.target.checked);
    if(setting==='show-properties')setPanelVisibility('properties',event.target.checked);
    if(setting==='sidebar'&&['pages','bookmarks','annotations'].includes(event.target.value)){state.sidebar=event.target.value;renderSidebar();schedulePersist();}
    if(setting==='sidebar-width'||setting==='properties-width'){
      if(!event.target.validity.valid||!event.target.value){event.target.reportValidity();return;}
      setPanelWidth(setting==='sidebar-width'?'sidebar':'properties',Number(event.target.value));
    }
    renderSettings();return;
  }
  if(event.target.dataset.input==='page'){const value=Number(event.target.value);if(Number.isFinite(value))gotoPage(Math.round(value)-1);}
  if(event.target.dataset.input==='layout'){setReadingLayout(event.target.value);return;}
  if(event.target.dataset.input==='zoom'){if(state.zoomLocked)return;const value=event.target.value;if(['fit','whole','height'].includes(value)){state.zoomMode=value;fitZoom();}else if(value!=='custom')setZoom(Number(value));}
  if(event.target.dataset.input==='pdf-file'){const file=event.target.files?.[0],options=pendingBrowserSelection||{};pendingBrowserSelection=null;event.target.value='';openBrowserFile(file,options);}
});
ui('settings-dialog').addEventListener('close',()=>{if(shortcutCapture)cancelShortcutCapture();flushPreferences();});
ui('settings-dialog').addEventListener('cancel',event=>{if(shortcutCapture){event.preventDefault();cancelShortcutCapture();}});
ui('favourites-dialog').addEventListener('close',()=>{favouriteDraftDirty=false;flushPreferences();});
ui('favourites-dialog').addEventListener('cancel',event=>{if(!discardFavouriteDraft())event.preventDefault();});
ui('favourite-editor').addEventListener('submit',event=>{event.preventDefault();saveManagedFavourite();});
ui('search-form').addEventListener('submit',event=>{event.preventDefault();runTextSearch();});
ui('text-editor-form').addEventListener('submit',event=>{event.preventDefault();applyTextEditor();});
ui('text-editor-form').addEventListener('input',()=>{textEditorDirty=true;ui('text-editor-status').textContent='文字编辑尚未应用。';});
ui('text-editor-dialog').addEventListener('cancel',event=>{if(!discardTextEditor())event.preventDefault();});
ui('text-editor-dialog').addEventListener('close',()=>{textEditorContext=null;textEditorDirty=false;updateButtons();});
root.addEventListener('pointerdown',event=>{const button=event.target.closest('button');if(button?.dataset.review||button?.dataset.favouriteId&&reviewTools.includes(state.favourites.find(favourite=>favourite.id===button.dataset.favouriteId)?.tool))event.preventDefault();});
ui('search-dialog').addEventListener('close',stopTextSearch);
ui('backup-dialog').addEventListener('cancel',event=>{if(backupRestoreOpening)event.preventDefault();});
ui('backup-dialog').addEventListener('close',()=>{++backupRestoreSerial;backupRestoreBusy=false;backupRestoreOpening=false;updateButtons();});
for(const dialog of modalDialogs())dialog.addEventListener('keydown',event=>{
  if(dialog===ui('settings-dialog')&&recordShortcut(event))return;
  // Keep PDF shortcuts out of the modal while leaving native controls and Esc usable.
  event.stopPropagation();
  preventModalBrowserShortcut(event);
});
ui('document-tabs').addEventListener('keydown',event=>{
  if(event.ctrlKey||event.altKey||event.metaKey)return;
  const buttons=[...ui('document-tabs').querySelectorAll('[data-tab-id]:not(:disabled)')],index=buttons.indexOf(event.target);if(index<0)return;
  let target;if(event.key==='ArrowLeft')target=buttons[(index+buttons.length-1)%buttons.length];else if(event.key==='ArrowRight')target=buttons[(index+1)%buttons.length];else if(event.key==='Home')target=buttons[0];else if(event.key==='End')target=buttons[buttons.length-1];else if(event.key==='Delete'){event.preventDefault();event.stopPropagation();closeDocumentTab(event.target.dataset.tabId);return;}else return;
  event.preventDefault();event.stopPropagation();target?.focus();
});
viewport.addEventListener('scroll',()=>{if(state.pdf&&!state.opening&&!state.rendering&&!tabBusy)scheduleWorkspacePersist();},{passive:true});
viewport.addEventListener('scroll',scheduleSelectionMenuPosition,{passive:true});
function endPanelResize({cancelled=false}={}){
  if(!panelDrag)return;
  const drag=panelDrag;panelDrag=null;root.classList.remove('is-resizing-panel');
  if(cancelled)setPanelWidth(drag.panel,drag.original,{persist:false});
  if(drag.changed)schedulePersist();
  if(drag.separator.hasPointerCapture(drag.id))drag.separator.releasePointerCapture(drag.id);
}
for(const separator of $$('[data-resize-panel]')){
  const panel=separator.dataset.resizePanel,limits=panelSizes[panel];
  separator.addEventListener('pointerdown',event=>{
    if(lastNarrow||event.button!==0||panelDrag)return;
    const width=separator.parentElement.getBoundingClientRect().width;
    panelDrag={panel,separator,id:event.pointerId,x:event.clientX,width,original:state[limits.field],changed:false};
    try{separator.setPointerCapture(event.pointerId);}catch{panelDrag=null;return;}
    root.classList.add('is-resizing-panel');event.preventDefault();event.stopPropagation();
  });
  separator.addEventListener('pointermove',event=>{
    if(!panelDrag||panelDrag.id!==event.pointerId||panelDrag.separator!==separator)return;
    const delta=event.clientX-panelDrag.x;if(!delta&&!panelDrag.changed)return;
    panelDrag.changed=true;
    setPanelWidth(panel,panelDrag.width+(panel==='sidebar'?delta:-delta),{persist:false});
    event.preventDefault();
  });
  separator.addEventListener('pointerup',event=>{if(panelDrag?.id===event.pointerId)endPanelResize();});
  separator.addEventListener('pointercancel',event=>{if(panelDrag?.id===event.pointerId)endPanelResize({cancelled:true});});
  separator.addEventListener('lostpointercapture',event=>{if(panelDrag?.id===event.pointerId)endPanelResize({cancelled:true});});
  separator.addEventListener('keydown',event=>{
    if(lastNarrow)return;
    if(event.ctrlKey||event.altKey||event.metaKey)return;
    if(event.key==='Escape'&&panelDrag){endPanelResize({cancelled:true});event.preventDefault();event.stopPropagation();return;}
    let width=separator.parentElement.getBoundingClientRect().width;const step=event.shiftKey?10:1;
    if(event.key==='Home')width=limits.min;
    else if(event.key==='End')width=limits.max;
    else if(event.key==='ArrowLeft')width+=(panel==='sidebar'?-step:step);
    else if(event.key==='ArrowRight')width+=(panel==='sidebar'?step:-step);
    else return;
    setPanelWidth(panel,width);event.preventDefault();event.stopPropagation();
  });
}
window.addEventListener('blur',()=>endPanelResize({cancelled:true}));
root.addEventListener('drop',event=>{if(nativeDialogBusy||modalDialogs().some(dialog=>dialog.open)){event.preventDefault();event.stopImmediatePropagation();}},true);
root.addEventListener('dragover',event=>{if(event.dataTransfer?.types.includes('Files')){event.preventDefault();event.dataTransfer.dropEffect='copy';root.classList.add('is-dragover');}});root.addEventListener('dragleave',event=>{if(!root.contains(event.relatedTarget))root.classList.remove('is-dragover');});root.addEventListener('drop',event=>{event.preventDefault();root.classList.remove('is-dragover');const file=[...event.dataTransfer.files].find(f=>/\.pdf$/i.test(f.name));if(file)openBrowserFile(file);else toast('请拖入 PDF 文件');});
document.addEventListener('fullscreenchange',()=>{const active=!!document.fullscreenElement;$$('[data-action=fullscreen]').forEach(button=>{button.setAttribute('aria-pressed',String(active));button.setAttribute('aria-label',active?'退出全屏':'全屏阅读');});fitIfNeeded();});


window.addEventListener('beforeunload',event=>{if(persistTimer)flushPreferences();flushWorkspace({keepalive:true});if(!nativeWindowCloseAllowed&&(state.dirty||state.saving||documentTabs.some(tab=>tabDirty(tab))||favouriteDraftDirty||backupSettingsDirty||bookmarkEditorDraft?.dirty||textEditorDirty||annotationPendingFields.size)){event.preventDefault();event.returnValue='';}});
window.addEventListener('pagehide',()=>{if(persistTimer)flushPreferences();flushWorkspace({keepalive:true});});
document.addEventListener('visibilitychange',()=>{if(document.hidden){if(persistTimer)flushPreferences();flushWorkspace();}});
new ResizeObserver(()=>requestAnimationFrame(fitIfNeeded)).observe(viewport);
new ResizeObserver(()=>{
  const narrow=root.clientWidth<=720;if(narrow===lastNarrow){applyPanelWidths();return;}
  endPanelResize({cancelled:true});
  // Responsive and focus layouts are temporary; neither replaces the saved panel choices.
  if(narrow){state.showSidebar=false;state.showProperties=false;}
  else if(!state.focus){state.showSidebar=widePanels.sidebar;state.showProperties=widePanels.properties;}
  focusPanels=null;lastNarrow=narrow;updateLayout();applyPanelWidths();
}).observe(root);
installInputControls();
installSelectionMenu();
function resetNativeWindowClose(){
  clearTimeout(nativeWindowCloseTimer);nativeWindowCloseTimer=null;nativeWindowCloseAllowed=false;nativeWindowCloseInProgress=false;
  for(const [element,inert]of nativeWindowInertChildren)element.inert=inert;nativeWindowInertChildren.clear();
  ui('window-close-overlay').hidden=true;
  if(nativeWindowBackupsPaused){nativeWindowBackupsPaused=false;for(const tab of documentTabs)tab.scheduler?.start().catch(error=>reportBackupStatus({state:'error',message:error.message,error}));}
}
function showNativeWindowClose(message){
  ui('window-close-status').textContent=message;
  if(nativeWindowCloseInProgress)return;
  nativeWindowCloseInProgress=true;
  for(const dialog of modalDialogs())if(dialog.open)dialog.close();
  for(const element of root.children){if(element===ui('window-close-overlay'))continue;nativeWindowInertChildren.set(element,element.inert);element.inert=true;}
  ui('window-close-overlay').hidden=false;
}
function nativeCloseBusy(){
  return state.opening||state.saving||state.rendering||tabBusy||nativeDialogBusy||backupRestoreBusy||backupRestoreOpening||backupSettingsSaving||navigationRestoring||annotationVisibilityBusy||layoutBusy
    ||workspaceRestoring||!workspaceReady||hasInkGesture()||panelDrag||middleLasso||panStart||touching.size||activePenPointers.size||spacePan||nativeWindowCompositionActive;
}
async function requestNativeWindowClose(){
  if(nativeWindowCloseAllowed)return{allow:false,message:'先前的关闭确认仍在处理，请等待窗口退出或恢复编辑后重试。'};
  if(nativeWindowCloseInProgress)return{allow:false,message:'正在处理先前的关闭请求，请等待。'};
  if(nativeCloseBusy())return{allow:false,message:'文件、阅读状态或输入操作尚未结束，请完成后再关闭窗口。'};
  if(!commitPendingAnnotationField())return{allow:false,message:'批注属性尚未应用，请核对输入后再关闭窗口。'};
  if(textEditorDirty||favouriteDraftDirty||bookmarkEditorDraft?.dirty||backupSettingsDirty||shortcutCapture)return{allow:false,message:'仍有尚未应用的文字、收藏、书签、备份设置或按键输入，请先应用或放弃输入。'};
  const originalId=activeTabId;let approved=false;
  try{
    showNativeWindowClose('正在核对打开的 PDF…');stopTextSearch();captureActiveTab();
    if(state.dirty&&!activeDocumentTab())throw new Error('当前文档尚未完成状态登记，已保留窗口，请稍后重试。');
    const dirtyTabs=documentTabs.filter(tab=>tabDirty(tab));
    if(dirtyTabs.length){
      const saveAll=window.confirm('有 '+dirtyTabs.length+' 个 PDF 存在未保存的更改。确定保存全部后关闭窗口吗？取消可选择放弃修改或继续编辑。');
      if(saveAll){
        for(let index=0;index<dirtyTabs.length;index++){
          const tab=dirtyTabs[index];ui('window-close-status').textContent='正在保存 '+(index+1)+' / '+dirtyTabs.length+'：'+tab.filename;
          if(!tab.model||(tab.id!==activeTabId&&!await activateDocumentTab(tab.id)))throw new Error('未能切换到待保存文档，窗口仍保留。');
          await savePdf();captureActiveTab();
          if(tabDirty(tab))throw new Error('“'+tab.filename+'”的保存尚未确认或已取消，窗口仍保留。');
        }
        if(documentTabs.some(tab=>tabDirty(tab))||state.dirty)throw new Error('仍有未保存的更改，窗口仍保留。');
      }else if(!window.confirm('放弃这 '+dirtyTabs.length+' 个 PDF 的未保存更改并关闭窗口？取消将继续编辑。原文件不会被自动覆盖。'))return{allow:false,message:'已取消关闭，文档仍保留。'};
    }
    ui('window-close-status').textContent='正在保存偏好和阅读状态…';flushPreferences();flushWorkspace();await workspaceTail;
    for(const tab of documentTabs)tab.scheduler?.stop();nativeWindowBackupsPaused=true;
    nativeWindowCloseAllowed=true;approved=true;
    const expiresAt=Date.now()+8000;
    ui('window-close-status').textContent='关闭检查已完成，正在等待窗口退出…';
    nativeWindowCloseTimer=setTimeout(()=>{resetNativeWindowClose();toast('窗口未关闭，已恢复编辑；如需关闭请重试。');},10000);
    return{allow:true,expiresAt};
  }catch(error){return{allow:false,message:error?.message||'关闭检查失败，窗口仍保留。'};}
  finally{
    if(!approved){
      if(originalId&&activeTabId!==originalId&&documentTabs.some(tab=>tab.id===originalId&&tab.model)){try{await activateDocumentTab(originalId);}catch{/* The saved models and current document remain retained. */}}
      resetNativeWindowClose();
    }
  }
}
for(const type of ['pointerdown','pointermove','pointerup','click','dblclick','contextmenu','wheel','keydown','keyup','beforeinput','input','change','submit','dragover','drop'])document.addEventListener(type,event=>{
  if(!nativeWindowCloseInProgress)return;
  // Prevent user edits while internal serial save/tab operations remain usable.
  if(type==='keydown'||type==='keyup'||event.target instanceof Node&&root.contains(event.target)){event.preventDefault();event.stopImmediatePropagation();}
},{capture:true,passive:false});
document.addEventListener('compositionstart',()=>{nativeWindowCompositionActive=true;},true);
document.addEventListener('compositionend',()=>{nativeWindowCompositionActive=false;},true);
nativeWindowBridge=connectNativeWindow({requestClose:requestNativeWindowClose,onError:error=>{resetNativeWindowClose();showError(error);}});
window.inkPDFApp={getState,openBytes,exportBytes,get engine(){return engine;},gotoPage,setTool,activateDocumentTab,closeDocumentTab,getInputPreferences:()=>clone({ink:state.ink,navigation:state.navigation,shortcuts:state.shortcuts}),getTabs:()=>documentTabs.map(tab=>({id:tab.id,filename:tab.filename,active:tab.id===activeTabId,dirty:tabDirty(tab),loaded:!!tab.model,error:tab.error||null}))};
backupClient.settings().then(settings=>{backupSettings=settings;backupNotice='本地备份设置已载入，打开 PDF 后按文档跟踪变化。';renderBackupSettings();}).catch(error=>reportBackupStatus({state:'error',message:error.message,error}));
initializeWorkspace().catch(error=>{workspaceReady=true;workspaceRestoring=false;renderWorkspaceStatus(error.message);});
window.addEventListener('pagehide',()=>{for(const tab of documentTabs)tab.scheduler?.stop();});
window.addEventListener('pageshow',event=>{if(event.persisted)for(const tab of documentTabs)tab.scheduler?.start().catch(error=>reportBackupStatus({state:'error',message:error.message,error}));});
iconRefresh();
