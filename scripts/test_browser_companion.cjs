// Public fixtures only. No Accessibility domain, existing browser profile, or user page is inspected.
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {spawn, execFileSync} = require('node:child_process');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.ZENCROP_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '..');
const testExe = path.join(root, 'build/cmake/tests/test_screenshot_editor_state_contract.exe');
const installedExtension = path.join(root, 'build/run/x64-release/extensions/browser-companion');
const browserExe = process.argv[2];
if (!browserExe) throw Error('Pass an isolated test-capable Chrome/Edge executable path. Build the existing screenshot contract first.');
const browserName = process.argv[3] || 'browser';
const browserScale = process.argv[4];
if (browserScale && (!Number.isFinite(Number(browserScale)) || Number(browserScale) < 0.25 || Number(browserScale) > 16))
  throw Error('Browser device scale must be between 0.25 and 16.');
const out = path.join(root, 'build/artifacts/tests', `browser-companion-${browserName}-${process.pid}`);
const testHost = `com.zencrop.browser_companion.test_${process.pid}`;
const keys = ['Software\\Google\\Chrome\\NativeMessagingHosts\\'+testHost,
  'Software\\Microsoft\\Edge\\NativeMessagingHosts\\'+testHost];
const ps = code => execFileSync('pwsh', ['-NoProfile', '-Command', code], {encoding: 'utf8', windowsHide: true});
const quote = text => "'" + text.replaceAll("'", "''") + "'";
let context;
let original;
let hostManifest;
let server;
const reports = [];
const idleSamples = [];

async function sampleIdle(phase) {
  const measure = JSON.parse(execFileSync('pwsh',['-NoProfile','-File',path.join(root,'scripts/measure_browser_viewport.ps1'),
    '-Title',`ZenCrop companion public fixture ${process.pid}`],{encoding:'utf8',windowsHide:true}));
  const window = Array.isArray(measure) ? measure[0] : measure;
  const snapshot = () => JSON.parse(ps(`$all=Get-CimInstance Win32_Process;$ids=[Collections.Generic.HashSet[int]]::new();$ids.Add(${window.pid})|Out-Null;do{$changed=$false;foreach($p in $all){if($ids.Contains([int]$p.ParentProcessId)-and $ids.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);$list=@();foreach($id in $ids){$p=Get-Process -Id $id -ErrorAction SilentlyContinue;if($p){$list+=@{pid=$p.Id;cpuSeconds=$p.CPU;workingSet=$p.WorkingSet64;privateBytes=$p.PrivateMemorySize64;nativeHost=$p.Path -eq ${quote(testExe)}}}};ConvertTo-Json -InputObject $list`));
  const first = snapshot();
  const started = performance.now();
  await new Promise(resolve=>setTimeout(resolve,1000));
  const second = snapshot();
  const elapsed = (performance.now()-started)/1000;
  const cpu = second.reduce((sum,p)=>sum+Math.max(0,p.cpuSeconds-(first.find(q=>q.pid===p.pid)?.cpuSeconds ?? p.cpuSeconds)),0);
  const native = second.filter(p=>p.nativeHost);
  const hostCpu = native.reduce((sum,p)=>sum+Math.max(0,p.cpuSeconds-(first.find(q=>q.pid===p.pid)?.cpuSeconds ?? p.cpuSeconds)),0);
  idleSamples.push({phase,elapsedSeconds:elapsed,browserTreeCpuSeconds:cpu,processes:second,
    hostCpuSeconds:hostCpu,
    hostWorkingSet:native.reduce((s,p)=>s+p.workingSet,0),hostPrivateBytes:native.reduce((s,p)=>s+p.privateBytes,0)});
}

