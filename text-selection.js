/* Map a native PDF text selection to orthogonal, page-local review quadrilaterals. */
export function selectedTextQuads(records, zoom) {
  const selection=window.getSelection();
  if(!selection||selection.isCollapsed||selection.rangeCount!==1)throw new Error('请先用“文字”工具选择同一页的 PDF 正文。');
  const range=selection.getRangeAt(0);
  const record=records.find(item=>{
    const layer=item.els['pdf-text'];return layer.isConnected&&!layer.hidden&&layer.contains(range.startContainer)&&layer.contains(range.endContainer);
  });
  if(!record)throw new Error('请在同一页的正文文本层中选择；跨页选区请分开标记。');
  if(!Number.isFinite(zoom)||zoom<=0)throw new Error('当前页面倍率无效，未添加标记。');
  const layer=record.els['pdf-text'],paper=record.els.paper,paperBox=paper.getBoundingClientRect();
  const walker=document.createTreeWalker(layer,NodeFilter.SHOW_TEXT),points=[],seen=new Set();
  function directions(element){
    let matrix=new DOMMatrix();
    for(let current=element;current&&current!==paper;current=current.parentElement){
      const style=getComputedStyle(current),local=style.transform==='none'?new DOMMatrix():new DOMMatrix(style.transform);
      if(style.rotate&&style.rotate!=='none'){
        if(!/^-?[\d.]+deg$/.test(style.rotate))throw new Error('此文字采用暂不支持的旋转方式，未添加标记。');
        const rotation=new DOMMatrix().rotate(parseFloat(style.rotate));matrix=rotation.multiply(local).multiply(matrix);
      }else matrix=local.multiply(matrix);
    }
    const lengthX=Math.hypot(matrix.a,matrix.b),lengthY=Math.hypot(matrix.c,matrix.d);
    if(!lengthX||!lengthY||matrix.a*matrix.d-matrix.b*matrix.c<=0)throw new Error('此文字含不支持的镜像或退化变换，未添加标记。');
    const x=[matrix.a/lengthX,matrix.b/lengthX],y=[matrix.c/lengthY,matrix.d/lengthY];
    const orthogonal=vector=>Math.min(Math.abs(vector[0]),Math.abs(vector[1]))<.0001;
    if(!orthogonal(x)||!orthogonal(y)||Math.abs(x[0]*y[0]+x[1]*y[1])>.0001)throw new Error('此选区含非直角旋转文字，暂不能准确标记；未添加批注。');
    return{x:x.map(value=>Math.abs(value)<.0001?0:Math.sign(value)),y:y.map(value=>Math.abs(value)<.0001?0:Math.sign(value))};
  }
  for(let node=walker.nextNode();node;node=walker.nextNode()){
    if(!node.textContent||!range.intersectsNode(node))continue;
    const part=document.createRange();part.selectNodeContents(node);
    if(range.compareBoundaryPoints(Range.START_TO_START,part)>0)part.setStart(range.startContainer,range.startOffset);
    if(range.compareBoundaryPoints(Range.END_TO_END,part)<0)part.setEnd(range.endContainer,range.endOffset);
    if(part.collapsed)continue;
    const {x,y}=directions(node.parentElement);
    for(const raw of part.getClientRects()){
      const left=Math.max(raw.left,paperBox.left),top=Math.max(raw.top,paperBox.top),right=Math.min(raw.right,paperBox.right),bottom=Math.min(raw.bottom,paperBox.bottom);
      if(right-left<.01||bottom-top<.01)continue;
      const corners=[[left,top],[right,top],[right,bottom],[left,bottom]];
      const u=corners.map(p=>p[0]*x[0]+p[1]*x[1]),v=corners.map(p=>p[0]*y[0]+p[1]*y[1]);
      const minU=Math.min(...u),maxU=Math.max(...u),minV=Math.min(...v),maxV=Math.max(...v);
      const quad=[[minU,minV],[maxU,minV],[maxU,maxV],[minU,maxV]].map(([a,b])=>[(a*x[0]+b*y[0]-paperBox.left)/zoom,(a*x[1]+b*y[1]-paperBox.top)/zoom,.5]);
      const key=quad.map(p=>p.slice(0,2).map(value=>Math.round(value*1000)).join(',')).join('/');if(seen.has(key))continue;seen.add(key);
      if(points.length+4>16000)throw new Error('选区过大，请分段添加文字标记；本次未添加。');points.push(...quad);
    }
  }
  if(!points.length)throw new Error('未取得可标记的正文选区；扫描页面需要已有文本层。');
  return{record,pageIndex:record.index,points};
}
