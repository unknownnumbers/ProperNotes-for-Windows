/* Pure Windows keyboard bindings. Importing installs no listeners and accesses
 * no storage. Physical KeyboardEvent.code is used, not keyboard-layout text. */
const catalog=[
  ['open','打开 PDF','global',['Ctrl+KeyO']],['save','保存 PDF','global',['Ctrl+KeyS']],['saveAs','另存为','global',['Ctrl+Shift+KeyS']],
  ['search','搜索 PDF 文字','global',['Ctrl+KeyF']],['closeTab','关闭当前标签','global',['Ctrl+KeyW']],
  ['nextTab','下一个标签','global',['Ctrl+Tab']],['previousTab','上一个标签','global',['Ctrl+Shift+Tab']],
  ['undo','撤销批注操作','annotation',['Ctrl+KeyZ']],['redo','重做批注操作','annotation',['Ctrl+Shift+KeyZ','Ctrl+KeyY']],
  ['selectAll','全选当前页批注','annotation',['Ctrl+KeyA']],['copyAnnotations','复制所选批注','annotation',['Ctrl+KeyC']],
  ['pasteAnnotations','粘贴批注','annotation',['Ctrl+KeyV']],['deleteSelection','删除所选批注','annotation',['Delete','Backspace']],
  ['deselect','取消手势或选择','annotation',['Escape']],['toolPen','手写笔','canvas',['KeyP']],['toolHighlighter','荧光笔','canvas',['KeyH']],
  ['toolEraser','橡皮擦','canvas',['KeyE']],['toolSelect','选择批注','canvas',['KeyV']],['toolLasso','套索选择批注','canvas',[]],['toolPan','平移工具','canvas',[]],['toolText','选择 PDF 文字','canvas',[]],
  ['toolLine','直线工具','canvas',[]],['toolArrow','箭头工具','canvas',[]],['toolRectangle','矩形工具','canvas',[]],['toolEllipse','椭圆工具','canvas',[]],['toolTextbox','文本框工具','canvas',[]],['toolNote','便笺工具','canvas',[]],
  ['navBack','返回阅读位置','canvas',['Alt+ArrowLeft']],['navForward','前进阅读位置','canvas',['Alt+ArrowRight']],
  ['previousPage','上一页／双页组','canvas',[]],['nextPage','下一页／双页组','canvas',[]],
  ['zoomIn','放大','canvas',['Ctrl+Equal','Ctrl+Shift+Equal','Ctrl+NumpadAdd']],['zoomOut','缩小','canvas',['Ctrl+Minus','Ctrl+NumpadSubtract']],
  ['fitWidth','适合宽度','canvas',[]],['fitPage','适合页面','canvas',[]],['fitHeight','适合高度','canvas',[]],
  ['fullscreen','切换全屏','canvas',['F11']],['focus','切换专注模式','canvas',[]],
];
export const SHORTCUT_COMMANDS=Object.freeze(catalog.map(([id,label,context])=>Object.freeze({id,label,context})));
export const DEFAULT_SHORTCUTS=Object.freeze(Object.fromEntries(catalog.map(([id,,,bindings])=>[id,Object.freeze(bindings.slice())])));
export const MAX_SHORTCUT_BINDINGS=3;
const ids=new Set(catalog.map(([id])=>id));
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const modifiers=['Ctrl','Alt','Shift'];
const namedCodes=new Set(['Escape','Tab','Enter','Space','Backspace','Delete','Insert','Home','End','PageUp','PageDown','ArrowLeft','ArrowRight','ArrowUp','ArrowDown',
  'Minus','Equal','BracketLeft','BracketRight','Backslash','Semicolon','Quote','Comma','Period','Slash','Backquote',
  'NumpadAdd','NumpadSubtract','NumpadMultiply','NumpadDivide','NumpadDecimal','NumpadEnter']);
