// Included by prepare.cjs. Project input behaviour; no unverified pen button,
// Drawboard tool-return or hardware-specific mapping is inferred here.
function renderInputSettings(){
  for(const field of $$('[data-ink-setting]')){const key=field.dataset.inkSetting;if(key==='pressure')field.checked=state.styles.pen.pressure;else field.value=state.ink[key];}
  ui('ink-sensitivity-value').textContent=String(state.ink.pressureSensitivity);ui('ink-smoothing-value').textContent=String(state.ink.smoothing);
  for(const field of $$('[data-navigation-setting]')){const value=state.navigation[field.dataset.navigationSetting];if(field.type==='checkbox')field.checked=value;else field.value=value;}
  renderShortcutManager();
}
function syncInputSettings(){
  const records=documentView?.getRecords()||[];
  for(const record of records)record.engine.setInputSettings(state.ink);
  if(!records.length)engine?.setInputSettings(state.ink);
  if(state.tool==='pen')engine?.setStyle({...currentStyle(),eraserMode:state.eraserMode});
  documentView?.syncTools();
}
function updateInputPreference(field){
  if(field.dataset.inkSetting){
    const key=field.dataset.inkSetting;
    if(key==='pressure')state.styles.pen.pressure=field.checked;
    else state.ink=normalizeInkPreferences({...state.ink,[key]:key==='response'?field.value:Number(field.value)});
    syncInputSettings();renderInputSettings();schedulePersist();return true;
  }
  if(field.dataset.navigationSetting){
    clearCanvasNavigation();state.navigation=normalizeNavigationPreferences({...state.navigation,[field.dataset.navigationSetting]:field.type==='checkbox'?field.checked:field.value});renderInputSettings();schedulePersist();return true;
  }
  return false;
}
function renderShortcutManager(){
  refreshShortcutHints();
  const list=ui('shortcut-list');if(!list)return;
  const focused=document.activeElement?.dataset.shortcutAction;list.replaceChildren();
  for(const command of SHORTCUT_COMMANDS){
    const row=document.createElement('div');row.className='ip-shortcut-row'+(shortcutCapture===command.id?' is-recording':'');
    const label=document.createElement('strong');label.className='ip-shortcut-label';label.textContent=command.label;
    const capture=document.createElement('button');capture.type='button';capture.className='ip-button';capture.dataset.shortcutAction=command.id;capture.setAttribute('aria-pressed',String(shortcutCapture===command.id));capture.setAttribute('aria-label','修改 '+command.label+' 的快捷键');capture.textContent=shortcutCapture===command.id?'请按组合键…':state.shortcuts[command.id].map(formatBinding).join(' / ')||'未分配';
    const clear=document.createElement('button');clear.type='button';clear.className='ip-button';clear.dataset.shortcutClear=command.id;clear.textContent='清空';clear.disabled=!state.shortcuts[command.id].length;
    const reset=document.createElement('button');reset.type='button';reset.className='ip-button';reset.dataset.shortcutReset=command.id;reset.textContent='恢复';row.append(label,capture,clear,reset);list.append(row);
  }
  ui('shortcut-status').textContent=shortcutNotice||'点击绑定录入新组合；新绑定替换该项的全部组合，Esc 取消录入。空格保留为按住临时平移。';
  $('[data-action=shortcut-cancel-capture]').hidden=!shortcutCapture;
  if(focused)list.querySelector('[data-shortcut-action="'+focused+'"]')?.focus({preventScroll:true});
}
function refreshShortcutHints(){
  const actions={open:'open',save:'save','save-as':'saveAs',search:'search',undo:'undo',redo:'redo','select-all':'selectAll','copy-selection':'copyAnnotations','paste-annotations':'pasteAnnotations',delete:'deleteSelection',deselect:'deselect','nav-back':'navBack','nav-forward':'navForward',previous:'previousPage',next:'nextPage','zoom-in':'zoomIn','zoom-out':'zoomOut',fullscreen:'fullscreen',focus:'focus'};
  const tools={pen:'toolPen',highlighter:'toolHighlighter',eraser:'toolEraser',select:'toolSelect',lasso:'toolLasso',pan:'toolPan',text:'toolText',line:'toolLine',arrow:'toolArrow',rectangle:'toolRectangle',ellipse:'toolEllipse',textbox:'toolTextbox',note:'toolNote'};
  for(const button of $$('button[data-action],button[data-tool]')){
    const id=tools[button.dataset.tool]||actions[button.dataset.action];if(!id)continue;
    if(!button.dataset.shortcutHintBase)button.dataset.shortcutHintBase=(button.dataset.tooltip||SHORTCUT_COMMANDS.find(command=>command.id===id).label).split(' · ')[0];
    const bindings=state.shortcuts[id],suffix=bindings.map(formatBinding).join(' / ');
    button.dataset.tooltip=[button.dataset.shortcutHintBase,suffix,button.dataset.tool==='pan'?'按住空格临时平移':''].filter(Boolean).join(' · ');
    const punctuation={Minus:'-',Equal:'=',BracketLeft:'[',BracketRight:']',Backslash:'\\',Semicolon:';',Quote:"'",Comma:',',Period:'.',Slash:'/',Backquote:'`'};
    const aria=bindings.filter(binding=>!binding.includes('Numpad')).map(binding=>binding.split('+').map(part=>part==='Ctrl'?'Control':punctuation[part]||part.replace(/^Key([A-Z])$/,'$1').replace(/^Digit(\d)$/,'$1')).join('+')).join(' ');
    if(aria)button.setAttribute('aria-keyshortcuts',aria);else button.removeAttribute('aria-keyshortcuts');
  }
}
function cancelShortcutCapture(message='已取消按键录入，原绑定保留。'){
  shortcutCapture=null;shortcutNotice=message;renderShortcutManager();
}
function beginShortcutCapture(id){
  const command=SHORTCUT_COMMANDS.find(item=>item.id===id);if(!command)return;
  shortcutCapture=id;shortcutNotice='正在设置“'+command.label+'”。请按组合键；新绑定替换该项的所有组合，Esc 取消。';renderShortcutManager();$('[data-shortcut-action="'+id+'"]').focus({preventScroll:true});
}
function applyShortcutBindings(id,bindings){
  const draft={...state.shortcuts,[id]:bindings},checked=validateShortcuts(draft);
  if(!checked.valid){
    shortcutNotice=checked.errors.map(error=>{const other=SHORTCUT_COMMANDS.find(command=>command.id===error.conflictWith);return(formatBinding(error.binding)||'该组合')+'：'+error.message+(other?'（'+other.label+'）':'');}).join(' ');renderShortcutManager();return false;
  }
  state.shortcuts=normalizeShortcuts(draft);shortcutCapture=null;shortcutNotice='快捷键已应用，正在保存本机偏好。';renderShortcutManager();schedulePersist();return true;
}
function recordShortcut(event){
  if(!shortcutCapture)return false;
  event.stopPropagation();
  if(event.isComposing||event.keyCode===229||['Dead','Process','Unidentified'].includes(event.key))return true;
  if(event.key==='Escape'){event.preventDefault();cancelShortcutCapture();return true;}
  if(event.repeat){event.preventDefault();return true;}
  const binding=bindingFromEvent(event);if(!binding)return true;
  event.preventDefault();applyShortcutBindings(shortcutCapture,[binding]);return true;
}
function restoreAllShortcuts(){
  if(!window.confirm('恢复全部快捷键的项目默认设置？'))return;
  state.shortcuts=clone(DEFAULT_SHORTCUTS);shortcutCapture=null;shortcutNotice='全部快捷键已恢复项目默认设置。';renderShortcutManager();schedulePersist();
}
function canvasNavigationReady(){return !!state.pdf&&!state.opening&&!state.rendering&&!tabBusy&&!nativeDialogBusy&&!backupRestoreBusy&&!navigationRestoring&&!annotationVisibilityBusy&&!layoutBusy&&!modalDialogs().some(dialog=>dialog.open);}
function shortcutOperationBusy(){return state.opening||state.rendering||tabBusy||nativeDialogBusy||backupRestoreBusy||layoutBusy||annotationVisibilityBusy||navigationRestoring;}
function nativeEditingKey(event){return(event.ctrlKey||event.metaKey)&&!event.altKey&&['KeyA','KeyC','KeyV','KeyX','KeyZ','KeyY'].includes(event.code);}
function preventModalBrowserShortcut(event){
  // Stopping propagation blocks our dispatch. Native editing and plain letters
  // must still reach inputs; only browser-level app chords need cancellation.
  if(nativeEditingKey(event))return;
  const action=commandForEvent(event,state.shortcuts),command=SHORTCUT_COMMANDS.find(command=>command.id===action);
  if((command?.context==='global'&&(event.ctrlKey||event.altKey))||((event.ctrlKey||event.metaKey)&&['KeyS','KeyO','KeyF'].includes(event.code)))event.preventDefault();
}
function hasInkGesture(){return documentView?documentView.getRecords().some(record=>record.engine.hasActiveGesture()):!!engine?.hasActiveGesture();}
function captureZoomPoint(point){
  if(!Number.isFinite(point.clientX)||!Number.isFinite(point.clientY))return null;
  const target=point.target instanceof Element?point.target:null;
  const slot=target?.closest('[data-document-page]');
  const element=slot&&documentView?.getContainer().contains(slot)?slot:ui('page-frame');
  if(!element?.isConnected)return null;
  const box=element.getBoundingClientRect();return{element,x:(point.clientX-box.left)/state.zoom,y:(point.clientY-box.top)/state.zoom,clientX:point.clientX,clientY:point.clientY};
}
function releaseViewportPointer(id){try{if(viewport.hasPointerCapture(id))viewport.releasePointerCapture(id);}catch{/* The pointer can already have ended. */}}
function clearCanvasNavigation(){
  const lasso=middleLasso;middleLasso=null;if(lasso)lasso.engine.cancelGesture();
  const pointers=new Set([...touching.keys(),...(panStart?[panStart.id]:[])]);touching.clear();panStart=null;pinchStart=null;activePenPointers.clear();
  for(const id of pointers)releaseViewportPointer(id);
  if(spacePan){spacePan=false;engine?.setTool(toolForEngine(state.tool));documentView?.syncTools();}
  root.classList.remove('is-panning');
}
function startCanvasPointer(event){
  if(!canvasNavigationReady())return;
  if(event.pointerType==='pen'){activePenPointers.add(event.pointerId);for(const id of touching.keys())releaseViewportPointer(id);touching.clear();pinchStart=null;}
  if(event.pointerType==='touch'){
    event.preventDefault();event.stopPropagation();
    if(activePenPointers.size||hasInkGesture()||middleLasso||panStart)return;
    if(touching.size>=2)return;
    touching.set(event.pointerId,{x:event.clientX,y:event.clientY});
    try{viewport.setPointerCapture(event.pointerId);}catch{touching.delete(event.pointerId);return;}
    if(touching.size===2){const points=[...touching.values()];pinchStart={distance:Math.hypot(points[0].x-points[1].x,points[0].y-points[1].y),zoom:state.zoom};}return;
  }
  const middle=event.pointerType==='mouse'&&event.button===1;
  if(middle){
    event.preventDefault();event.stopPropagation();
    if(hasInkGesture()||panStart||touching.size)return;
    if(state.navigation.middleAction==='lasso'){
      if(!state.annotationsVisible){toast('请先显示批注，再使用中键套索');return;}
      const slot=event.target.closest?.('[data-document-page]'),index=slot?Number(slot.dataset.documentPage):-1;
      const record=Number.isInteger(index)?documentView?.activateReadyPage(index):null;
      if(!record){toast('请在已经显示的 PDF 页面上按住中键套索');return;}
      if(record.engine.beginLasso(event)){middleLasso={engine:record.engine,pointerId:event.pointerId};window.getSelection()?.removeAllRanges();}
      return;
    }
  }
  const pan=middle||(event.button===0&&(state.tool==='pan'||spacePan||(!state.annotationsVisible&&state.tool!=='text')));
  if(!pan||hasInkGesture())return;
  try{viewport.setPointerCapture(event.pointerId);}catch{return;}
  panStart={x:event.clientX,y:event.clientY,left:viewport.scrollLeft,top:viewport.scrollTop,id:event.pointerId};event.preventDefault();event.stopPropagation();root.classList.add('is-panning');
}
function moveCanvasPointer(event){
  if(middleLasso?.pointerId===event.pointerId){event.preventDefault();event.stopPropagation();middleLasso.engine.moveLasso(event);return;}
  if(touching.has(event.pointerId)){
    const previous=touching.get(event.pointerId);touching.set(event.pointerId,{x:event.clientX,y:event.clientY});
    if(touching.size===2&&pinchStart){const points=[...touching.values()];setZoom(pinchStart.zoom*Math.hypot(points[0].x-points[1].x,points[0].y-points[1].y)/Math.max(1,pinchStart.distance),'custom',{anchor:{clientX:(points[0].x+points[1].x)/2,clientY:(points[0].y+points[1].y)/2,target:document.elementFromPoint((points[0].x+points[1].x)/2,(points[0].y+points[1].y)/2)}});}
    else if(touching.size===1){viewport.scrollLeft-=event.clientX-previous.x;viewport.scrollTop-=event.clientY-previous.y;}
    event.preventDefault();event.stopPropagation();return;
  }
  if(panStart?.id===event.pointerId){viewport.scrollLeft=panStart.left-(event.clientX-panStart.x);viewport.scrollTop=panStart.top-(event.clientY-panStart.y);event.preventDefault();event.stopPropagation();}
}
function endCanvasPointer(event){
  if(middleLasso?.pointerId===event.pointerId){
    const lasso=middleLasso;middleLasso=null;const cancelled=event.type!=='pointerup';event.preventDefault();event.stopPropagation();lasso.engine.endLasso(event,{cancelled});
    if(!cancelled&&lasso.engine===engine)setTool('select');
  }
  activePenPointers.delete(event.pointerId);touching.delete(event.pointerId);if(touching.size<2)pinchStart=null;
  if(panStart?.id===event.pointerId){panStart=null;root.classList.remove('is-panning');}
}
function wheelCanvas(event){
  if(event.defaultPrevented)return;
  if(!canvasNavigationReady()){if(event.ctrlKey)event.preventDefault();return;}
  if(hasInkGesture()||panStart||middleLasso||touching.size){event.preventDefault();return;}
  let action=state.navigation.wheelAction;if(event.ctrlKey&&state.navigation.ctrlInverts)action=action==='scroll'?'zoom':'scroll';
  const factor=event.deltaMode===1?16:event.deltaMode===2?Math.max(1,viewport.clientHeight):1;
  const dx=event.deltaX*factor,dy=event.deltaY*factor;if(!Number.isFinite(dx)||!Number.isFinite(dy))return;
  event.preventDefault();
  if(action==='scroll'||!dy){viewport.scrollLeft+=dx;viewport.scrollTop+=dy;return;}
  if(state.zoomLocked)return;
  setZoom(state.zoom*Math.exp(-Math.max(-400,Math.min(400,dy))*.002),'custom',{anchor:state.navigation.zoomAtPointer?event:null});
}
function performShortcut(action){
  const toolActions={toolPen:'pen',toolHighlighter:'highlighter',toolEraser:'eraser',toolSelect:'select',toolLasso:'lasso',toolPan:'pan',toolText:'text',toolLine:'line',toolArrow:'arrow',toolRectangle:'rectangle',toolEllipse:'ellipse',toolTextbox:'textbox',toolNote:'note'};
  if(toolActions[action]){if(canvasNavigationReady()&&!hasInkGesture())setTool(toolActions[action]);return;}
  switch(action){
    case'open':chooseFile();break;case'save':savePdf();break;case'saveAs':savePdf(true);break;case'search':openTextSearch();break;
    case'closeTab':if(activeTabId)closeDocumentTab(activeTabId);break;
    case'nextTab':case'previousTab':{const index=documentTabs.findIndex(tab=>tab.id===activeTabId),next=documentTabs[(index+(action==='nextTab'?1:-1)+documentTabs.length)%documentTabs.length];if(next)activateDocumentTab(next.id);break;}
    case'undo':if(canEditAnnotations())engine.undo();break;case'redo':if(canEditAnnotations())engine.redo();break;
    case'selectAll':selectAllAnnotations();break;
    case'copyAnnotations':copySelectedAnnotations();break;case'pasteAnnotations':pasteAnnotations();break;
    case'deleteSelection':if(canEditAnnotations())engine.deleteSelection();break;
    case'deselect':if(!engine?.cancelGesture())engine?.clearSelection();break;
    case'navBack':navigateHistory('back');break;case'navForward':navigateHistory('forward');break;
    case'previousPage':case'nextPage':if(canvasNavigationReady()&&!hasInkGesture())gotoPage(documentView?.getAdjacentIndex(action==='previousPage'?-1:1)??state.pageIndex+(action==='previousPage'?-1:1));break;
    case'zoomIn':if(canvasNavigationReady()&&!hasInkGesture())setZoom(state.zoom+.1);break;
    case'zoomOut':if(canvasNavigationReady()&&!hasInkGesture())setZoom(state.zoom-.1);break;
    case'fitWidth':case'fitPage':case'fitHeight':if(canvasNavigationReady()&&!hasInkGesture()&&!state.zoomLocked){state.zoomMode=action==='fitWidth'?'fit':action==='fitPage'?'whole':'height';fitZoom();}break;
    case'fullscreen':toggleFullscreen();break;case'focus':toggleFocus();break;
  }
}
function keyCanvas(event){
  if(event.defaultPrevented||event.isComposing||event.keyCode===229)return;
  const action=commandForEvent(event,state.shortcuts);
  if(modalDialogs().some(dialog=>dialog.open)){preventModalBrowserShortcut(event);return;}
  const target=event.target instanceof Element?event.target:null,editing=target?.closest('input,select,textarea,[contenteditable]:not([contenteditable=false]),[role=textbox]');
  const control=target?.closest('button,[role=tablist],[data-resize-panel]');
  if(editing){if(nativeEditingKey(event))return;const command=SHORTCUT_COMMANDS.find(command=>command.id===action);if(command?.context==='global'&&(event.ctrlKey||event.altKey)){event.preventDefault();if(!event.repeat&&!shortcutOperationBusy()&&!hasInkGesture())performShortcut(action);}return;}
  if(control&&!event.ctrlKey&&!event.altKey&&['Space','Enter','NumpadEnter','Tab'].includes(event.code))return;
  const selection=window.getSelection(),nativeText=selection&&!selection.isCollapsed;
  if((event.ctrlKey||event.metaKey)&&['KeyC','KeyA','KeyV'].includes(event.code)&&(state.tool==='text'||nativeText))return;
  if(event.code==='Space'&&!event.ctrlKey&&!event.altKey&&!event.metaKey&&!event.shiftKey){
    if(canvasNavigationReady()&&!hasInkGesture()&&!spacePan&&!event.repeat){spacePan=true;documentView?.syncTools();engine?.setTool('pan');root.classList.add('is-panning');ui('mode-status').textContent='临时平移';}event.preventDefault();return;
  }
  if(event.key==='Escape'&&(middleLasso||panStart||touching.size||spacePan)){event.preventDefault();clearCanvasNavigation();return;}
  if(!action)return;
  if(['copyAnnotations','selectAll'].includes(action)&&(!isAnnotationSelectionTool()||!canEditAnnotations()||(action==='copyAnnotations'&&!state.selection)))return;
  if(action==='pasteAnnotations'&&(state.tool==='text'||!annotationClipboard.length||!canEditAnnotations()))return;
  if(shortcutOperationBusy()){event.preventDefault();return;}
  if(hasInkGesture()&&action!=='deselect'){event.preventDefault();return;}
  event.preventDefault();if(event.repeat)return;performShortcut(action);
}
function installInputControls(){
  refreshShortcutHints();
  viewport.addEventListener('pointerdown',startCanvasPointer,true);viewport.addEventListener('pointermove',moveCanvasPointer,true);
  for(const type of ['pointerup','pointercancel','lostpointercapture'])viewport.addEventListener(type,endCanvasPointer,true);
  viewport.addEventListener('auxclick',event=>{if(event.button===1)event.preventDefault();});
  viewport.addEventListener('wheel',wheelCanvas,{passive:false});document.addEventListener('keydown',keyCanvas);
  document.addEventListener('keyup',event=>{if(event.code==='Space'&&spacePan){spacePan=false;engine?.setTool(toolForEngine(state.tool));documentView?.syncTools();if(!panStart)root.classList.remove('is-panning');ui('mode-status').textContent=state.annotationsVisible?names[state.tool]:'批注已隐藏 · 浏览';}});
  for(const type of ['pointerup','pointercancel'])window.addEventListener(type,event=>activePenPointers.delete(event.pointerId));
  window.addEventListener('blur',()=>{if(documentView){for(const record of documentView.getRecords())record.engine.cancelGesture();}else engine?.cancelGesture();clearCanvasNavigation();if(shortcutCapture)cancelShortcutCapture('窗口失去焦点，已取消按键录入。');});
  document.addEventListener('visibilitychange',()=>{if(document.hidden){if(documentView){for(const record of documentView.getRecords())record.engine.cancelGesture();}else engine?.cancelGesture();clearCanvasNavigation();}});
}
