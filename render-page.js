async function renderPage(index,{newDocument=false,preserveView=false}={}) {
  if(!state.pdf)return false;
  if(!newDocument)saveCurrentPage();
  index=Math.max(0,Math.min(Math.trunc(index)||0,state.pages.length-1));
  const serial=++renderGeneration,documentSerial=documentGeneration,previousView=documentView,previousEngine=engine;
  const previousTrayNodes=[...paperTray.childNodes];
  const previousScroll={left:viewport.scrollLeft,top:viewport.scrollTop};
  let candidate=null;
  state.targetIndex=index;state.rendering=true;setBusy(true,'正在显示第 '+(index+1)+' 页…');updatePageLabels();
  try {
    if(newDocument||!documentView){
      candidate=makeDocumentView();await candidate.prepare(index);
      if(serial!==renderGeneration||documentSerial!==documentGeneration){candidate.destroy();return false;}
      documentView=candidate;candidate.mount();viewport.scrollLeft=0;viewport.scrollTop=0;
    }else await documentView.show(index,{preserveView});
    if(serial!==renderGeneration||documentSerial!==documentGeneration)return false;
    activatePageRecord(documentView.getActiveRecord(),index);
    fitIfNeeded();setZoom(state.zoom,state.zoomMode,{force:true});syncAnnotationVisibility();
    if(candidate){viewport.scrollLeft=0;viewport.scrollTop=0;}
    ui('empty').hidden=true;$('.ip-favourites').hidden=false;renderSidebar();renderTool();
    if(candidate&&previousView&&previousView!==candidate)previousView.destroy();
    else if(candidate&&previousEngine!==engine)previousEngine?.destroy();
    return true;
  }catch(error){
    if(candidate){documentView=previousView;engine=previousEngine;candidate.destroy();if(previousView){if(!previousView.getContainer().isConnected)previousView.mount();}else paperTray.replaceChildren(...previousTrayNodes);viewport.scrollLeft=previousScroll.left;viewport.scrollTop=previousScroll.top;}
    if(serial===renderGeneration&&documentSerial===documentGeneration){state.targetIndex=state.pageIndex;if(error?.name!=='AbortError'&&error?.name!=='RenderingCancelledException')showError(error);updatePageLabels();}
    return false;
  }finally{
    if(serial===renderGeneration){restoring=false;state.rendering=false;setBusy(state.opening);updateButtons();scheduleWorkspacePersist();}
  }
}
