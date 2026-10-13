const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const events = () => ({addListener() {}});
let finishOldEnd;
const replies = [];
const chrome = {
  runtime:{id:'fixture',onMessage:events(),onInstalled:events(),onStartup:events(),
    connectNative:()=>({postMessage:message=>replies.push(message),onMessage:events(),onDisconnect:events()})},
  windows:{getAll:async()=>[],onCreated:events(),onRemoved:events(),onBoundsChanged:events(),onFocusChanged:events()},
  tabs:{onActivated:events(),sendMessage:()=>new Promise(resolve=>{finishOldEnd=resolve;})},
  webNavigation:{onCommitted:events()},action:{onClicked:events()},alarms:{onAlarm:events(),create(){}}
};
const context = vm.createContext({chrome,crypto:require('node:crypto').webcrypto,console,setInterval,clearInterval,Map,Promise});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../../extensions/browser-companion/worker.js'),'utf8'),context);
const detector = fs.readFileSync(path.join(__dirname,'../../src/detect/SmartDetector.cpp'),'utf8');
assert(!/WarmBrowserMsaa|TouchUiaPointThroughOverlay|WM_GETOBJECT|uiautomation\.h/.test(detector),'Browser warmup/AX path removed');
for (const [entry,next] of [['EnsureAccessibleRoot','MakeElement'],['GetRectByPoint','GetParentRect'],
  ['GetParentRect','GetChildRect'],['GetChildRect','ClearCache']]) {
  const body = detector.slice(detector.indexOf('SmartDetector::'+entry+'('),detector.indexOf('SmartDetector::'+next+'('));
  assert(body.includes('browser_companion::IsBrowserWindow'),'Browser guard at '+entry);
  if (entry === 'EnsureAccessibleRoot') assert(body.indexOf('browser_companion::IsBrowserWindow') < body.indexOf('AccessibleObjectFromWindow'));
}
async function checkPageCandidates() {
  let contentHandler;
  const root = {clientWidth:1000,clientHeight:800,parentElement:null,
    getBoundingClientRect:()=>({left:0,top:0,right:1000,bottom:800}),getRootNode:()=>document};
  const panel = {parentElement:root,getBoundingClientRect:()=>({left:10,top:10,right:400,bottom:300})};
  const leaf = {parentElement:panel,getBoundingClientRect:()=>({left:20,top:20,right:120,bottom:70})};
  const document = {documentElement:root,elementFromPoint:()=>leaf};
  const contentContext = vm.createContext({document,innerWidth:1000,innerHeight:800,devicePixelRatio:1,visualViewport:null,
    chrome:{runtime:{id:'fixture',onMessage:{addListener:listener=>{contentHandler=listener;}},sendMessage:async()=>{}}},
    MutationObserver:class {observe(){} disconnect(){}},HTMLIFrameElement:class {},HTMLFrameElement:class {},
    addEventListener(){},removeEventListener(){},setTimeout:()=>1,clearTimeout(){}});
  vm.runInContext(fs.readFileSync(path.join(__dirname,'../../extensions/browser-companion/content.js'),'utf8'),contentContext);
  const inspect = (sessionId,point=[50,50]) => new Promise(resolve=>contentHandler({type:'inspect',sessionId,point}, {id:'fixture'},resolve));
  const full = await inspect('page');
  assert.equal(full.page,true,'A root covering the visible document is a page candidate');
  root.getBoundingClientRect=()=>({left:0,top:0,right:1000,bottom:799.6});
  assert.equal((await inspect('fractional-page')).page,true,
    'Integer clientHeight rounding cannot turn a whole page into a locked ordinary region');
  root.getBoundingClientRect=()=>({left:0,top:0,right:1000,bottom:800});
  assert.equal((await inspect('page',[-1,50])).status,'outside_viewport','Outside viewport is a point miss');
  document.elementFromPoint=()=>null;
  assert.equal((await inspect('page')).status,'empty_hit','Missing element is a point miss');
  document.elementFromPoint=()=>({parentElement:null,getRootNode:()=>document,
    getBoundingClientRect:()=>({left:50,top:50,right:51,bottom:51})});
  assert.equal((await inspect('page')).status,'empty_hit','Empty candidate path never produces an invalid result message');
  document.elementFromPoint=()=>leaf;
  assert((await inspect('page')).candidates.length,'Same content session recovers after point misses');
  const edge={parentElement:root,getBoundingClientRect:()=>({left:900,top:10,right:1200,bottom:300})};
  document.elementFromPoint=()=>edge;
  const fractionalClip=await new Promise(resolve=>contentHandler({type:'inspect',sessionId:'fractional-clip',
    point:[950,50],viewportSize:[1000.28,800.28]}, {id:'fixture'},resolve));
  assert.equal(fractionalClip.candidates[0][2],1000.28,
    'High zoom clips at the precise physical viewport boundary rather than rounded innerWidth');
  document.elementFromPoint=()=>leaf;
  const iframe=vm.runInContext('new HTMLIFrameElement()',contentContext);
  Object.assign(iframe,{parentElement:root,offsetWidth:300,offsetHeight:200,
    getBoundingClientRect:()=>({left:0,top:0,right:300,bottom:200,width:300,height:200})});
  contentContext.DOMMatrix=class {constructor(){this.is2D=false;}};
  contentContext.getComputedStyle=()=>({transform:'matrix3d(...)'});
  document.elementFromPoint=()=>iframe;
  const unsupported=await inspect('unsupported-frame');
  assert(!unsupported.status && unsupported.candidates.length && !unsupported.frame,
    'A 3D iframe keeps its safe containing rectangle');
  document.elementFromPoint=()=>leaf;
  root.getBoundingClientRect=()=>({left:0,top:0,right:1000,bottom:300});
  assert.equal((await inspect('small-root')).page,false,'A small terminal root must keep ordinary region locking');
  vm.runInContext("active={sessionId:'page',frames:new Map()}",context);
  chrome.tabs.sendMessage=async()=>full;
  assert.equal((await vm.runInContext("inspectFrame(1,0,'root',[50,50],{sessionId:'page'})",context)).page,true,
    'Top document page marker survives routing');
  assert.equal((await vm.runInContext("inspectFrame(1,7,'child',[50,50],{sessionId:'page'},1)",context)).page,false,
    'An iframe root never becomes the whole browser page');
  chrome.webNavigation.getAllFrames=async()=>[{parentFrameId:0,frameId:7,documentId:'child'}];
  let childCount=2;
  chrome.tabs.sendMessage=async(tab,message,options)=> message.type==='route' ? {index:0} : options.frameId===0 ?
    {candidates:[[0,0,300,200],[0,0,1000,800]],page:true,frame:{matrix:[1,0,0,1,0,0],index:0}} :
    {candidates:Array.from({length:childCount},()=>[20,20,120,70]),page:true};
  assert.equal((await vm.runInContext("inspectFrame(1,0,'root',[50,50],{sessionId:'page'})",context)).page,true,
    'The top document page marker survives iframe traversal');
  childCount=64;
  assert.equal((await vm.runInContext("inspectFrame(1,0,'root',[50,50],{sessionId:'page'})",context)).page,false,
    'A truncated chain cannot mark a child candidate as the whole page');
}
async function checkMutationGeometry() {
  let listener, mutation;
  const sent=[], listeners=new Map(), observed=[];
  let leafRight=120, panelRight=400;
  const root={isConnected:true,clientWidth:1000,clientHeight:800,parentElement:null,
    getBoundingClientRect:()=>({left:0,top:0,right:1000,bottom:800}),getRootNode:()=>document};
  const panel={isConnected:true,parentElement:root,getBoundingClientRect:()=>({left:10,top:10,right:panelRight,bottom:300})};
  const leaf={isConnected:true,parentElement:panel,getBoundingClientRect:()=>({left:20,top:20,right:leafRight,bottom:70})};
  const replacement={...leaf};
  let hit=leaf;
  const document={documentElement:root,elementFromPoint:()=>hit};
  const content=vm.createContext({document,innerWidth:1000,innerHeight:800,devicePixelRatio:1,visualViewport:null,
    chrome:{runtime:{id:'fixture',onMessage:{addListener:f=>{listener=f;}},sendMessage:async m=>{sent.push(m);}}},
    MutationObserver:class {constructor(f){mutation=f;}observe(target){observed.push(target);}disconnect(){}},
    HTMLIFrameElement:class {},HTMLFrameElement:class {},
    addEventListener:(name,f)=>listeners.set(name,f),removeEventListener:name=>listeners.delete(name),
    setTimeout:()=>1,clearTimeout(){}});
  vm.runInContext(fs.readFileSync(path.join(__dirname,'../../extensions/browser-companion/content.js'),'utf8'),content);
  const inspect=id=>new Promise(resolve=>listener({type:'inspect',sessionId:id,frozen:true,point:[50,50]}, {id:'fixture'},resolve));
  await inspect('stable');
  mutation([{type:'attributes',attributeName:'class',target:{}}]);
  mutation([{type:'attributes',attributeName:'style',target:leaf}]);
  assert.equal(sent.length,0,'Unrelated carousel mutations and unchanged leaf styles preserve frozen geometry');
  leafRight=140; mutation([{type:'attributes',attributeName:'style',target:leaf}]);
  assert.equal(sent.length,1,'A resized candidate invalidates frozen geometry');
  assert.equal(sent[0].sessionId,'stable');
  leafRight=120; await inspect('replacement');
  hit=replacement; mutation([{type:'childList',target:panel}]);
  assert.equal(sent.length,2,'A replacement or overlay at the hit point invalidates even with the same rectangle');
  hit=leaf; await inspect('parent');
  panelRight=450; mutation([{type:'attributes',target:panel}]);
  assert.equal(sent.length,3,'A moved ancestor invalidates the wheel candidate path');
  panelRight=400; await inspect('scroll');
  listeners.get('scroll')();
  assert.equal(sent.length,4,'Scrolling remains terminal');
  await inspect('ended');
  listener({type:'end',sessionId:'ended'}, {id:'fixture'},()=>{});
  leafRight=160; mutation([{type:'attributes',target:leaf}]);
  assert.equal(sent.length,4,'Ending selection removes the active geometry references');
  leafRight=120;
  const shadow={elementFromPoint:()=>leaf,addEventListener(){},removeEventListener(){}};
  leaf.shadowRoot=shadow;
  await inspect('shadow'); await inspect('shadow'); mutation([]);
  assert.equal(observed.filter(target=>target===shadow).length,1,'A visited ShadowRoot is observed only once per session');
}