function supportedCode(code){return typeof code==='string'&&(/^(?:Key[A-Z]|Digit\d|Numpad\d|F(?:[1-9]|1[0-2]))$/.test(code)||namedCodes.has(code));}
function parsed(binding){
  if(typeof binding!=='string'||!binding||binding.length>80)return null;
  const parts=binding.split('+'),code=parts.pop();if(!supportedCode(code)||parts.length>3)return null;
  if(parts.some(part=>!modifiers.includes(part))||new Set(parts).size!==parts.length)return null;
  const ordered=modifiers.filter(modifier=>parts.includes(modifier));
  if([...ordered,code].join('+')!==binding)return null;
  return{code,ctrl:parts.includes('Ctrl'),alt:parts.includes('Alt'),shift:parts.includes('Shift')};
}
function reservedReason(binding){
  const item=parsed(binding);if(!item)return'按键组合格式不受支持。';
  const{code,ctrl,alt,shift}=item;
  if(code==='Space')return'空格保留为按住临时平移；带修饰键的空格可能由输入法或系统使用。';
  if(!ctrl&&!alt&&['Tab','Enter','NumpadEnter'].includes(code))return'此按键保留给界面焦点和控件激活。';
  if(['F1','F3','F5','F6','F12'].includes(code))return'此功能键保留给浏览器帮助、查找、刷新、焦点或开发工具。';
  if(code==='F11'&&(ctrl||alt||shift))return'此修改后的功能键保留给浏览器或系统。';
  if(alt&&['F4','Tab','Escape','Home','Space','KeyD','KeyF','KeyE'].includes(code))return'此组合保留给 Windows 或浏览器操作。';
  if(ctrl&&!alt&&['KeyL','KeyN','KeyT','KeyR','KeyP','KeyU','KeyJ','KeyH','KeyD','KeyK'].includes(code))return'此组合保留给浏览器地址、标签、刷新、打印或其他内置功能。';
  if(ctrl&&shift&&!alt&&['KeyW','KeyQ','KeyI','KeyC','KeyB','KeyO','Delete'].includes(code))return'此组合保留给浏览器窗口、开发工具或浏览记录操作。';
  if(ctrl&&alt)return'Ctrl+Alt 组合可能与 AltGr／输入法冲突，本阶段不支持。';
  return'';
}
export function validateShortcuts(value){
  const errors=[],used=new Map();
  if(!object(value))return{valid:false,errors:[{action:null,binding:null,message:'快捷键设置必须为按操作分组的记录。'}]};
  for(const action of Object.keys(value))if(!ids.has(action))errors.push({action,binding:null,message:'包含本版本不支持的快捷键操作。'});
  for(const{id:action}of SHORTCUT_COMMANDS){
    if(!Object.prototype.hasOwnProperty.call(value,action))continue;
    const bindings=value[action];
    if(!Array.isArray(bindings)||bindings.length>MAX_SHORTCUT_BINDINGS){errors.push({action,binding:null,message:'每个操作最多可设置 3 个组合；空列表表示未分配。'});continue;}
    const local=new Set();
    for(const binding of bindings){
      const reason=reservedReason(binding);
      if(reason){errors.push({action,binding:typeof binding==='string'?binding:null,message:reason});continue;}
      if(local.has(binding)){errors.push({action,binding,message:'此操作重复使用了同一个组合。'});continue;}
      local.add(binding);
      if(used.has(binding))errors.push({action,binding,conflictWith:used.get(binding),message:'此组合已分配给其他操作。'});
      else used.set(binding,action);
    }
  }
  return{valid:errors.length===0,errors};
}
export function normalizeShortcuts(input,defaults=DEFAULT_SHORTCUTS){
  const source=object(input)?input:{},seed=object(defaults)?defaults:DEFAULT_SHORTCUTS,result={},used=new Set();
  // Supplied bindings take precedence over missing-field defaults. Any malformed
  // or colliding records remain write-protected by preferences.js inspection.
  const supplied=SHORTCUT_COMMANDS.filter(({id})=>Object.prototype.hasOwnProperty.call(source,id));
  const missing=SHORTCUT_COMMANDS.filter(({id})=>!Object.prototype.hasOwnProperty.call(source,id));
  for(const{id}of [...supplied,...missing]){
    const candidate=Object.prototype.hasOwnProperty.call(source,id)?source[id]:seed[id]??DEFAULT_SHORTCUTS[id];
    result[id]=[];
    if(!Array.isArray(candidate))continue;
    for(const binding of candidate.slice(0,MAX_SHORTCUT_BINDINGS))if(!reservedReason(binding)&&!used.has(binding)){result[id].push(binding);used.add(binding);}
  }
  return Object.fromEntries(SHORTCUT_COMMANDS.map(({id})=>[id,result[id]]));
}
export function bindingFromEvent(event){
  if(!event||event.isComposing||event.keyCode===229||event.metaKey||['Dead','Process','Unidentified'].includes(event.key)||!supportedCode(event.code))return null;
  try{if(event.getModifierState?.('AltGraph'))return null;}catch{return null;}
  return[...(event.ctrlKey?['Ctrl']:[]),...(event.altKey?['Alt']:[]),...(event.shiftKey?['Shift']:[]),event.code].join('+');
}
export function commandForEvent(event,bindings){
  const binding=bindingFromEvent(event);if(!binding||reservedReason(binding)||!object(bindings))return null;
  let match=null;
  for(const{id}of SHORTCUT_COMMANDS)if(Array.isArray(bindings[id])&&bindings[id].includes(binding)){if(match)return null;match=id;}
  return match;
}
export function formatBinding(binding){
  const item=parsed(binding);if(!item)return typeof binding==='string'?binding:'';
  const names={Escape:'Esc',ArrowLeft:'←',ArrowRight:'→',ArrowUp:'↑',ArrowDown:'↓',PageUp:'Page Up',PageDown:'Page Down',Minus:'−',Equal:'=',BracketLeft:'[',BracketRight:']',Backslash:'\\',Semicolon:';',Quote:"'",Comma:',',Period:'.',Slash:'/',Backquote:'`',NumpadAdd:'小键盘 +',NumpadSubtract:'小键盘 −',NumpadMultiply:'小键盘 ×',NumpadDivide:'小键盘 ÷',NumpadDecimal:'小键盘 .',NumpadEnter:'小键盘 Enter'};
  const code=item.code,key=names[code]||(/^Key[A-Z]$/.test(code)?code.slice(3):/^Digit\d$/.test(code)?code.slice(5):/^Numpad\d$/.test(code)?'小键盘 '+code.slice(6):code);
  return[...(item.ctrl?['Ctrl']:[]),...(item.alt?['Alt']:[]),...(item.shift?['Shift']:[]),key].join(' + ');
}
