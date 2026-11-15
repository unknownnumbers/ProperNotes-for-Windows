/* User-selected Windows file access. No path or filesystem access occurs on import. */
export class FileClientError extends Error {
  constructor(message,code,status){super(message);this.name='FileClientError';this.code=code;this.status=status;}
}
export function createFileClient(){
  let token=null;
  async function errorOf(response){
    let value;try{value=await response.json();}catch{/* An invalid response is not success. */}
    return new FileClientError(value?.error?.message||'本地文件服务请求失败。',value?.error?.code||'FILE_SERVICE_FAILED',response.status);
  }
  async function session(){
    if(token)return token;
    const response=await fetch('/api/session',{cache:'no-store',credentials:'same-origin'});
    if(!response.ok)throw await errorOf(response);
    const value=await response.json();
    if(typeof value?.token!=='string'||!value.token)throw new FileClientError('本地文件会话无法验证。','INVALID_SESSION',502);
    token=value.token;return token;
  }
  function metadata(value){
    if(!value||typeof value.token!=='string'||!value.token||typeof value.filename!=='string'||!value.filename
      ||typeof value.sourcePath!=='string'||!value.sourcePath||!/^document-[A-Za-z0-9_-]{1,120}$/.test(value.documentId)
      ||typeof value.hash!=='string'||!/^[a-f\d]{64}$/i.test(value.hash))throw new FileClientError('本地文件服务返回了无效的文件信息。','INVALID_FILE_RESPONSE',502);
    // The service ID identifies the selected physical path for backup grouping.
    // It must never replace the logical editing ID stored inside a PDF.
    return{token:value.token,filename:value.filename,sourcePath:value.sourcePath,backupDocumentId:value.documentId,hash:value.hash,
      ...(typeof value.workspaceWarning==='string'?{workspaceWarning:value.workspaceWarning.slice(0,2000)}:{})};
  }
  async function request(route,{bytes,details,json,keepalive=false}={},retry=true){
    const headers={'X-ProperNotes-Token':await session(),'Content-Type':bytes?'application/pdf':'application/json'};
    if(details)headers['X-ProperNotes-Metadata']=encodeURIComponent(JSON.stringify(details));
    const response=await fetch(route,{method:'POST',headers,body:bytes||JSON.stringify(json||{}),cache:'no-store',credentials:'same-origin',keepalive});
    if(!response.ok){
      const error=await errorOf(response);
      if(retry&&response.status===403&&error.code==='INVALID_TOKEN'){token=null;return request(route,{bytes,details,json,keepalive},false);}
      throw error;
    }
    return response;
  }
  async function selectedFile(route,json){
      const response=await request(route,{json});if(response.status===204)return null;
      let details;try{details=metadata(JSON.parse(decodeURIComponent(response.headers.get('X-ProperNotes-File')||'')));}
      catch(error){if(error instanceof FileClientError)throw error;throw new FileClientError('无法读取本地文件响应信息。','INVALID_FILE_RESPONSE',502);}
      const bytes=new Uint8Array(await response.arrayBuffer());
      if(bytes.length<5||bytes.length>640*1024*1024||!String.fromCharCode(...bytes.slice(0,1024)).includes('%PDF-'))throw new FileClientError('本地文件不是当前支持的 PDF 数据。','INVALID_FILE_RESPONSE',502);
      return{...details,bytes};
  }
  function workspaceValue(value){
    if(!value||typeof value.restoreSession!=='boolean'||!Array.isArray(value.recents)||value.recents.length>100
      ||!Array.isArray(value.records)||value.records.length>256||!value.session||!Array.isArray(value.session.tabs)||value.session.tabs.length>32
      ||typeof value.canWrite!=='boolean'||typeof value.message!=='string')throw new FileClientError('本地会话服务返回了无效记录。','INVALID_WORKSPACE_RESPONSE',502);
    return value;
  }
  return Object.freeze({
    open:()=>selectedFile('/api/files/open'),
    reopen:id=>selectedFile('/api/files/reopen',{id}),
    async workspace(){return workspaceValue(await(await request('/api/workspace/get')).json());},
    async updateWorkspace(value,{keepalive=false}={}){return workspaceValue(await(await request('/api/workspace/update',{json:value,keepalive})).json());},
    async save({bytes,token:currentToken=null,saveAs=false,filename='document.pdf',excludeTokens=[],excludeDocumentIds=[]}={}){
      if(!(bytes instanceof Uint8Array)||!bytes.length||bytes.length>640*1024*1024)throw new FileClientError('PDF 数据为空或超过本地文件接口的 640 MiB 范围。','FILE_SIZE',400);
      let response;
      try{response=await request('/api/files/save',{bytes,details:{token:currentToken,saveAs,filename,excludeTokens,excludeDocumentIds}});}
      catch(error){
        // Preserve explicit pre-write refusals and the unavailable-dialog fallback.
        // Unknown service failures can arrive after the file was published.
        if(error instanceof FileClientError&&error.code==='NATIVE_DIALOG_UNAVAILABLE')throw error;
        if(error instanceof FileClientError&&error.status<500&&!['FILE_SAVE_FAILED','FILE_SERVICE_FAILED'].includes(error.code))throw error;
        const uncertain=new FileClientError('保存响应中断，结果尚未确认。请核对目标文件，当前批注仍保留。','SAVE_UNCONFIRMED',0);uncertain.resultUnconfirmed=true;throw uncertain;
      }
      if(response.status===204)return null;
      try{return metadata(await response.json());}
      catch{
        const uncertain=new FileClientError('文件服务未能确认保存结果。请核对目标文件，当前批注仍保留。','SAVE_UNCONFIRMED',response.status);uncertain.resultUnconfirmed=true;throw uncertain;
      }
    },
  });
}