async function checkWorkerLifecycle() {
  const create = async () => {
    let now=0, next=0, enterFrame=true, childError=null, leaseReply=null;
    const timers=new Map(), sent=[], posts=[];
    const window={id:1,type:'normal',left:0,top:0,width:1000,height:900,focused:true,
      tabs:[{id:1,active:true,url:'https://example.test'}]};
    const listeners={};
    const event=name=>({addListener:f=>{listeners[name]=f;}});
    const chrome={
      runtime:{id:'fixture',onMessage:event('content'),onInstalled:events(),onStartup:events(),
        connectNative:()=>({postMessage:m=>posts.push(m),onMessage:events(),onDisconnect:events()})},
      windows:{getAll:async()=>[window],getLastFocused:async()=>window,get:async()=>window,
        onCreated:events(),onRemoved:events(),onBoundsChanged:events(),onFocusChanged:event('focus')},
      tabs:{onActivated:events(),sendMessage:async(tab,m,options)=>{
        sent.push({message:m,options});
        if(m.type==='metrics') return {width:1000,height:800,dpr:1,visualScale:1,offsetX:0,offsetY:0,
          screen:[0,0,2560,1400]};
        if(m.type==='route') return {index:0};
        if(m.type==='begin') return leaseReply ? leaseReply(options) : {renewed:true};
        if(m.type==='end') return {ended:true};
        if(options.frameId!==0) {
          if(childError==='no_script') throw new Error('No receiving end');
          if(childError) return {status:childError};
          return {candidates:[[20,20,120,70]]};
        }
        return {candidates:[[0,0,300,200],[0,0,1000,800]],page:true,
          ...(enterFrame?{frame:{index:0,matrix:[1,0,0,1,0,0]}}:{})};
      }},
      webNavigation:{getFrame:async()=>({documentId:'root'}),
        getAllFrames:async()=>[{parentFrameId:0,frameId:7,documentId:'child'}],onCommitted:events()},
      action:{onClicked:events()},alarms:{onAlarm:events(),create(){}}
    };
    const context=vm.createContext({chrome,crypto:{randomUUID:()=> 'connection'},Map,Promise,
      setInterval:(f,ms)=>{const id=++next;timers.set(id,{f,ms,due:now+ms});return id;},
      clearInterval:id=>timers.delete(id)});
    vm.runInContext(fs.readFileSync(path.join(__dirname,'../../extensions/browser-companion/worker.js'),'utf8'),context);
    const inspect=async(overrides={})=>{
      const m={type:'inspect',version:1,sessionId:'session',requestId:String(sent.length+1),
        connectionId:'connection',windowId:1,frozen:true,point:[50,50],viewport:[0,0,1000,800],...overrides};
      await vm.runInContext('query('+JSON.stringify(m)+')',context);
      return posts.findLast(p=>p.type==='result'||p.type==='unavailable');
    };
    const advance=async(target)=>{
      while(true) {
        const due=[...timers.values()].sort((a,b)=>a.due-b.due)[0];
        if(!due||due.due>target) break;
        now=due.due; due.due+=due.ms; due.f();
        await Promise.resolve(); await Promise.resolve();
      }
      now=target;
    };
    return {inspect,advance,sent,posts,timers,context,window,listeners,
      enterFrame:value=>{enterFrame=value;},childError:value=>{childError=value;},
      leaseReply:value=>{leaseReply=value;}};
  };
  const lease=await create();
  assert.equal((await lease.inspect()).type,'result');
  lease.enterFrame(false);
  for(let seconds=5;seconds<=40;seconds+=5) {
    await lease.advance(seconds*1000);
    assert.equal((await lease.inspect()).type,'result');
  }
  assert.equal(lease.sent.filter(x=>x.options.frameId===7&&x.message.lease).length,4,
    'Continuous hover renews a previously visited child frame every ten seconds');
  for(const error of ['no_script','frame_not_authorized','unsupported_frame_transform','empty_frame','outside_viewport','empty_hit']) {
    const nested=await create(); nested.childError(error);
    const result=await nested.inspect();
    assert.equal(result.type,'result','Unavailable iframe keeps its containing box: '+error);
    assert.deepEqual(JSON.parse(JSON.stringify(result.candidates)),[[0,0,300,200],[0,0,1000,800]]);
    assert(!vm.runInContext('active.frames.has(7)',nested.context),'Failed child is not leased: '+error);
    await nested.advance(10000);
    assert(!nested.sent.some(x=>x.options.frameId===7&&x.message.lease));
    nested.enterFrame(false);
    assert.equal((await nested.inspect()).type,'result','Other elements stay selectable after iframe failure');
  }
  const oldLease=await create(); await oldLease.inspect();
  const rejections=[];
  oldLease.leaseReply(()=>new Promise((resolve,reject)=>{rejections.push(reject);}));
  await oldLease.advance(10000);
  await oldLease.inspect({sessionId:'new-session'});
  for(const reject of rejections) reject(Error('Old document disconnected'));
  await Promise.resolve(); await Promise.resolve();
  assert(vm.runInContext("active.sessionId==='new-session' && !active.invalid",oldLease.context),
    'Late renewal failure from an ended session cannot invalidate its replacement');
  const invalid=await create(); invalid.childError('geometry_invalidated');
  assert.equal((await invalid.inspect()).status,'geometry_invalidated','True geometry invalidation is never hidden by iframe fallback');
  const zoom=await create(); zoom.enterFrame(false);
  await vm.runInContext(`{const send=chrome.tabs.sendMessage;chrome.tabs.sendMessage=async(...args)=>
    args[1].type==='metrics'?{width:235,height:151,dpr:7.5,visualScale:1,offsetX:0,offsetY:0}:send(...args);}`,zoom.context);
  assert.equal((await zoom.inspect({viewport:[0,0,1767,1139]})).type,'result',
    'Integer CSS viewport rounding is accepted at 500% zoom with 150% system DPI');
  assert.equal((await zoom.inspect({viewport:[0,0,1767,1150]})).status,'viewport_unavailable',
    'Larger viewport mismatches still fail at high zoom');
  const viewport=await create(); viewport.enterFrame(false);
  assert.equal((await viewport.inspect({viewports:[[0,0,1000,800],[1000,0,2000,800]]})).status,'viewport_ambiguous',
    'Equal page and DevTools viewports degrade without guessing the DOM owner');
  assert.equal((await viewport.inspect()).type,'result','A viewport ambiguity is not a terminal worker state');
  assert.equal((await viewport.inspect({point:[1100,50],viewports:[[0,0,1000,800],[1000,0,1400,800]]})).status,
    'outside_viewport','A point in DevTools is not interpreted as a page point');
  await Promise.resolve(); await Promise.resolve();
  const firstHello=viewport.posts.findLast(p=>p.type==='hello');
  assert(firstHello.windows[0].lastFocused && firstHello.windows[0].screen.length===4,
    'Window identity keeps pre-overlay focus and monitor-relative coordinates');
  viewport.window.focused=false; viewport.listeners.focus(-1);
  await vm.runInContext('hello()',viewport.context);
  assert(viewport.posts.findLast(p=>p.type==='hello').windows[0].lastFocused,
    'Losing focus to the screenshot overlay does not lose the browser window identity');
}

const promise = vm.runInContext(`active={sessionId:'old',requestId:'1',frozen:true,tabId:1,frames:new Map([[0,'old-document']])};
invalidate('geometry_invalidated')`,context);
assert(vm.runInContext("active.sessionId==='old' && active.invalid",context),'Frozen identity latched synchronously');
vm.runInContext("active={sessionId:'new',requestId:'2',frozen:true,tabId:2,frames:new Map()}",context);
finishOldEnd({ended:true});
promise.then(async()=>{
  assert(vm.runInContext("active.sessionId==='new' && !active.invalid",context),'Old asynchronous cleanup cannot replace new session');
  assert(replies.some(message=>message.type==='invalidated' && message.sessionId==='old'));
  await checkPageCandidates();
  await checkMutationGeometry();
  await checkWorkerLifecycle();
  console.log('PASS: lifecycle renewal, nested fallback, viewport identity, shadow roots and frozen geometry contracts');
}).catch(error=>{console.error(error);process.exitCode=1;});
