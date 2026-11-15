/* Bounded Unicode text appearances. Importing this module does not access DOM or fonts. */
(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.ProperNotesText = factory();
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const TOOLS = ['textbox', 'note'];
  const FAMILY = { sans: 'Arial, "Microsoft YaHei", sans-serif', serif: '"Times New Roman", SimSun, serif', mono: 'Consolas, "Microsoft YaHei", monospace' };
  const MAX_TEXT = 20000, MAX_PIXELS = 4000000, MAX_EDGE = 4096;
  const MAX_TOTAL_TEXT = 1000000;
  const MAX_BYTES = 8 * 1024 * 1024, MAX_TOTAL_BYTES = 24 * 1024 * 1024, MAX_TOTAL_PIXELS = 16000000;
  const PNG_SIGNATURE = [137,80,78,71,13,10,26,10];
  const CRC_TABLE = Array.from({length:256}, (_, value) => {
    let result=value;for(let bit=0;bit<8;bit++)result=result&1?0xedb88320^(result>>>1):result>>>1;return result>>>0;
  });
  let measureCanvas=null;
  class TextAppearanceError extends Error {
    constructor(code,message){super(message);this.name='TextAppearanceError';this.code=code;}
  }
  function fail(code,message){throw new TextAppearanceError(code,message);}
  function finite(value){return typeof value==='number'&&Number.isFinite(value);}
  function color(value){
    if(typeof value!=='string'||!/^#(?:[\da-f]{3}|[\da-f]{6})$/i.test(value))fail('TEXT_COLOR','文字颜色无效。');
    const hex=value.slice(1).toLowerCase();return '#'+(hex.length===3?hex.split('').map(c=>c+c).join(''):hex);
  }
  function validUnicode(value){
    for(let index=0;index<value.length;index++){
      const code=value.charCodeAt(index);
      if(code>=0xd800&&code<=0xdbff){const next=value.charCodeAt(++index);if(!(next>=0xdc00&&next<=0xdfff))return false;}
      else if(code>=0xdc00&&code<=0xdfff)return false;
    }
    return true;
  }
  function descriptor(annotation){
    if(!annotation||!TOOLS.includes(annotation.tool)||typeof annotation.text!=='string'||annotation.text.length>MAX_TEXT
      ||!validUnicode(annotation.text)||/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(annotation.text))fail('TEXT_DATA','文字内容无效或超过 20000 个字符。');
    if(!finite(annotation.fontSize)||annotation.fontSize<4||annotation.fontSize>240||!Object.hasOwn(FAMILY,annotation.fontFamily)
      ||typeof annotation.bold!=='boolean'||typeof annotation.italic!=='boolean'||!finite(annotation.width)||annotation.width<.1||annotation.width>100
      ||!Array.isArray(annotation.points)||annotation.points.length!==2)fail('TEXT_STYLE','文字样式或文本框无效。');
    const points=annotation.points;
    if(points.some(point=>!Array.isArray(point)||point.length!==3||!point.every(finite)||Math.abs(point[0])>1e9||Math.abs(point[1])>1e9||point[2]!==.5))fail('TEXT_BOX','文本框坐标无效。');
    const left=Math.min(points[0][0],points[1][0]),top=Math.min(points[0][1],points[1][1]);
    const width=Math.abs(points[1][0]-points[0][0]),height=Math.abs(points[1][1]-points[0][1]);
    if(width<8||height<8||width>1000000||height>1000000)fail('TEXT_BOX','文本框至少为 8 × 8，且不能超过当前范围。');
    const fill=annotation.fill===undefined?'none':annotation.fill;
    return {tool:annotation.tool,left,top,width,height,text:annotation.text,fontSize:annotation.fontSize,fontFamily:annotation.fontFamily,
      bold:annotation.bold,italic:annotation.italic,color:color(annotation.color),fill:fill==='none'?'none':color(fill),borderWidth:annotation.width};
  }
  function key(annotation){
    const value=descriptor(annotation);
    return JSON.stringify(['ProperNotesText/1',value.tool,value.width,value.height,value.text,value.fontSize,value.fontFamily,value.bold,value.italic,value.color,value.fill,value.borderWidth]);
  }
  function dimensions(annotation){
    const box=descriptor(annotation),scale=Math.min(2,MAX_EDGE/box.width,MAX_EDGE/box.height,Math.sqrt(MAX_PIXELS/(box.width*box.height)));
    return {width:Math.max(1,Math.floor(box.width*scale)),height:Math.max(1,Math.floor(box.height*scale))};
  }
  function canvas(){
    if(typeof document!=='object'||typeof document.createElement!=='function')fail('TEXT_RENDER_UNAVAILABLE','当前环境没有可用的文字画布，未生成不完整的 PDF。');
    const element=document.createElement('canvas');if(!element.getContext('2d'))fail('TEXT_RENDER_UNAVAILABLE','无法创建文字画布。');return element;
  }
  function layout(annotation){
    const box=descriptor(annotation);if(!measureCanvas)measureCanvas=canvas();
    const context=measureCanvas.getContext('2d'),familyCSS=FAMILY[box.fontFamily];
    const fontCSS=(box.italic?'italic ':'')+(box.bold?'bold ':'')+box.fontSize+'px '+familyCSS;
    context.font=fontCSS;context.textAlign='left';context.textBaseline='alphabetic';
    const padding=6,lineHeight=box.fontSize*1.3,available=Math.max(0,box.width-padding*2),lines=[];
    const paragraphs=box.text.replace(/\r\n?/g,'\n').replace(/\t/g,'    ').split('\n');let overflow=false,horizontalOverflow=false;
    const maxLines=Math.max(0,Math.floor((box.height-padding*2-box.fontSize)/lineHeight)+1);
    function push(text){if(lines.length>=maxLines){overflow=true;return false;}lines.push({text,x:padding,y:padding+box.fontSize+lines.length*lineHeight});return true;}
    for(let paragraphIndex=0;paragraphIndex<paragraphs.length;paragraphIndex++){
      const characters=Array.from(paragraphs[paragraphIndex]);
      if(!characters.length){if(!push(''))break;continue;}
      let start=0;
      while(start<characters.length){
        if(lines.length>=maxLines){overflow=true;break;}
        const remaining=characters.length-start;
        const measured=count=>context.measureText(characters.slice(start,start+count).join('')).width;
        let count=1;
        if(measured(1)>available)horizontalOverflow=true;
        else{
          // Exponential search keeps very long paragraphs and narrow boxes
          // bounded without measuring every growing prefix of the paragraph.
          let low=1,high=Math.min(2,remaining);
          while(high<remaining&&measured(high)<=available){low=high;high=Math.min(remaining,high*2);}
          if(measured(high)<=available)count=high;
          else{while(low+1<high){const middle=Math.floor((low+high)/2);if(measured(middle)<=available)low=middle;else high=middle;}count=low;}
        }
        if(!push(characters.slice(start,start+count).join('')))break;start+=count;
      }
      if(overflow)break;
    }
    return {width:box.width,height:box.height,padding,lineHeight,fontCSS,familyCSS,lines,overflow:overflow||horizontalOverflow};
  }
  function decodeBase64(value){
    if(typeof value!=='string'||!value.length||value.length>Math.ceil(MAX_BYTES/3)*4||value.length%4||!/^[A-Za-z0-9+/]*={0,2}$/.test(value))fail('TEXT_RASTER','文字外观 PNG 数据无效或过大。');
    let bytes;
    try{
      if(typeof atob==='function'){const binary=atob(value);if(btoa(binary)!==value)throw new Error('Noncanonical base64');bytes=Uint8Array.from(binary,character=>character.charCodeAt(0));}
      else if(typeof Buffer==='function'){const buffer=Buffer.from(value,'base64');if(buffer.toString('base64')!==value)throw new Error('Noncanonical base64');bytes=new Uint8Array(buffer);}
      else fail('TEXT_RASTER_UNAVAILABLE','当前环境无法读取文字外观。');
    }catch(error){if(error instanceof TextAppearanceError)throw error;fail('TEXT_RASTER','文字外观编码无效。');}
    if(bytes.length>MAX_BYTES)fail('TEXT_RASTER','单个文字外观超过 8 MiB。');return bytes;
  }
  function unsigned(bytes,index){return ((bytes[index]*0x1000000)+(bytes[index+1]<<16)+(bytes[index+2]<<8)+bytes[index+3])>>>0;}
  function crc(bytes,start,end){let result=0xffffffff;for(let index=start;index<end;index++)result=CRC_TABLE[(result^bytes[index])&255]^(result>>>8);return (result^0xffffffff)>>>0;}
  function parsePNG(bytes){
    if(bytes.length<45||PNG_SIGNATURE.some((value,index)=>bytes[index]!==value))fail('TEXT_RASTER','文字外观不是有效 PNG。');
    const idat=[];let offset=8,width=0,height=0,components=0,finished=false,chunks=0,sawData=false,endedData=false;
    while(offset<bytes.length){
      if(++chunks>10000||offset+12>bytes.length)fail('TEXT_RASTER','PNG 结构无效。');
      const length=unsigned(bytes,offset),end=offset+12+length;if(length>MAX_BYTES||end>bytes.length)fail('TEXT_RASTER','PNG 数据块无效。');
      const type=String.fromCharCode(...bytes.subarray(offset+4,offset+8));
      if(crc(bytes,offset+4,offset+8+length)!==unsigned(bytes,offset+8+length))fail('TEXT_RASTER','PNG 校验失败。');
      if(chunks===1&&type!=='IHDR')fail('TEXT_RASTER','PNG 缺少首部。');
      if(type==='IHDR'){
        if(chunks!==1||length!==13)fail('TEXT_RASTER','PNG 首部无效。');
        width=unsigned(bytes,offset+8);height=unsigned(bytes,offset+12);const bitDepth=bytes[offset+16],colorType=bytes[offset+17];
        if(!width||!height||width>MAX_EDGE||height>MAX_EDGE||width*height>MAX_PIXELS||bitDepth!==8||![2,6].includes(colorType)
          ||bytes[offset+18]||bytes[offset+19]||bytes[offset+20])fail('TEXT_RASTER','PNG 尺寸或像素格式不受支持。');
        components=colorType===6?4:3;
      }else if(type==='IDAT'){
        if(endedData)fail('TEXT_RASTER','PNG 图像数据不连续。');sawData=true;idat.push(bytes.subarray(offset+8,offset+8+length));
      }else if(type==='IEND'){
        if(length||!sawData||end!==bytes.length)fail('TEXT_RASTER','PNG 结束数据无效。');finished=true;break;
      }else{
        // Canvas-generated colour metadata is inert. Other chunk types can
        // contain compressed metadata; do not hand them to an unbounded decoder.
        if(!['sRGB','gAMA','cHRM','pHYs'].includes(type))fail('TEXT_RASTER','PNG 含不受支持的附加数据。');
        if(sawData)endedData=true;
      }
      offset=end;
    }
    if(!finished)fail('TEXT_RASTER','PNG 不完整。');return {width,height,components,idat,bytes};
  }
  function validateRaster(annotation,{required=false}={}){
    descriptor(annotation);
    if(!annotation.raster){if(required)fail('TEXT_RASTER_MISSING','文字外观尚未生成，已停止保存以避免缺字。');return null;}
    const value=annotation.raster,expected=dimensions(annotation);
    if(!value||typeof value!=='object'||Array.isArray(value)||value.key!==key(annotation)||value.width!==expected.width||value.height!==expected.height)fail('TEXT_RASTER_STALE','文字外观与当前内容或样式不一致，请重新生成。');
    const png=parsePNG(decodeBase64(value.data));if(png.width!==value.width||png.height!==value.height)fail('TEXT_RASTER','文字外观尺寸不一致。');return png;
  }
  function paeth(a,b,c){const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;}
  async function decodeRaster(annotation){
    const png=validateRaster(annotation,{required:true});
    if(typeof DecompressionStream!=='function'||typeof Blob!=='function')fail('TEXT_RASTER_UNAVAILABLE','当前环境缺少有界 PNG 解压接口，未生成不完整的 PDF。');
    const rowLength=png.width*png.components,expected=(rowLength+1)*png.height,filtered=new Uint8Array(expected);
    let reader,total=0;
    try{
      reader=new Blob(png.idat).stream().pipeThrough(new DecompressionStream('deflate')).getReader();
      while(true){const {done,value}=await reader.read();if(done)break;if(total+value.length>expected){await reader.cancel().catch(()=>{});fail('TEXT_RASTER','PNG 解压数据超过声明尺寸。');}filtered.set(value,total);total+=value.length;}
      if(total!==expected)fail('TEXT_RASTER','PNG 解压数据不完整。');
    }catch(error){if(error instanceof TextAppearanceError)throw error;fail('TEXT_RASTER','PNG 解压失败。');}
    finally{if(reader)reader.releaseLock();}
    const pixels=new Uint8Array(rowLength*png.height);
    for(let y=0;y<png.height;y++){
      const filter=filtered[y*(rowLength+1)];if(filter>4)fail('TEXT_RASTER','PNG 像素过滤器无效。');
      for(let x=0;x<rowLength;x++){
        const position=y*rowLength+x,left=x>=png.components?pixels[position-png.components]:0,up=y?pixels[position-rowLength]:0,upperLeft=y&&x>=png.components?pixels[position-rowLength-png.components]:0;
        const prediction=filter===0?0:filter===1?left:filter===2?up:filter===3?Math.floor((left+up)/2):paeth(left,up,upperLeft);
        pixels[position]=(filtered[y*(rowLength+1)+1+x]+prediction)&255;
      }
    }
    const rgb=new Uint8Array(png.width*png.height*3),alpha=png.components===4?new Uint8Array(png.width*png.height):null;let opaque=true;
    for(let index=0;index<png.width*png.height;index++){rgb.set(pixels.subarray(index*png.components,index*png.components+3),index*3);if(alpha){alpha[index]=pixels[index*4+3];if(alpha[index]!==255)opaque=false;}}
    return {width:png.width,height:png.height,rgb,alpha:opaque?null:alpha};
  }
  function rasterize(annotation){
    const box=descriptor(annotation),size=dimensions(annotation),plan=layout(annotation),element=canvas();element.width=size.width;element.height=size.height;
    const context=element.getContext('2d'),sx=size.width/box.width,sy=size.height/box.height;context.scale(sx,sy);context.clearRect(0,0,box.width,box.height);
    if(box.fill!=='none'){context.fillStyle=box.fill;context.fillRect(0,0,box.width,box.height);}
    context.save();context.beginPath();context.rect(0,0,box.width,box.height);context.clip();context.strokeStyle=box.color;context.lineWidth=box.borderWidth;
    const inset=Math.min(box.borderWidth/2,box.width/2,box.height/2);context.strokeRect(inset,inset,Math.max(0,box.width-inset*2),Math.max(0,box.height-inset*2));
    context.font=plan.fontCSS;context.textAlign='left';context.textBaseline='alphabetic';context.fillStyle=box.color;
    for(const line of plan.lines)context.fillText(line.text,line.x,line.y);context.restore();
    const data=element.toDataURL('image/png');if(!data.startsWith('data:image/png;base64,'))fail('TEXT_RENDER_UNAVAILABLE','文字画布未能生成 PNG。');
    const raster={width:size.width,height:size.height,data:data.slice(22),key:key(annotation)};
    validateRaster({...annotation,raster},{required:true});element.width=1;element.height=1;return raster;
  }
  async function preparePages(input){
    if(!Array.isArray(input))fail('TEXT_DATA','缺少文字页面数据。');
    const cached=new Map();let totalText=0,totalBytes=0,totalPixels=0;
    // Inspect budgets before copying raster strings or creating any canvas.
    for(const page of input){if(!Array.isArray(page))fail('TEXT_DATA','文字页面数据无效。');for(const annotation of page)if(TOOLS.includes(annotation?.tool)){
      if(typeof annotation.text!=='string')fail('TEXT_DATA','文字内容无效。');totalText+=annotation.text.length;
      if(totalText>MAX_TOTAL_TEXT)fail('TEXT_DATA_LIMIT','整份 PDF 的新增文字超过 100 万个字符，原更改仍保留。');
      descriptor(annotation);const size=dimensions(annotation);totalPixels+=size.width*size.height;
      if(totalPixels>MAX_TOTAL_PIXELS)fail('TEXT_RASTER_LIMIT','整份 PDF 的文字外观超过当前像素范围，原更改仍保留。');
      let current=null;try{current=validateRaster(annotation);}catch(error){if(!(error instanceof TextAppearanceError))throw error;}
      if(current){totalBytes+=current.bytes.length;if(totalBytes>MAX_TOTAL_BYTES)fail('TEXT_RASTER_LIMIT','整份 PDF 的文字外观超过当前大小范围，原更改仍保留。');cached.set(annotation,annotation.raster);}
    }}
    const pending=[],output=input.map(page=>page.map(annotation=>{
      if(!TOOLS.includes(annotation?.tool))return JSON.parse(JSON.stringify(annotation));
      const {raster,...logical}=annotation,result=JSON.parse(JSON.stringify(logical));
      if(cached.has(annotation))result.raster={...cached.get(annotation)};else pending.push(result);return result;
    }));
    if(pending.length){if(typeof document!=='object')fail('TEXT_RENDER_UNAVAILABLE','当前环境无法生成文字外观。');if(document.fonts?.ready)await document.fonts.ready;}
    for(const annotation of pending){annotation.raster=rasterize(annotation);const png=validateRaster(annotation,{required:true});totalBytes+=png.bytes.length;if(totalBytes>MAX_TOTAL_BYTES)fail('TEXT_RASTER_LIMIT','整份 PDF 的文字外观超过当前大小范围，原更改仍保留。');}
    return output;
  }
  return Object.freeze({layout,key,descriptor,dimensions,validateRaster,decodeRaster,preparePages,TextAppearanceError,
    limits:Object.freeze({text:MAX_TEXT,totalText:MAX_TOTAL_TEXT,pixels:MAX_PIXELS,edge:MAX_EDGE,bytes:MAX_BYTES,totalBytes:MAX_TOTAL_BYTES,totalPixels:MAX_TOTAL_PIXELS})});
});
