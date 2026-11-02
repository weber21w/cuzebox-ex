/* Execute the shipped controls-page script with a small DOM/API fixture.
 * No browser or npm packages required: node tools/test_screenshot_web_controls.js */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(require('path').join(__dirname,'web_debugger/controls.html'),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const presets=[['CYCLE',1440,5],['RAW',1440,1],['WIDTH256',256,1]].map(([name,x_width,y_scale])=>({name,x_width,y_scale}));
const state={ok:1,x_width:1440,y_scale:5,sampling:'NEAREST',bypass_filters:1,include_gui:0,preset:'CYCLE',x_choices:[1440,2880,256,240,192,160],sampling_choices:['NEAREST','AREA','LINEAR'],presets};
const nodes={},commands=[];
const context={
 $:id=>nodes[id]||(nodes[id]={innerHTML:''}),esc:s=>String(s).replaceAll('&','&amp;').replaceAll('"','&quot;'),
 command:async cmd=>{
  commands.push(cmd);const parts=cmd.split(' ');
  if(parts[1]==='PRESET'){const p=presets.find(p=>p.name===parts[2]);Object.assign(state,{x_width:p.x_width,y_scale:p.y_scale});}
  else if(parts[1]==='SET'){const key=parts[2].toLowerCase();state[key]=key==='sampling'?parts[3]:Number(parts[3]);}
  state.preset=presets.find(p=>p.x_width===state.x_width&&p.y_scale===state.y_scale)?.name||'CUSTOM';
  return state;
 }
};
vm.createContext(context);
vm.runInContext(script.replace('bridgeStatus();refresh();setInterval(refresh,3000);',''),context);
(async()=>{
 context.renderShot(state);
 assert(nodes.screenshotControls.innerHTML.includes('value="5" selected'));
 await context.shotSet('PRESET','WIDTH256');
 assert(state.x_width===256&&state.y_scale===1);
 await context.shotSet('Y_SCALE','3');
 assert(state.preset==='CUSTOM'&&nodes.screenshotControls.innerHTML.includes('Custom'));
 await context.shotSet('X_WIDTH','240');await context.shotSet('SAMPLING','AREA');
 assert(commands.includes('SCREENSHOT_SETTINGS SET X_WIDTH 240'));
 assert(nodes.screenshotControls.innerHTML.includes('value="AREA" selected'));
 await context.shotSet('PRESET','CYCLE');
 assert(state.x_width===1440&&state.y_scale===5&&state.sampling==='AREA');
 console.log('Screenshot web controls: PASS (preset, independent axes, custom state, sampling, command routing)');
})().catch(e=>{console.error(e);process.exitCode=1;});