async function runProbe(page, fixture, worker, mutation) {
  execFileSync('pwsh', ['-NoProfile', '-File', path.join(root,'scripts/measure_browser_viewport.ps1'),
    '-PrepareFixtureWindow',fixture.hwnd], {windowsHide:true,stdio:'ignore'});
  await page.bringToFront();
  const input = path.join(out, `probe-${reports.length}.json`);
  await fs.writeFile(input, JSON.stringify(fixture));
  const child = spawn(testExe, [], {windowsHide: true, env: {...process.env,
    ZENCROP_BROWSER_COMPANION_PROBE: input, ZENCROP_DATA_DIR: path.join(out, 'app-data')}, stdio: 'pipe'});
  let output = '';
  child.stdout.on('data', chunk => output += chunk);
  child.stderr.on('data', chunk => output += chunk);
  const completion = new Promise(resolve => child.on('exit', resolve));
  const timeout = setTimeout(() => child.kill(), fixture.leaseLoss || fixture.continuousLease ? 45000 : 12000);
  try {
    await worker.evaluate(async () => { connect(); await hello(); });
    if (mutation) {
      await worker.evaluate(() => new Promise((resolve, reject) => {
        const started = Date.now();
        const timer = setInterval(() => {
          if (active?.frames?.size && Number(active.requestId) >= 3) { clearInterval(timer); resolve(); }
          else if (Date.now() - started > 4000) { clearInterval(timer); reject(Error('No active query')); }
        }, 10);
      }));
      await new Promise(resolve => setTimeout(resolve, 100));
      await mutation();
    }
    const exit = await completion;
    await fs.writeFile(input + '.log', output);
    const result = JSON.parse(await fs.readFile(input + '.result.json', 'utf8'));
    if (exit !== 0) {
      console.error('Public fixture geometry:',await page.evaluate(()=>({
        rect:document.documentElement.getBoundingClientRect().toJSON(),
        client:[document.documentElement.clientWidth,document.documentElement.clientHeight],
        viewport:[innerWidth,innerHeight],dpr:devicePixelRatio})));
      console.error('Public fixture native viewports:',ps(`& ${quote(path.join(root,'scripts/measure_browser_viewport.ps1'))} -Title ${quote(`ZenCrop companion public fixture ${process.pid}`)}`));
      await page.screenshot({path:input+'.failure.png'});
    }
    assert.equal(exit, 0, output.split('\n').filter(line => line.startsWith('FAIL')).join('\n'));
    assert.equal(result.failures, 0);
    const clean = await worker.evaluate(() => new Promise(resolve => {
      const started = performance.now();
      const check = () => {
        if (active === null) resolve(true);
        else if (performance.now()-started >= 250) resolve(false);
        else setTimeout(check,5);
      };
      check();
    }));
    assert(clean, 'Worker ended the active session within 250 ms');
    reports.push({fixture, ...result});
    return result;
  } finally { clearTimeout(timeout); if (child.exitCode === null) child.kill(); }
}

