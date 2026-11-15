// Included by prepare.cjs. Explicit preset/default edits and selected-object
// edits stay separate; this is project behaviour, not a Drawboard R04 claim.
const annotationFieldContexts=new WeakMap();
const annotationPendingFields=new Set();
let annotationForcedFields=new Set();
let annotationBlockedPointerClick=false;
function refreshAnnotationProperties(field=null){
  const previous=annotationForcedFields;annotationForcedFields=new Set(field?[field]:[]);
  if(field){annotationPendingFields.delete(field);delete field.dataset.pendingStyle;}
  try{renderProperties();}finally{annotationForcedFields=previous;}
}
function noteAnnotationFieldInput(field){annotationPendingFields.add(field);field.dataset.pendingStyle='true';field.title='属性输入尚未应用；完成输入后应用，Esc 放弃本次输入。';}
function commitPendingAnnotationField(){
  for(const field of [...annotationPendingFields]){
    const applied=field.dataset.selectionStyle?applySelectionField(field):applyTextPropertyField(field);
    if(!applied){toast('属性输入尚未应用，已保留当前文档；请核对后重试');return false;}
  }return true;
}
function favouriteDescription(favourite){
  return names[favourite.tool]+' · '+(textTools.includes(favourite.tool)?favourite.fontSize+' px / '+({sans:'无衬线',serif:'衬线',mono:'等宽'}[favourite.fontFamily])+(favourite.bold?' / 粗体':'')+(favourite.italic?' / 斜体':''):favourite.tool==='text-highlight'?'正文选区标记':favourite.width+' px')+' · '+Math.round(favourite.opacity*100)+'%';
}
function applyFavouritePreset(id){
  const favourite=state.favourites.find(item=>item.id===id);if(!favourite)return;
  if(state.opening||state.rendering||tabBusy||hasInkGesture()){toast('请先结束当前文档或笔迹操作');return;}
  if(!commitPendingAnnotationField())return;
  try{
    state.styles[favourite.tool]=favouriteStyle(favourite.tool,favourite);
    if(reviewTools.includes(favourite.tool)){state.reviewTool=favourite.tool;setTool('text');toast('已准备“'+favourite.name+'”；选择正文后点击对应审阅按钮应用');}
    else setTool(favourite.tool);
    schedulePersist();
  }catch(error){showError(error);}
}
function seedFavouriteToolFields(){
  const next=$('[data-favourite-field=tool]').value,previous=favouriteEditorTool;
  if(!FAVOURITE_TOOLS.includes(next))return;
  const defaults=defaultFavouriteStyle(next),field=key=>$('[data-favourite-field='+key+']');
  if(textTools.includes(next)&&!textTools.includes(previous))for(const key of ['fontSize','fontFamily','bold','italic']){if(field(key).type==='checkbox')field(key).checked=defaults[key];else field(key).value=defaults[key];}
  const fillable=tool=>['rectangle','ellipse',...textTools].includes(tool);
  if(fillable(next)&&!fillable(previous)){field('fill-enabled').checked=defaults.fill!=='none';field('fill-color').value=colorInputValue(defaults.fill,'#fff2a8');}
  if(next==='pen'&&previous!=='pen')field('pressure').checked=defaults.pressure;
  favouriteEditorTool=next;updateFavouriteEditorFields();
}
function favouriteSelectedStyle(){
  if(!state.pdf||!state.annotationsVisible||state.opening||state.rendering)return;
  const info=engine.getSelection();if(info.count!==1){toast('请只选择一个对象，再收藏它的样式');return;}
  if(state.favourites.length>=MAX_FAVOURITES){toast('收藏已达到当前 '+MAX_FAVOURITES+' 项处理上限');return;}
  try{
    const annotation=engine.getAnnotation(info.ids[0]);if(!annotation||!FAVOURITE_TOOLS.includes(annotation.tool))return;
    const favourite=createFavourite(annotation.tool,annotation);state.favourites=ensureFavouriteIds([...state.favourites,favourite]);favouriteEditorId=favourite.id;
    renderFavourites();updateAnnotationActions();schedulePersist();toast('已收藏样式；不含文字正文、位置或缩放旋转');
  }catch(error){showError(error);}
}
function selectionCanChange(){return canEditAnnotations()&&!tabBusy&&!nativeDialogBusy&&!backupRestoreBusy&&!navigationRestoring&&!hasInkGesture()&&!modalDialogs().some(dialog=>dialog.open);}
function applySelectedStyle(patch,{field=null}={}){
  if(!selectionCanChange()||!state.selection){refreshAnnotationProperties(field);return false;}
  try{
    const prepared=engine.previewSelectionStyle(patch);if(!prepared.annotations.length)return true;
    if(['fontSize','fontFamily','bold','italic','width'].some(field=>patch[field]!==undefined)){
      const overflow=prepared.annotations.filter(annotation=>textTools.includes(annotation.tool)&&window.ProperNotesText.layout(annotation).overflow).length;
      if(overflow&&!window.confirm('修改后有 '+overflow+' 个文字框的文字超出显示范围。原文会保留，可扩大框或减小字号。仍然应用吗？'))return false;
    }
    assertAnnotationBudgets(prepared.annotations,new Set(prepared.replacedIds));
    if(!engine.updateSelection(patch))return true;
    saveCurrentPage();renderSidebar();updateButtons();return true;
  }catch(error){showError(error);return false;}
  finally{refreshAnnotationProperties(field);}
}
function fieldContextMatches(field){
  const previous=annotationFieldContexts.get(field);if(!previous)return true;
  const selection=engine?.getSelection(),ids=JSON.stringify(selection?.ids.slice().sort()||[]);
  const matches=previous.engine===engine&&previous.generation===documentGeneration&&previous.ids===ids&&(selection?.count>0||previous.tool===activePresetTool());
  if(!matches){toast('对象或文档已改变，未应用先前输入；请重新聚焦属性后修改');refreshAnnotationProperties(field);}return matches;
}
function styleFieldValue(field,key){
  if(field.type==='checkbox')return field.checked;
  if(['fontSize','width','opacity'].includes(key))return Number(field.value)/(key==='opacity'?100:1);
  return field.value;
}
function applySelectionField(field){
  if(!fieldContextMatches(field))return false;
  if(!field.reportValidity()||field.type==='number'&&!field.value)return false;
  const key=field.dataset.selectionStyle;
  const patch=key==='filled'?{fill:field.checked?$('[data-selection-style=fill]').value:'none'}:{[key]:styleFieldValue(field,key)};
  return applySelectedStyle(patch,{field});
}
function applyTextPropertyField(field){
  if(!fieldContextMatches(field)||!field.reportValidity()||field.type==='number'&&!field.value)return false;
  try{return changeStyle({[field.dataset.textStyle]:styleFieldValue(field,field.dataset.textStyle)},{field})===true;}finally{refreshAnnotationProperties(field);}
}
function setAnnotationStyleField(field,key,style,mixed,disabled){
  const value=style[key];field.disabled=disabled;field.toggleAttribute('data-mixed',mixed);field.title=mixed?'所选对象此属性不同；更改后统一应用于可编辑对象':'';
  if((field===document.activeElement||annotationPendingFields.has(field))&&!annotationForcedFields.has(field)){if(annotationPendingFields.has(field))field.title='属性输入尚未应用；完成输入后应用，Esc 放弃本次输入。';return;}
  if(field.type==='checkbox'){field.indeterminate=mixed;field.checked=key==='filled'?value!=='none':!!value;}
  else if(field.type==='color'){field.value=colorInputValue(value,key==='fill'?'#fff2a8':'#24262b');}
  else if(field.tagName==='SELECT'){
    let option=field.querySelector('[data-mixed-option]');if(mixed&&!option){option=document.createElement('option');option.value='';option.textContent='混合';option.disabled=true;option.dataset.mixedOption='';field.prepend(option);}
    if(!mixed)option?.remove();field.value=mixed?'':value??'sans';
  }else{field.placeholder=mixed?'混合':'';field.value=mixed?'':key==='opacity'?Math.round((value??1)*10000)/100:value??'';}
}
function renderTextProperties(info=engine?.getSelection()){
  const selected=!!info?.count,hasText=selected?info.capabilities?.textCount>0:textTools.includes(state.tool),group=ui('text-tool-properties');group.hidden=!hasText;
  const style=selected?(info.editableStyle||info.style||{}):currentStyle(),mixed=new Set(info?.editableStyle?info.editableMixedFields:info?.mixedFields);
  for(const field of group.querySelectorAll('[data-text-style]')){const key=field.dataset.textStyle;setAnnotationStyleField(field,key,style,selected&&mixed.has(key),selected?!selectionCanChange()||!info.editableFields?.includes(key):state.opening||state.rendering||!state.annotationsVisible);}
}
function renderSelectionMenu(){
  $$('[data-review]').forEach(button=>{const active=state.tool==='text'&&state.reviewTool===button.dataset.review;button.classList.toggle('is-active',active);button.setAttribute('aria-pressed',String(active));});
  const menu=ui('selection-menu'),info=engine?.getSelection();if(!info?.count){menu.hidden=true;return;}
  const style=info.editableStyle||info.style||{},mixed=new Set(info.editableStyle?info.editableMixedFields:info.mixedFields),fields=new Set(info.applicableFields),editableFields=new Set(info.editableFields),canChange=selectionCanChange();
  ui('selection-menu-title').textContent=info.tools.length===1?names[info.tools[0]]+'操作':'混合批注操作';
  ui('selection-description').textContent=(info.lockedCount?info.lockedCount+' 条已锁定。 ':'')+(mixed.size?'部分属性为混合值。 ':'')+'仅修改未锁定且支持该属性的对象；不改工具或收藏。';
  ui('selection-text-controls').hidden=!info.capabilities.textCount;ui('selection-fill-controls').hidden=!fields.has('fill');
  for(const field of menu.querySelectorAll('[data-selection-style]')){
    const key=field.dataset.selectionStyle,logical=key==='filled'?'fill':key;field.closest('label').hidden=!fields.has(logical);
    const view=key==='filled'?{...style,filled:style.fill}:style;
    setAnnotationStyleField(field,key,view,mixed.has(logical),!canChange||!editableFields.has(logical)||(key==='fill'&&style.fill==='none'&&!mixed.has('fill')));
  }
  const singleText=info.count===1&&textTools.includes(info.tools[0]);$('[data-action=selection-edit-text]').hidden=!singleText;$('[data-action=selection-edit-text]').disabled=!canChange||!info.unlockedCount;
  $('[data-action=selection-favourite]').disabled=!state.annotationsVisible||state.opening||state.rendering||info.count!==1||state.favourites.length>=MAX_FAVOURITES;
  for(const action of ['selection-rotate-left','selection-rotate-right'])$('[data-action='+action+']').disabled=!canChange||!info.unlockedCount;
  scheduleSelectionMenuPosition();
}
function scheduleSelectionMenuPosition(){if(!selectionMenuFrame)selectionMenuFrame=requestAnimationFrame(()=>{selectionMenuFrame=0;positionSelectionMenu();});}
function positionSelectionMenu(){
  const menu=ui('selection-menu'),info=engine?.getSelection(),stage=$('.ip-stage');
  if(!info?.bounds||!state.annotationsVisible||state.opening||state.rendering||tabBusy||layoutBusy||annotationVisibilityBusy||nativeDialogBusy||backupRestoreBusy||navigationRestoring||hasInkGesture()||panStart||middleLasso||touching.size||spacePan||modalDialogs().some(dialog=>dialog.open)){menu.hidden=true;return;}
  const area=stage.getBoundingClientRect(),paper=ui('paper').getBoundingClientRect(),bounds=info.bounds;
  const box={left:paper.left+bounds.x*state.zoom,top:paper.top+bounds.y*state.zoom,right:paper.left+(bounds.x+bounds.width)*state.zoom,bottom:paper.top+(bounds.y+bounds.height)*state.zoom};
  if(area.width<40||area.height<40||box.right<area.left||box.left>area.right||box.bottom<area.top||box.top>area.bottom){menu.hidden=true;return;}
  menu.hidden=false;menu.style.maxWidth=Math.max(24,area.width-16)+'px';menu.style.maxHeight=Math.max(24,area.height-16)+'px';
  const size=menu.getBoundingClientRect(),clamp=(value,min,max)=>Math.max(min,Math.min(Math.max(min,max),value));
  const left=clamp((box.left+box.right-size.width)/2,area.left+8,area.right-size.width-8);
  const above=box.top-size.height-10,below=box.bottom+10,top=above>=area.top+8?above:below+size.height<=area.bottom-8?below:clamp(box.top,area.top+8,area.bottom-size.height-8);
  menu.style.left=(left-area.left)+'px';menu.style.top=(top-area.top)+'px';
}
function installSelectionMenu(){
  root.addEventListener('pointerdown',event=>{annotationBlockedPointerClick=false;const field=event.target.closest?.('[data-selection-style],[data-text-style]');if(annotationPendingFields.size&&(!field||!annotationPendingFields.has(field))&&!commitPendingAnnotationField()){annotationBlockedPointerClick=true;event.preventDefault();event.stopImmediatePropagation();}},true);
  root.addEventListener('click',event=>{if(annotationBlockedPointerClick&&event.detail>0){annotationBlockedPointerClick=false;event.preventDefault();event.stopImmediatePropagation();return;}const field=event.target.closest?.('[data-selection-style],[data-text-style]');if(annotationPendingFields.size&&(!field||!annotationPendingFields.has(field))&&!commitPendingAnnotationField()){event.preventDefault();event.stopImmediatePropagation();}},true);
  root.addEventListener('keydown',event=>{const field=event.target.closest?.('[data-selection-style],[data-text-style]');if(!field)return;if(event.key==='Escape'&&!event.ctrlKey&&!event.altKey){event.preventDefault();event.stopPropagation();refreshAnnotationProperties(field);}else if(event.key==='Tab'&&annotationPendingFields.size&&!commitPendingAnnotationField()){event.preventDefault();event.stopPropagation();}});
  root.addEventListener('focusin',event=>{const field=event.target.closest?.('[data-selection-style],[data-text-style]');if(field){if(annotationPendingFields.size&&!annotationPendingFields.has(field)&&!commitPendingAnnotationField())return;annotationFieldContexts.set(field,{engine,generation:documentGeneration,ids:JSON.stringify(engine?.getSelection().ids.slice().sort()||[]),tool:activePresetTool()});}});
  root.addEventListener('focusout',event=>{if(event.target.matches?.('[data-selection-style],[data-text-style]'))requestAnimationFrame(()=>{renderProperties();scheduleSelectionMenuPosition();});});
  for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture'])viewport.addEventListener(type,scheduleSelectionMenuPosition,true);
  new ResizeObserver(scheduleSelectionMenuPosition).observe($('.ip-stage'));new ResizeObserver(scheduleSelectionMenuPosition).observe(ui('selection-menu'));
}
