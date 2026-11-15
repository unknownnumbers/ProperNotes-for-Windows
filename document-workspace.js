// Included in app-main.js by prepare.cjs. Metadata persistence contains no PDF,
// annotation, clipboard, file capability token or undo payload.
const documentModelFields=['pdf','sourceBytes','pages','inkPages','filename','handle','fileBaseline','nativeFileToken','documentId','backupDocumentId','sessionId','sourcePath','pageIndex','targetIndex','histories','outline','bookmarks','bookmarksEditable','bookmarksMessage','bookmarkEdits','revision','dirty','zoom','zoomMode','layout'];
function activeDocumentTab(){return documentTabs.find(tab=>tab.id===activeTabId)||null;}
async function activateExistingDocument(tab,placeholder){
  const success=await activateDocumentTab(tab.id,{fromOpen:true});
  if(success&&placeholder&&placeholder!==tab&&!placeholder.model){const index=documentTabs.indexOf(placeholder);if(index>=0)documentTabs.splice(index,1);if(placeholder.dirtyOnRestore)toast('已切换到当前已打开的文档；上次未保存内容请核对本地备份');renderDocumentTabs();scheduleWorkspacePersist();}
  return success;
}
function documentOperationBusy({fromOpen=false}={}){return tabBusy||state.opening||state.saving||state.rendering||(!fromOpen&&nativeDialogBusy)||backupRestoreBusy||navigationRestoring||annotationVisibilityBusy||layoutBusy;}
function captureDocumentModel(){
  const model=Object.fromEntries(documentModelFields.map(key=>[key,state[key]]));
  model.navigation={past:navigationPast,future:navigationFuture,selectedBookmark:bookmarkSelectedId,readonlyExpansion:readonlyBookmarkExpansion,listOffset:annotationListOffset,filter:{...annotationFilter},selectionIds:engine?.getSelection().ids||[],pageTextNotice};
  return model;
}
function applyDocumentModel(model){
  for(const key of documentModelFields)state[key]=model[key];
  const nav=model.navigation||{};navigationPast=nav.past||[];navigationFuture=nav.future||[];bookmarkSelectedId=nav.selectedBookmark||null;readonlyBookmarkExpansion=nav.readonlyExpansion||new Map();annotationListOffset=nav.listOffset||0;annotationFilter={tool:'all',lock:'all',currentPage:false,...nav.filter};pageTextNotice=nav.pageTextNotice||'';
  state.selection=0;state.selectionLocked=0;state.selectionTools=[];
}
function captureActiveTab({finish=true}={}){
  const tab=activeDocumentTab();if(!tab||!state.pdf||tab.model?.pdf!==state.pdf)return tab;
  if(finish)saveCurrentPage();tab.model=captureDocumentModel();tab.reading=captureNavigation();tab.filename=state.filename;tab.sourcePath=state.sourcePath;
  return tab;
}
function resetDocumentInteraction(){
  clearCanvasNavigation();
  ++fileOpenSerial;++navigationSerial;stopTextSearch();ui('search-results').replaceChildren();ui('search-status').textContent='输入文字搜索 PDF 已有文本层。';
  clearTimeout(resolutionTimer);renderTask?.cancel();resolutionTask?.cancel();pageTextTask?.cancel();
  touching.clear();activePenPointers.clear();panStart=null;pinchStart=null;spacePan=false;root.classList.remove('is-panning');window.getSelection()?.removeAllRanges();
}
function tabDirty(tab){return tab.id===activeTabId&&state.pdf===tab.model?.pdf?state.dirty:tab.model?.dirty===true;}
function renderDocumentTabs(){
  const list=ui('document-tabs');if(!list)return;
  const focusId=list.contains(document.activeElement)?document.activeElement?.dataset.tabId:null;
  list.replaceChildren();
  for(const tab of documentTabs){
    const row=document.createElement('div');row.className='ip-document-tab'+(tab.id===activeTabId?' is-active':'')+(!tab.model?' is-restorable':'')+(tab.error?' is-missing':'');
    const activate=document.createElement('button');activate.type='button';activate.className='ip-tab-activate';activate.dataset.tabId=tab.id;activate.id='pn-tab-'+tab.id;activate.setAttribute('role','tab');activate.setAttribute('aria-selected',String(tab.id===activeTabId));activate.tabIndex=tab.id===(activeTabId||documentTabs[0]?.id)?0:-1;activate.disabled=documentOperationBusy();
    const label=document.createElement('span');label.className='ip-tab-label';label.textContent=tab.filename||'PDF';activate.append(label);
    if(tabDirty(tab)){const dot=document.createElement('span');dot.textContent='●';dot.className='ip-tab-dirty';dot.setAttribute('aria-label','未保存');activate.append(dot);}
    else if(!tab.model){const pending=document.createElement('small');pending.textContent=tab.error?'重试':'待恢复';activate.append(pending);}
    activate.title=[tab.sourcePath||'浏览器未提供完整路径',tab.error||''].filter(Boolean).join('\n');
    const close=document.createElement('button');close.type='button';close.className='ip-tab-close';close.dataset.closeTabId=tab.id;close.textContent='×';close.setAttribute('aria-label','关闭 '+tab.filename);close.disabled=documentOperationBusy();row.append(activate,close);list.append(row);
  }
  if(focusId)list.querySelector('[data-tab-id="'+focusId+'"]')?.focus({preventScroll:true});
}
async function makeTabBackupSnapshot(tab){
  if(!documentTabs.includes(tab)||!tab.model)return null;
  if(tab.id===activeTabId)return makeBackupSnapshot();
  const model=tab.model;if(!model.revision)return null;
  const snapshot={sourceBytes:model.sourceBytes,documentId:model.documentId,pages:clone(model.pages),bookmarkEdits:clone(model.bookmarkEdits)};
  const documentId=model.backupDocumentId||model.documentId,filename=model.filename,sourcePath=model.sourcePath,revision=model.sessionId+':'+model.revision;
  snapshot.inkPages=await window.ProperNotesText.preparePages(model.inkPages);
  const bytes=await window.InkPDFStorage.save(snapshot);return{bytes,documentId,filename,sourcePath,revision};
}
function ensureTabScheduler(tab){
  if(!tab.scheduler){tab.scheduler=createBackupScheduler({client:backupClient,getSnapshot:()=>makeTabBackupSnapshot(tab),onStatus:reportBackupStatus});tab.scheduler.start().catch(error=>reportBackupStatus({state:'error',message:error.message,error}));}
  backupScheduler=tab.scheduler;
}
function commitOpenedTab({nativeId=null,browserId=null,restoreTab=null,reading=null,fingerprint=null}={}){
  const tab=restoreTab||{id:newSessionId(),model:null,scheduler:null};
  if(!documentTabs.includes(tab))documentTabs.push(tab);
  activeTabId=tab.id;tab.nativeId=nativeId;tab.browserId=browserId;tab.fingerprint=fingerprint;tab.filename=state.filename;tab.sourcePath=state.sourcePath;tab.model=captureDocumentModel();tab.reading=reading||captureNavigation();tab.error=null;tab.dirtyOnRestore=false;
  ensureTabScheduler(tab);renderDocumentTabs();scheduleWorkspacePersist();return tab;
}
async function restoreReading(reading){
  if(!reading||!state.pdf)return;
  // Page numbers are clamped against the current disk file. A remembered page
  // count/coordinate never authorizes editing an older PDF snapshot.
  if(reading.anchor){const index=Math.max(0,Math.min(state.pages.length-1,reading.anchor.index));documentView?.restoreAnchor({...reading.anchor,index});}
  else{viewport.scrollLeft=reading.scrollLeft||0;viewport.scrollTop=reading.scrollTop||0;}
}
async function activateDocumentTab(id,{fromOpen=false}={}){
  const tab=documentTabs.find(item=>item.id===id);if(!tab)return false;
  if(tab.id===activeTabId&&tab.model)return true;
  if(documentOperationBusy({fromOpen})){toast('请等待当前文件或页面操作完成');return false;}
  if(!commitPendingAnnotationField())return false;
  if(!tab.model)return reopenWorkspaceTab(tab);
  if(!discardBookmarkDraft())return false;
  const previous=captureActiveTab(),previousModel=state.pdf?captureDocumentModel():null,previousReading=state.pdf?captureNavigation():null,previousId=activeTabId;
  tabBusy=true;++workspaceInteraction;resetDocumentInteraction();++documentGeneration;++renderGeneration;
  try{
    applyDocumentModel(tab.model);
    if(!await renderPage(tab.reading?.pageIndex??state.pageIndex,{newDocument:true}))throw new Error('无法恢复此标签的页面，已保留原标签。');
    activeTabId=tab.id;await restoreReading(tab.reading);ensureTabScheduler(tab);
    if(tab.model.navigation?.selectionIds?.length&&isAnnotationSelectionTool())engine.selectAnnotations(tab.model.navigation.selectionIds);
    updateDirtyLabel();readOutline(state.pdf,documentGeneration);hideError();scheduleWorkspacePersist();return true;
  }catch(error){
    if(previousModel){applyDocumentModel(previousModel);activeTabId=previousId;++documentGeneration;if(documentView)activatePageRecord(documentView.getActiveRecord(),documentView.getActiveRecord().index);if(previousReading?.anchor)documentView?.restoreAnchor(previousReading.anchor);if(previous)backupScheduler=previous.scheduler;renderSidebar();renderTool();updatePageLabels();updateDirtyLabel();}
    showError(error);return false;
  }finally{tabBusy=false;updateButtons();renderDocumentTabs();}
}
function resetEmptyDocument(){
  documentView?.destroy();documentView=null;thumbnailObserver?.disconnect();
  paperTray.replaceChildren(frameTemplate.cloneNode(true));ui('page-frame').hidden=true;
  state.pdf=null;state.sourceBytes=null;state.pages=[];state.inkPages=[];state.histories=new Map();state.filename='';state.handle=null;state.fileBaseline=null;state.nativeFileToken=null;state.sourcePath=null;state.documentId=null;state.backupDocumentId=null;state.sessionId=null;state.pageIndex=0;state.targetIndex=0;state.revision=0;state.dirty=false;state.bookmarks=[];state.bookmarkEdits=[];state.bookmarksEditable=false;state.bookmarksMessage='';state.outline=[];state.selection=0;state.selectionLocked=0;state.selectionTools=[];navigationPast=[];navigationFuture=[];bookmarkSelectedId=null;readonlyBookmarkExpansion=new Map();bookmarkEditorDraft=null;pageTextNotice='';
  engine=window.createInkPDFEngine(ui('ink'),{});engine.setInputSettings(state.ink);engine.setTool(toolForEngine(state.tool));engine.setStyle({...currentStyle(),eraserMode:state.eraserMode});
  activeTabId=null;backupScheduler=null;ui('empty').hidden=false;$('.ip-favourites').hidden=true;renderSidebar();renderTool();updatePageLabels();updateDirtyLabel();
}
async function closeDocumentTab(id,{discard=false}={}){
  const tab=documentTabs.find(item=>item.id===id);if(!tab||documentOperationBusy())return;
  if(!commitPendingAnnotationField())return;
  if(tab.id===activeTabId&&!discardBookmarkDraft())return;
  captureActiveTab();
  if(tabDirty(tab)&&!discard){
    if(window.confirm('“'+tab.filename+'”有未保存的更改。确定先保存再关闭吗？取消可选择放弃或继续编辑。')){
      if(tab.id!==activeTabId&&!await activateDocumentTab(tab.id))return;
      await savePdf();captureActiveTab();if(tabDirty(tab)){toast('保存尚未确认，标签仍保留');return;}
    }else if(!window.confirm('放弃“'+tab.filename+'”未保存的更改并关闭？取消将继续编辑。'))return;
  }
  const index=documentTabs.indexOf(tab);
  if(tab.id===activeTabId){
    const next=documentTabs.slice(index+1).concat(documentTabs.slice(0,index).reverse()).find(item=>item.model);
    if(next){if(!await activateDocumentTab(next.id))return;}
    else{resetDocumentInteraction();++documentGeneration;++renderGeneration;resetEmptyDocument();}
  }
  persistTabReading(tab);tab.scheduler?.stop();tab.model?.pdf?.destroy().catch(()=>{});documentTabs.splice(documentTabs.indexOf(tab),1);++workspaceInteraction;renderDocumentTabs();scheduleWorkspacePersist();
}
function persistTabReading(tab){
  if(!tab?.reading)return;
  if(tab.nativeId)enqueueWorkspaceWrite({reading:{id:tab.nativeId,value:tab.reading}}).catch(()=>{});
  else if(tab.browserId)try{browserWorkspaceSnapshot=browserWorkspace.update({reading:{id:tab.browserId,value:tab.reading}});}catch(error){renderWorkspaceStatus(error.message);}
}
function workspaceRecords(){return [...(workspaceSnapshot?.records||workspaceSnapshot?.recents||[]),...(browserWorkspaceSnapshot.records||browserWorkspaceSnapshot.recents||[])];}
function renderWorkspaceStatus(message=workspaceNotice){
  workspaceNotice=message;ui('workspace-status').textContent=message;$('[data-setting=restore-session]').checked=restoreSession;
  if(ui('recent-dialog').open)renderRecentFiles();
}
function workspaceSession(){
  const tabs=documentTabs.map(tab=>({id:tab.nativeId||tab.browserId,dirty:tabDirty(tab)||!!tab.dirtyOnRestore})).filter(tab=>tab.id);
  const active=activeDocumentTab(),id=active?.nativeId||active?.browserId;return{tabs,activeId:tabs.some(tab=>tab.id===id)?id:tabs[0]?.id||null};
}
function enqueueWorkspaceWrite(patch,{keepalive=false}={}){
  const operation=workspaceTail.catch(()=>{}).then(async()=>{
    const next=await fileClient.updateWorkspace(patch,{keepalive});workspaceSnapshot=next;
    renderWorkspaceStatus([next.message||'最近文件与阅读状态已保存。',browserWorkspaceSnapshot.canWrite?'':browserWorkspaceSnapshot.message].filter(Boolean).join(' '));return next;
  });
  workspaceTail=operation.catch(error=>renderWorkspaceStatus('阅读记录未能保存：'+error.message));return operation;
}
function flushWorkspace({keepalive=false}={}){
  clearTimeout(workspaceTimer);workspaceTimer=null;
  if(!workspaceReady||workspaceRestoring||tabBusy||state.opening||state.rendering)return;
  if(!keepalive&&documentView?.getRecords().some(record=>record.engine.hasActiveGesture())){scheduleWorkspacePersist();return;}
  captureActiveTab({finish:keepalive});const session=workspaceSession();
  const browserUpdates=documentTabs.filter(tab=>tab.browserId&&tab.reading).map(tab=>({id:tab.browserId,value:tab.reading}));
  try{for(const reading of browserUpdates)browserWorkspaceSnapshot=browserWorkspace.update({reading});browserWorkspaceSnapshot=browserWorkspace.update({restoreSession,session});}
  catch(error){renderWorkspaceStatus('浏览器阅读记录未能保存：'+error.message);}
  const nativeTabs=session.tabs.filter(tab=>tab.id.startsWith('document-'));
  const reading=documentTabs.filter(tab=>tab.nativeId&&tab.reading).map(tab=>({id:tab.nativeId,value:tab.reading}));
  const patch={restoreSession,session:{tabs:nativeTabs,activeId:nativeTabs.some(tab=>tab.id===session.activeId)?session.activeId:nativeTabs[0]?.id||null},readings:reading};
  // The keepalive request is best-effort at exit; periodic metadata saves are
  // the primary path. Unload does not claim a successful disk commit.
  enqueueWorkspaceWrite(patch,{keepalive}).catch(()=>{});
  if(!browserWorkspaceSnapshot.canWrite)renderWorkspaceStatus(browserWorkspaceSnapshot.message);
}
function scheduleWorkspacePersist(){
  if(!workspaceReady||workspaceRestoring)return;
  clearTimeout(workspaceTimer);workspaceTimer=setTimeout(()=>flushWorkspace(),650);
}
function renderRecentFiles(){
  const list=ui('recent-list');list.replaceChildren();
  const entries=[...(workspaceSnapshot?.recents||[]),...(browserWorkspaceSnapshot.recents||[])].sort((a,b)=>new Date(b.lastOpened)-new Date(a.lastOpened));
  for(const entry of entries){
    const row=document.createElement('div');row.className='ip-recent-entry';const open=document.createElement('button');open.type='button';open.dataset.recentId=entry.id;open.disabled=documentOperationBusy();
    const name=document.createElement('strong'),path=document.createElement('small'),detail=document.createElement('small');name.textContent=entry.filename;path.textContent=entry.sourcePath||'浏览器文件 · 需重新选择并核对内容';detail.textContent=(entry.reading?'第 '+(entry.reading.pageIndex+1)+' 页 · ':'')+new Date(entry.lastOpened).toLocaleString();open.append(name,path,detail);
    const remove=document.createElement('button');remove.type='button';remove.className='ip-icon-button';remove.dataset.removeRecentId=entry.id;remove.textContent='×';remove.setAttribute('aria-label','从最近文件移除 '+entry.filename);row.append(open,remove);list.append(row);
  }
  ui('recent-status').textContent=workspaceNotice+(entries.length?' 共 '+entries.length+' 条。':' 暂无最近文件。');
}
async function openRecentFiles(){
  ++workspaceInteraction;if(ui('settings-dialog').open)ui('settings-dialog').close();engine?.finishGesture();if(!ui('recent-dialog').open)ui('recent-dialog').showModal();renderRecentFiles();
  try{workspaceSnapshot=await fileClient.workspace();renderRecentFiles();}catch(error){renderWorkspaceStatus('无法刷新本地最近文件：'+error.message);}
}
async function removeRecent(id){
  try{if(id.startsWith('browser-'))browserWorkspaceSnapshot=browserWorkspace.update({removeRecentId:id});else await enqueueWorkspaceWrite({removeRecentId:id});renderRecentFiles();}catch(error){renderWorkspaceStatus(error.message);}
}
async function clearRecentFiles(){
  if(!window.confirm('清空最近文件列表？PDF 和本地备份会保留。'))return;
  browserWorkspaceSnapshot=browserWorkspace.update({clearRecents:true});try{await enqueueWorkspaceWrite({clearRecents:true});}catch{/* Status is reported by the queue. */}renderRecentFiles();
}
async function fingerprintBytes(bytes){try{if(!globalThis.crypto?.subtle)return null;return [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(value=>value.toString(16).padStart(2,'0')).join('');}catch{return null;}}
async function reopenWorkspaceTab(tab){
  if(documentOperationBusy())return false;
  if(tab.browserId){const record=workspaceRecords().find(entry=>entry.id===tab.browserId);if(!record){renderWorkspaceStatus('此浏览器文件的指纹记录不可用，请通过普通打开进入新标签。');return false;}ui('recent-dialog').close();toast('此文件由浏览器打开，请重新选择原 PDF；核对内容后恢复阅读位置');return chooseFile({browserRecord:record,restoreTab:tab});}
  nativeDialogBusy=true;updateButtons();const serial=++fileOpenSerial,generation=documentGeneration;
  try{
    const selected=await fileClient.reopen(tab.nativeId);
    if(serial!==fileOpenSerial||generation!==documentGeneration)return false;
    if(ui('recent-dialog').open)ui('recent-dialog').close();
    const result=await openBytes(selected.bytes,selected.filename,{sourcePath:selected.sourcePath,backupDocumentId:selected.backupDocumentId,nativeFileToken:selected.token,workspaceWarning:selected.workspaceWarning,restoreTab:tab,reading:tab.reading});
    return result;
  }catch(error){tab.error=error.message;renderWorkspaceStatus('“'+tab.filename+'”未能恢复：'+error.message);renderDocumentTabs();return false;}
  finally{nativeDialogBusy=false;updateButtons();}
}
async function openRecentId(id){
  const existing=documentTabs.find(tab=>(tab.nativeId||tab.browserId)===id);if(existing){ui('recent-dialog').close();return activateDocumentTab(existing.id);}
  if(documentTabs.length>=32){toast('当前最多保留 32 个标签，请先关闭一些文件');return;}
  const record=workspaceRecords().find(entry=>entry.id===id);if(!record)return;
  const tab={id:newSessionId(),nativeId:id.startsWith('document-')?id:null,browserId:id.startsWith('browser-')?id:null,filename:record.filename,sourcePath:record.sourcePath||null,reading:record.reading,model:null,scheduler:null};
  documentTabs.push(tab);renderDocumentTabs();const result=await reopenWorkspaceTab(tab);scheduleWorkspacePersist();return result;
}
function restoreSessionPlaceholders(snapshot,local){
  const records=workspaceRecords();
  const localTabs=local.session?.tabs||[],nativeTabs=snapshot?.session?.tabs||[];
  // Local metadata preserves mixed browser/native ordering only when its native
  // set still matches the authoritative disk session. Other origins use disk.
  const a=localTabs.filter(tab=>tab.id.startsWith('document-')).map(tab=>tab.id),b=nativeTabs.map(tab=>tab.id);
  const useLocal=localTabs.length&&(!snapshot?.canWrite||(localTabs.some(tab=>tab.id.startsWith('browser-'))&&a.length===b.length&&a.every((id,index)=>id===b[index])&&(local.session.activeId?.startsWith('browser-')||local.session.activeId===snapshot.session.activeId)));
  const session=useLocal?local.session:snapshot?.session||local.session;
  for(const saved of session?.tabs||[]){
    if(documentTabs.length>=32)break;const record=records.find(entry=>entry.id===saved.id);if(!record)continue;
    const authoritative=saved.id.startsWith('document-')?nativeTabs.find(item=>item.id===saved.id)||saved:saved;
    documentTabs.push({id:newSessionId(),nativeId:saved.id.startsWith('document-')?saved.id:null,browserId:saved.id.startsWith('browser-')?saved.id:null,filename:record.filename,sourcePath:record.sourcePath||null,reading:record.reading,dirtyOnRestore:!!authoritative.dirty,model:null,scheduler:null});
  }
  return documentTabs.find(tab=>(tab.nativeId||tab.browserId)===session?.activeId)||documentTabs[0];
}
async function initializeWorkspace(){
  const serial=++workspaceLoadSerial,interaction=workspaceInteraction;workspaceRestoring=true;
  try{workspaceSnapshot=await fileClient.workspace();if(!workspacePreferenceTouched)restoreSession=(workspaceSnapshot.canWrite?workspaceSnapshot:browserWorkspaceSnapshot).restoreSession!==false;renderWorkspaceStatus(workspaceSnapshot.message||'本机最近文件与阅读状态已载入。');}
  catch(error){if(!workspacePreferenceTouched)restoreSession=browserWorkspaceSnapshot.restoreSession!==false;renderWorkspaceStatus('本地会话服务不可用。'+(browserWorkspaceSnapshot.message||'浏览器记录仅保存在当前地址。'));}
  workspaceReady=true;
  try{
    if(serial!==workspaceLoadSerial||interaction!==workspaceInteraction||documentTabs.length||state.opening)return;
    if(!restoreSession)return;
    const tab=restoreSessionPlaceholders(workspaceSnapshot,browserWorkspaceSnapshot);renderDocumentTabs();
    if(documentTabs.some(item=>item.dirtyOnRestore))renderWorkspaceStatus('上次有未保存的批注。恢复会读取磁盘 PDF；请在本地备份中核对未保存版本。');
    if(tab?.nativeId)await reopenWorkspaceTab(tab);
    else if(tab)renderWorkspaceStatus('已恢复浏览器文件标签；点击标签重新选择原 PDF 后恢复阅读位置。');
  }finally{workspaceRestoring=false;renderDocumentTabs();updateButtons();if(documentTabs.length||workspaceInteraction!==interaction)scheduleWorkspacePersist();}
}