async function geometry(page, selector) {
  const inventory = JSON.parse(execFileSync('pwsh', ['-NoProfile', '-File',
    path.join(root, 'scripts/measure_browser_viewport.ps1'), '-Title',`ZenCrop companion public fixture ${process.pid}`], {encoding: 'utf8', windowsHide: true}));
  const windows = Array.isArray(inventory) ? inventory : [inventory];
  const g = await page.evaluate(sel => {
    const e = sel === 'shadow' ? document.querySelector('#shadow').shadowRoot.querySelector('button') :
      document.querySelector(sel === 'iframe-box' ? '#same' : sel);
    const r = e.getBoundingClientRect();
    let rect = [r.left, r.top, r.right, r.bottom];
    if (sel === '#same' || sel === '#cross') {
      let matrix = new DOMMatrix(getComputedStyle(e).transform === 'none' ? undefined : getComputedStyle(e).transform);
      const w = e.offsetWidth, h = e.offsetHeight;
      const xs = [0, matrix.a*w, matrix.c*h, matrix.a*w+matrix.c*h];
      const ys = [0, matrix.b*w, matrix.d*h, matrix.b*w+matrix.d*h];
      const sx = r.width/(Math.max(...xs)-Math.min(...xs)), sy = r.height/(Math.max(...ys)-Math.min(...ys));
      const a = matrix.a*sx, b = matrix.b*sy, c = matrix.c*sx, d = matrix.d*sy;
      const tx = r.left-Math.min(...xs)*sx+a*e.clientLeft+c*e.clientTop;
      const ty = r.top-Math.min(...ys)*sy+b*e.clientLeft+d*e.clientTop;
      const points = [[25,25],[185,25],[25,85],[185,85]].map(([x,y]) => [a*x+c*y+tx,b*x+d*y+ty]);
      rect = [Math.min(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),
        Math.max(...points.map(p=>p[0])),Math.max(...points.map(p=>p[1]))];
    }
    return {rect, dpr: devicePixelRatio, width: innerWidth, height: innerHeight};
  }, selector);
  // The public fixture title identifies the HWND independently of the production scale conversion.
  const target = windows;
  assert.equal(target.length, 1, 'Unique fixture HWND');
  const viewports = target[0].viewports.filter((v,i,all)=>all.findIndex(other=>JSON.stringify(other.rect)===JSON.stringify(v.rect))===i)
    .filter(v=>Math.abs(v.rect[2]-v.rect[0]-g.width*g.dpr)<=Math.max(4,g.dpr) &&
      Math.abs(v.rect[3]-v.rect[1]-g.height*g.dpr)<=Math.max(4,g.dpr));
  assert.equal(viewports.length, 1, 'Unique physical viewport: '+JSON.stringify({page:g,native:target[0]}));
  const v = viewports[0].rect;
  const expected = g.rect.map((value,i) => i < 2 ? Math.floor(v[i%2]+value*g.dpr) : Math.ceil(v[i%2]+value*g.dpr));
  expected[0] = Math.max(v[0],expected[0]); expected[1] = Math.max(v[1],expected[1]);
  expected[2] = Math.min(v[2],expected[2]); expected[3] = Math.min(v[3],expected[3]);
  return {hwnd:target[0].hwnd, expected, point:[Math.round((expected[0]+expected[2])/2),Math.round((expected[1]+expected[3])/2)],
    browserChromePoint:[v[0]+20,target[0].physical[1]+20],
    frozen:true,tolerance:1};
}

