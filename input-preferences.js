/* Pure project input preferences. These values are not Drawboard defaults. */
export const DEFAULT_INK_PREFERENCES=Object.freeze({pressureSensitivity:50,smoothing:0,response:'frame'});
export const DEFAULT_NAVIGATION_PREFERENCES=Object.freeze({wheelAction:'scroll',ctrlInverts:true,middleAction:'pan',zoomAtPointer:true});
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const range=value=>typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=100;
const inkKeys=['pressureSensitivity','smoothing','response'];
const navigationKeys=['wheelAction','ctrlInverts','middleAction','zoomAtPointer'];
export function normalizeInkPreferences(input,defaults=DEFAULT_INK_PREFERENCES){
  const value=object(input)?input:{},seed=object(defaults)?defaults:DEFAULT_INK_PREFERENCES;
  return{pressureSensitivity:range(value.pressureSensitivity)?value.pressureSensitivity:range(seed.pressureSensitivity)?seed.pressureSensitivity:50,
    smoothing:range(value.smoothing)?value.smoothing:range(seed.smoothing)?seed.smoothing:0,
    response:['frame','immediate'].includes(value.response)?value.response:['frame','immediate'].includes(seed.response)?seed.response:'frame'};
}
export function validInkPreferences(value){
  return object(value)&&Object.keys(value).every(key=>inkKeys.includes(key))&&range(value.pressureSensitivity)&&range(value.smoothing)&&['frame','immediate'].includes(value.response);
}
export function normalizeNavigationPreferences(input,defaults=DEFAULT_NAVIGATION_PREFERENCES){
  const value=object(input)?input:{},seed=object(defaults)?defaults:DEFAULT_NAVIGATION_PREFERENCES;
  return{wheelAction:['scroll','zoom'].includes(value.wheelAction)?value.wheelAction:['scroll','zoom'].includes(seed.wheelAction)?seed.wheelAction:'scroll',
    ctrlInverts:typeof value.ctrlInverts==='boolean'?value.ctrlInverts:typeof seed.ctrlInverts==='boolean'?seed.ctrlInverts:true,
    middleAction:['pan','lasso'].includes(value.middleAction)?value.middleAction:['pan','lasso'].includes(seed.middleAction)?seed.middleAction:'pan',
    zoomAtPointer:typeof value.zoomAtPointer==='boolean'?value.zoomAtPointer:typeof seed.zoomAtPointer==='boolean'?seed.zoomAtPointer:true};
}
export function validNavigationPreferences(value){
  return object(value)&&Object.keys(value).every(key=>navigationKeys.includes(key))&&['scroll','zoom'].includes(value.wheelAction)
    &&typeof value.ctrlInverts==='boolean'&&['pan','lasso'].includes(value.middleAction)&&typeof value.zoomAtPointer==='boolean';
}