(async () => {
  await fs.mkdir(out, {recursive:true});
  const extension = path.join(out,'extension');
  await fs.cp(installedExtension,extension,{recursive:true});
  const workerPath = path.join(extension,'worker.js');
  const workerSource = await fs.readFile(workerPath,'utf8');
  assert(workerSource.includes('const host = "com.zencrop.browser_companion";'));
  await fs.writeFile(workerPath,workerSource.replace('const host = "com.zencrop.browser_companion";',`const host = "${testHost}";`));
  hostManifest = path.join(out, 'host.json');
  await fs.writeFile(hostManifest, JSON.stringify({name:testHost,description:'Public fixture contract host',
    path:testExe,type:'stdio',allowed_origins:['chrome-extension://halonieocgkgbdogndcipmlpklljmoja/']}));
  const stateFile = path.join(out,'registry-original.json');
  original = ps(`$r=[Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser','Registry64'); $a=@(); foreach($p in @(${keys.map(quote).join(',')})) {$k=$r.OpenSubKey($p); $a+=@{key=$p;existed=$null -ne $k;value=if($k){$k.GetValue('')}else{$null}};if($k){$k.Dispose()}}; $r.Dispose();ConvertTo-Json -InputObject $a`);
  assert(JSON.parse(original).every(record=>!record.existed),'PID test registrations must be new');
  await fs.writeFile(stateFile,original);
  ps(`$r=[Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser','Registry64');foreach($p in @(${keys.map(quote).join(',')})){$k=$r.CreateSubKey($p);$k.SetValue('',${quote(hostManifest)});$k.Dispose()};$r.Dispose()`);
  server = http.createServer(async (request,response) => {
    try {
      const name = request.url.endsWith('frame.html') ? 'frame.html' : 'index.html';
      response.setHeader('Content-Type','text/html'); response.end(await fs.readFile(path.join(root,'tests/browser-companion',name)));
    } catch { response.writeHead(404);response.end(); }
  });
  await new Promise(resolve=>server.listen(0,'::',resolve));
  const profile = await fs.mkdtemp(path.join(os.tmpdir(),'zencrop-browser-contract-'));
  context = await chromium.launchPersistentContext(profile,{executablePath:browserExe,headless:false,viewport:null,
    env:{...process.env,COMSPEC:process.env.COMSPEC || 'C:\\Windows\\System32\\cmd.exe'},
    args:['--disable-extensions-except='+extension,'--load-extension='+extension,'--window-size=1200,850','--window-position=80,80',
      ...(browserScale ? ['--force-device-scale-factor='+browserScale] : [])]});
  const page = await context.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
  await page.evaluate(pid=>document.title=`ZenCrop companion public fixture ${pid}`,process.pid);
  const worker = context.serviceWorkers()[0] || await context.waitForEvent('serviceworker');
  await page.bringToFront(); await page.keyboard.press('Escape');
  await page.waitForTimeout(2000);
  await sampleIdle('connected_before_selection');
  // Native Messaging's host deliberately uses the real library and verified process identity.
  await page.evaluate(()=>document.documentElement.style.minHeight='100vh');
  const navigationFixture = await geometry(page,'#button');
  const navigationTarget = await geometry(page,'#paragraph');
  const pageGeometry = await geometry(page,'html');
  await runProbe(page,{...navigationFixture,navigationTarget:{point:navigationTarget.point,
    expected:navigationTarget.expected,page:pageGeometry.expected}},worker);
  await page.evaluate(()=>document.documentElement.style.removeProperty('min-height'));
  const overlapping = await worker.evaluate(async()=> {
    const primary = await chrome.windows.getLastFocused();
    const [tab] = await chrome.tabs.query({active:true,windowId:primary.id});
    const secondary = await chrome.windows.create({url:tab.url,state:'maximized'});
    await chrome.windows.update(primary.id,{state:'maximized'});
    return {primary,secondary:secondary.id};
  });
  try {
    await page.bringToFront(); await page.waitForTimeout(150);
    await runProbe(page,await geometry(page,'#button'),worker);
    console.log(browserName+': overlapping maximized windows select the captured foreground window');
  } finally {
    await worker.evaluate(async({primary,secondary})=> {
      await chrome.windows.remove(secondary);
      await chrome.windows.update(primary.id,{state:'normal',left:primary.left,top:primary.top,
        width:primary.width,height:primary.height});
    },overlapping);
    await page.bringToFront(); await page.waitForTimeout(150);
  }
  await runProbe(page,{...await geometry(page,'#button'),frozen:false,liveWindowMove:true},worker);
  console.log(browserName+': live window movement refreshes DOM and fallback bounds');
  await page.waitForTimeout(150);
  const childLease = await geometry(page,'#same');
  await runProbe(page,{...await geometry(page,'#button'),continuousLease:{point:childLease.point}},worker);
  console.log(browserName+': visited iframe survives 35 seconds of continuous main-document selection');
  await page.evaluate(()=>{
    document.documentElement.style.minHeight='100vh';
    document.querySelector('#same').style.transform='perspective(500px) rotateY(20deg)';
  });
  try {
    await runProbe(page,await geometry(page,'iframe-box'),worker);
    console.log(browserName+': unsupported 3D iframe remains selectable as its containing box');
  } finally { await page.evaluate(()=>{
    document.querySelector('#same').style.removeProperty('transform');
    document.documentElement.style.removeProperty('min-height');
  }); }
  for (const zoom of [1,1.25,0.8,1.5,1,0.8,0.25,0.5,2,3,5]) {
    await worker.evaluate(async zoom=> {const [tab]=await chrome.tabs.query({active:true,lastFocusedWindow:true}); await chrome.tabs.setZoom(tab.id,zoom);},zoom);
    await page.waitForTimeout(150);
    for (const selector of (zoom < 0.8 || zoom > 1.5 ? ['#button'] : ['#button','shadow','#same','#cross'])) {
      await page.keyboard.press('Escape');
      const fixture = await geometry(page,selector);
      if (fixture.point[1] >= (await page.evaluate(()=>screen.height))*1.5) continue;
      const result = await runProbe(page,fixture,worker);
      console.log(`${browserName}: zoom=${zoom}, ${selector}, end=${result.endMs.toFixed(2)}ms, stop=${result.stopMs.toFixed(2)}ms`);
    }
  }
  await worker.evaluate(async()=> {const [tab]=await chrome.tabs.query({active:true,lastFocusedWindow:true});await chrome.tabs.setZoom(tab.id,1);});
  await page.waitForTimeout(150);
  const beforeZoom = await geometry(page,'#button');
  await runProbe(page,{...beforeZoom,invalidate:true},worker,()=>worker.evaluate(async()=> {
    const [tab]=await chrome.tabs.query({active:true,lastFocusedWindow:true});await chrome.tabs.setZoom(tab.id,1.25);
  }));
  await runProbe(page,await geometry(page,'#button'),worker);
  await worker.evaluate(async()=> {
    const [tab]=await chrome.tabs.query({active:true,lastFocusedWindow:true});await chrome.tabs.setZoom(tab.id,1);
  });
  const fixture = await geometry(page,'#button');
  await runProbe(page,{...fixture,invalidate:true},worker,()=>page.evaluate(()=>document.querySelector('#button').style.width='170px'));
  await sampleIdle('after_repeated_selection');
  const shadowFixture = await geometry(page,'shadow');
  await runProbe(page,{...shadowFixture,invalidate:true},worker,()=>page.evaluate(()=>document.querySelector('#shadow').shadowRoot.querySelector('#scroll').scrollTop=10));
  await page.evaluate(()=>document.querySelector('#shadow').shadowRoot.querySelector('#scroll').scrollTop=0);
  const disconnectFixture = await geometry(page,'#button');
  await runProbe(page,{...disconnectFixture,invalidate:true},worker,async()=> {
    const target = JSON.parse(execFileSync('pwsh',['-NoProfile','-File',path.join(root,'scripts/measure_browser_viewport.ps1'),
      '-Title',`ZenCrop companion public fixture ${process.pid}`],{encoding:'utf8',windowsHide:true}));
    const browserPid = (Array.isArray(target) ? target[0] : target).pid;
    ps(`$all=Get-CimInstance Win32_Process;$ids=[Collections.Generic.HashSet[int]]::new();$ids.Add(${browserPid})|Out-Null;do{$changed=$false;foreach($p in $all){if($ids.Contains([int]$p.ParentProcessId)-and $ids.Add([int]$p.ProcessId)){$changed=$true}}}while($changed);foreach($p in $all){if($ids.Contains([int]$p.ProcessId)-and $p.ExecutablePath -eq ${quote(testExe)} -and $p.CommandLine -like '*chrome-extension://halonieocgkgbdogndcipmlpklljmoja/*'){Stop-Process -Id $p.ProcessId}}`);
  });
  await page.evaluate(()=>document.querySelector('#button').style.width='160px');
  const leaseFixture = await geometry(page,'#button');
  await runProbe(page,{...leaseFixture,invalidate:true,leaseLoss:true},worker,()=>worker.evaluate(()=>clearInterval(renewal)));
  await fs.writeFile(path.join(out,'matrix.json'),JSON.stringify({browser:browserName,version:context.browser().version(),browserScale:browserScale || 'system',reports,idleSamples},null,2));
  console.log(`PASS: ${reports.length} browser geometry/lifecycle cases; evidence: ${out}`);
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{
  await context?.close();
  server?.close();
  if (original) {
    const saved = JSON.parse(original);
    for (const record of saved) ps(`$r=[Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser','Registry64');$k=$r.OpenSubKey(${quote(record.key)},$true);if($k){if($k.GetValue('') -ne ${quote(hostManifest)}){throw 'Test registration changed; preserving external value'};${record.value !== null ? `$k.SetValue('',${quote(record.value)})` : "$k.DeleteValue('',$false)"};$empty=$k.GetValueNames().Count -eq 0 -and $k.GetSubKeyNames().Count -eq 0;$k.Dispose();if($empty -and ${record.existed?'$false':'$true'}){$r.DeleteSubKey(${quote(record.key)},$false)}};$r.Dispose()`);
  }
});
