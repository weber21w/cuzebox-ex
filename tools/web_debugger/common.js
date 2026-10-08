const $=id=>document.getElementById(id);const hex=(v,n=4)=>'0x'+Number(v??0).toString(16).toUpperCase().padStart(n,'0');
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function api(command){const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})});return await r.json()}
function setConn(ok,text){const d=$('dot'),c=$('connection');if(d)d.className='dot'+(ok?' ok':'');if(c)c.textContent=text}
/* Connection indicator: only a failure to reach CUzeBox counts as
   "disconnected". A command that CUzeBox answered with ok:0 (unsupported
   feature, bad argument, nothing to report) still proves the link is up, so it
   must not flip the indicator; the error is reported to the caller/console. */
const CUZEBOX_LINK_ERRORS=['CUzeBox API unavailable','event API unavailable'];
function apiLinkDown(r){return !r||(!r.ok&&CUZEBOX_LINK_ERRORS.includes(r.error))}
let cuzeboxLinkFailures=0,cuzeboxLinkEverUp=false,cuzeboxApiPort=null;
function noteLink(up,text){if(up){cuzeboxLinkFailures=0;cuzeboxLinkEverUp=true;setConn(true,'API connected');const c=$('connection');if(c&&cuzeboxApiPort)c.title=`CUzeBox API port ${cuzeboxApiPort}`;return}
 /* Debounce: once connected, a single slow or dropped poll is not a disconnect. */
 if(++cuzeboxLinkFailures>=2||!cuzeboxLinkEverUp)setConn(false,text||'API disconnected')}
async function command(cmd,quiet=true){try{const r=await api(cmd);noteLink(!apiLinkDown(r),(r&&r.error)||'API disconnected');if(!quiet&&window.addConsoleLog){addConsoleLog('> '+cmd,'cmd');addConsoleLog(JSON.stringify(r,null,2),r.ok?'resp':'err')}return r}catch(e){noteLink(false,'Bridge unavailable');if(!quiet&&window.addConsoleLog)addConsoleLog(String(e),'err');return {ok:0,error:String(e)}}}
async function bridgeStatus(){try{const s=await (await fetch('/api/status',{cache:'no-store'})).json();const up=!!(s.connected??s.ok);if(up&&s.api_port)cuzeboxApiPort=s.api_port;noteLink(up,s.error||'API disconnected');return s}catch(e){noteLink(false,'Bridge unavailable');return {ok:0,error:String(e)}}}
/* Heartbeat so pages without their own polling (e.g. the index) stay accurate. */
document.addEventListener('DOMContentLoaded',()=>{if(!window.cuzeboxHeartbeat)window.cuzeboxHeartbeat=setInterval(bridgeStatus,3000)});
function activateNav(){const p=location.pathname.replace(/\/+$/,'')||'/';document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('active',a.getAttribute('href')===p))}
document.addEventListener('DOMContentLoaded',activateNav);

let cuzeboxEvents=null;
function startEventStream(){if(!window.EventSource||cuzeboxEvents)return;try{cuzeboxEvents=new EventSource('/api/events');cuzeboxEvents.onmessage=e=>{try{const d=JSON.parse(e.data);window.dispatchEvent(new CustomEvent('cuzebox-event',{detail:d}))}catch(_){}};cuzeboxEvents.onerror=()=>{};}catch(_){}}
document.addEventListener('DOMContentLoaded',startEventStream);

function sdProtoClamp(v,a,b){v=Number(v)||0;return Math.max(a,Math.min(b,v))}
function sdProtoByte(label,value,cls=''){return `<div class="sd-proto-byte ${cls}"><small>${esc(label)}</small><b>${value}</b></div>`}
function sdProtoSeg(label,value,cls='',pct=0){return `<div class="sd-proto-seg ${cls}"><small>${esc(label)}</small><b>${esc(value)}</b>${pct>0?`<span class="sd-proto-fill" style="width:${sdProtoClamp(pct,0,100)}%"></span>`:''}</div>`}
function renderSdProtocolTimeline(root,st,memo={}){
 const el=typeof root==='string'?$(root):root;if(!el||!st||!st.ok)return memo;
 const p=st.protocol||{},c=p.command||{},r=p.response||{},d=p.data||{},sp=st.spi||{};
 if(c.active){
  const bytes=(c.bytes||[]).slice(0,6);while(bytes.length<6)bytes.push(0);
  let mask=Number(c.known_mask)||0,actualCrc=false;
  if(c.crc_expected_valid&&c.phase==='RESPONSE WAIT'&&!sp.active){bytes[5]=Number(sp.tx)||0;mask|=32;actualCrc=true}
  memo.command={cmd:Number(c.cmd)||0,app:!!c.app,name:c.name||('CMD'+c.cmd),arg:Number(c.arg)>>>0,bytes,mask,crcExpected:Number(c.crc_expected)||0,crcExpectedValid:!!c.crc_expected_valid,actualCrc,cycle:Number(st.cpu_cycle)||0};
 }
 const cm=memo.command||null,cmdLabels=['CMD','ARG[31:24]','ARG[23:16]','ARG[15:8]','ARG[7:0]','CRC7'];
 let cmdHtml='';
 if(cm){
  for(let i=0;i<6;i++){
   const known=!!(cm.mask&(1<<i));let value=known?hex(cm.bytes[i],2):'--',cls=known?'done':'';
   if(i===5&&!known&&cm.crcExpectedValid){value='~'+hex(cm.crcExpected,2).slice(2);cls='expected'}
   if(c.active&&Number(c.next_index)===i)cls+=(cls?' ':'')+'next';
   cmdHtml+=sdProtoByte(cmdLabels[i],value,cls);
  }
 }else cmdHtml=sdProtoSeg('COMMAND','waiting for 0x40..0x7F','active');
 const waitTotal=Number(r.wait_total)||0,waitDone=Number(r.wait_done)||0,extraDone=Number(r.extra_done)||0;
 let respHtml=sdProtoSeg('STUFF',r.phase==='WAIT_R1'?`${waitDone}/${waitTotal}`:(r.phase==='EXTRA'?'done':'idle'),r.phase==='WAIT_R1'?'active':(r.phase==='EXTRA'?'done':''),waitTotal?100*waitDone/waitTotal:0);
 respHtml+=sdProtoSeg('R1',hex(r.r1,2),r.phase==='WAIT_R1'?'active':(r.phase==='EXTRA'||d.active?'done':''));
 const extraBytes=(r.extra_bytes||[]).slice(0,4).map(v=>hex(v,2).slice(2));
 respHtml+=sdProtoSeg('EXTRA',r.phase==='EXTRA'?`${extraDone}/4 · ${extraBytes.join(' ')}`:'0/4',r.phase==='EXTRA'?'active':'',25*extraDone);
 const dir=d.direction||'NONE',phase=d.phase||'IDLE',token=Number(d.token)||0,dataDone=Number(d.data_done)||0,crcDone=Number(d.crc_done)||0,pct=(Number(d.progress_x100)||0)/100;
 const tokenPassed=['DATA','CRC','BUSY'].includes(phase),dataPassed=['CRC','BUSY'].includes(phase),crcPassed=phase==='BUSY';
 let dataHtml=sdProtoSeg('TOKEN WAIT',phase==='WAIT_TOKEN'?(Number(d.token_wait_total)?`${d.token_wait_done}/${d.token_wait_total}`:`${d.token_wait_done}`):(tokenPassed?'done':'idle'),phase==='WAIT_TOKEN'?'active':(tokenPassed?'done':''),Number(d.token_wait_total)?100*Number(d.token_wait_done)/Math.max(1,Number(d.token_wait_total)):0);
 dataHtml+=sdProtoSeg('TOKEN',token?hex(token,2):'--',phase==='WAIT_TOKEN'?'active':(tokenPassed?'done':''));
 dataHtml+=sdProtoSeg('DATA',d.active?`${dataDone}/512`:'idle','data '+(phase==='DATA'?'active':(dataPassed?'done':'')),pct);
 const crcCalc=Number(d.crc_calculated)||0,crcRecv=Number(d.crc_received)||0;
 const crcText=dir==='READ'?`${hex((crcCalc>>8)&255,2).slice(2)} ${hex(crcCalc&255,2).slice(2)}`:(dir==='WRITE'?`${hex((crcRecv>>8)&255,2).slice(2)} ${hex(crcRecv&255,2).slice(2)}`:'idle');
 dataHtml+=sdProtoSeg('CRC',d.active?`${crcDone}/2 · ${crcText}`:'idle',phase==='CRC'?'active':(crcPassed?'done':''),50*crcDone);
 const wrt=Number(d.write_response_token);if((wrt===5||wrt===11)&&phase==='BUSY')dataHtml+=sdProtoSeg('RESP',hex(wrt,2),'done');
 dataHtml+=sdProtoSeg('BUSY',phase==='BUSY'?`${Number(d.busy_remaining_cycles||0).toLocaleString()} cy`:'idle',phase==='BUSY'?'active':'');
 const cmdDetail=cm?`${cm.app?'A':''}CMD${cm.cmd} · ${cm.name} · arg ${hex(cm.arg,8)}${cm.actualCrc&&cm.crcExpectedValid?` · CRC ${hex(cm.bytes[5],2)} ${cm.bytes[5]===cm.crcExpected?'OK':`expected ${hex(cm.crcExpected,2)}`}`:''}`:'No command packet captured in this browser session.';
 const dataDetail=d.active?`${dir} sector ${d.sector} · ${phase}${phase==='DATA'?` · ${dataDone}/512 (${pct.toFixed(1)}%)`:''}`:'No block transfer active.';
 const spiPct=sp.active?100*Number(sp.elapsed_cycles||0)/Math.max(1,Number(sp.duration_cycles||1)):0;
 el.innerHTML=`<div class="sd-proto"><div class="sd-proto-lane"><div class="sd-proto-label">COMMAND</div><div class="sd-proto-track">${cmdHtml}</div></div><div class="sd-proto-lane"><div class="sd-proto-label">RESPONSE</div><div class="sd-proto-track">${respHtml}</div></div><div class="sd-proto-lane"><div class="sd-proto-label">BLOCK</div><div class="sd-proto-track">${dataHtml}</div></div><div class="sd-proto-lane"><div class="sd-proto-label">SPI BUS</div><div class="sd-proto-detail sd-proto-bus"><span>MOSI <b>${hex(sp.tx,2)}</b></span><span>MISO <b>${hex(sp.rx,2)}</b></span><span>${sp.active?`byte in flight · ${spiPct.toFixed(1)}% · ${Number(sp.remaining_cycles||0)} cy left`:`idle · next card MISO ${hex(r.next_miso,2)}`}</span></div></div><div class="sd-proto-detail">${esc(cmdDetail)}<br>${esc(dataDetail)}</div></div>`;
 return memo;
}


function sdTraceDelta(cycle,start){return ((Number(cycle)>>>0)-(Number(start)>>>0))>>>0}
function sdTracePacketBytes(e){const cmd=0x40|(Number(e.cmd)||0),arg=Number(e.arg)>>>0,crc=Number(e.command_crc)||0;return [cmd,(arg>>>24)&255,(arg>>>16)&255,(arg>>>8)&255,arg&255,crc]}
function sdTraceEventPhase(e){
 switch(e.kind){case'COMMAND':return'R1';case'ABORT':return'ABORT';case'LATENCY':return'LATENCY';case'INIT_FAIL':return'INIT FAIL';case'DATA_TOKEN':return'DATA TOKEN';case'DATA_END':return'DATA END';case'CRC_END':return'CRC END';case'DATA_CRC':return'CRC ERROR';case'BLOCK':return'NEXT BLOCK';case'BUSY_END':return'BUSY END';default:return e.kind||'EVENT'}
}
function sdTraceTransactions(events){
 const map=new Map(),loose=[];
 for(const e0 of events||[]){const e={...e0},id=Number(e.transaction_id)>>>0;if(!id){loose.push(e);continue}let t=map.get(id);if(!t){t={id,events:[],command:null,firstCycle:Number(e.start_cycle||e.cycle)>>>0,lastCycle:Number(e.cycle)>>>0};map.set(id,t)}t.events.push(e);if(e.kind==='COMMAND'||e.kind==='ABORT')t.command=e;t.firstCycle=Number(t.command?.start_cycle||t.firstCycle||e.start_cycle||e.cycle)>>>0;t.lastCycle=Number(e.cycle)>>>0}
 const out=[...map.values()];for(const e of loose)out.push({id:0,events:[e],command:null,firstCycle:Number(e.start_cycle||e.cycle)>>>0,lastCycle:Number(e.cycle)>>>0,loose:true});out.sort((a,b)=>{const as=Math.max(...a.events.map(e=>Number(e.seq)===4294967295?-1:Number(e.seq)||0)),bs=Math.max(...b.events.map(e=>Number(e.seq)===4294967295?-1:Number(e.seq)||0));return as-bs||((a.lastCycle>>>0)-(b.lastCycle>>>0))});return out
}
function sdTraceDetail(e){
 const k=e.kind||'';
 if(k==='COMMAND'){const crc=Number(e.command_crc)||0,exp=Number(e.command_crc_expected)||0;return `R1 ${hex(e.r1,2)} · ${e.state_before_name||e.state_before} → ${e.state_after_name||e.state_after} · latency ${Number(e.latency_cycles)||0} cy · CRC7 ${hex(crc,2)}${exp?` / expected ${hex(exp,2)}${crc===exp?' OK':' MISMATCH'}`:''}`}
 if(k==='DATA_TOKEN')return `sector ${e.sector} · token ${hex(e.value,2)}`;
 if(k==='DATA_END')return `sector ${e.sector} · 512-byte payload complete · CRC16 ${hex(e.crc_calculated,4)}`;
 if(k==='CRC_END'){const calc=Number(e.crc_calculated)||0,recv=Number(e.crc_received)||0;return `sector ${e.sector} · CRC16 calc ${hex(calc,4)}${recv?` · received ${hex(recv,4)}${calc===recv?' OK':' MISMATCH'}`:''}${Number(e.value)===5?' · data response ACCEPTED':Number(e.value)===11?' · data response CRC REJECT':''}`}
 if(k==='BLOCK')return `advanced to sector ${e.sector}`;
 if(k==='BUSY_END')return `write busy released · sector ${e.sector}`;
 if(k==='DATA_CRC')return `write CRC rejected · sector ${e.sector}`;
 if(k==='LATENCY')return `response exceeded threshold · ${e.latency_cycles||0} cycles`;
 if(k==='INIT_FAIL')return `initialization cadence/state failure`;
 if(k==='ABORT')return `command aborted before R1`;
 return `${e.packet_state_name||''}${e.sector!==undefined?` · sector ${e.sector}`:''}`
}
function sdTraceSyntheticRows(t){
 const c=t.command;if(!c||c.kind!=='COMMAND')return [];
 const rows=[],bytes=sdTracePacketBytes(c),labels=['CMD','ARG[31:24]','ARG[23:16]','ARG[15:8]','ARG[7:0]','CRC7'],cycles=c.command_byte_cycle||[],pcs=c.command_byte_pc||[],rr=c.command_byte_row||[],bc=c.command_byte_beam_cycle||[];
 for(let i=0;i<6;i++){const cy=Number(cycles[i]??(i===0?c.start_cycle:(i===5?c.response_start_cycle:0)))>>>0;if(!cy)continue;let detail=`${labels[i]} ${hex(bytes[i],2)}`;if(i===0)detail+=` · ${c.app?'A':''}CMD${c.cmd} · ${c.command_name||''}`;if(i===5)detail+=` / expected ${hex(c.command_crc_expected,2)}${Number(c.command_crc)===Number(c.command_crc_expected)?' OK':' MISMATCH'}`;rows.push({kind:'CMD_BYTE',phase:labels[i],cycle:cy,pc:Number(pcs[i]??(i===0?c.start_pc:c.response_start_pc))||0,row:Number(rr[i]??(i===0?c.start_row:c.response_start_row))||0,beam_cycle:Number(bc[i]??(i===0?c.start_beam_cycle:c.response_start_beam_cycle))||0,detail,_source:c})}
 rows.push({...c,phase:'R1'});return rows
}
function renderSdTraceTransactions(root,events,opts={}){
 const el=typeof root==='string'?$(root):root;if(!el)return;const previousOpen=new Set([...el.querySelectorAll('details[open][data-tx]')].map(x=>x.dataset.tx));el.innerHTML='';let txs=sdTraceTransactions(events);const limit=Number(opts.limit)||24;if(txs.length>limit)txs=txs.slice(-limit);txs=txs.reverse();
 txs.forEach((t,idx)=>{const c=t.command||t.events.find(e=>e.cmd!==undefined)||{},first=Number(c.start_cycle||t.firstCycle)>>>0,last=Number(t.lastCycle)>>>0,dur=sdTraceDelta(last,first),hasBreak=t.events.some(e=>e.break),sectors=[...new Set(t.events.map(e=>Number(e.sector)).filter(Number.isFinite))],details=document.createElement('details');details.className='sd-tx';details.dataset.tx=String(t.id||('loose-'+idx));details.open=previousOpen.has(details.dataset.tx)||(!previousOpen.size&&idx===0);const summary=document.createElement('summary');summary.innerHTML=`<span class="sd-tx-title">${t.id?`TX ${t.id}`:'EVENT'} · ${esc(c.command_name||((c.cmd!==undefined)?'CMD'+c.cmd:(t.events[0]?.kind||'SD')))}${c.app?' (A)':''}</span><span>${sectors.length?`sector ${sectors[0]}${sectors.length>1?`…${sectors[sectors.length-1]}`:''} · `:''}${dur.toLocaleString()} cy · ${t.events.length} event${t.events.length===1?'':'s'}${hasBreak?' · <span class="badge">BREAK</span>':''}</span>`;details.appendChild(summary);
  const body=document.createElement('div');body.className='sd-tx-body';if(t.command){const b=sdTracePacketBytes(t.command).map(v=>hex(v,2)).join(' ');body.innerHTML=`<div class="sd-tx-packet mono"><b>${esc(t.command.command_name||('CMD'+t.command.cmd))}</b> · arg ${hex(t.command.arg,8)} · packet ${b} · R1 ${hex(t.command.r1,2)}</div>`}const table=document.createElement('table');table.className='mono sd-tx-table';table.innerHTML='<thead><tr><th>Phase</th><th>Cycle</th><th>+Δ</th><th>Raster</th><th>PC</th><th>Detail</th></tr></thead><tbody></tbody>';const tbody=table.querySelector('tbody');let rows=[];const synth=sdTraceSyntheticRows(t);if(synth.length)rows.push(...synth);for(const e of t.events){if(e.kind==='COMMAND')continue;rows.push({...e,phase:sdTraceEventPhase(e)})}const phaseOrder={CMD_BYTE:0,COMMAND:1,LATENCY:2,DATA_TOKEN:3,DATA_END:4,CRC_END:5,BLOCK:6,DATA_CRC:7,BUSY_END:8,ABORT:9,INIT_FAIL:10};rows.sort((a,b)=>sdTraceDelta(a.cycle,first)-sdTraceDelta(b.cycle,first)||(phaseOrder[a.kind]??50)-(phaseOrder[b.kind]??50)||(Number(a.seq)||0)-(Number(b.seq)||0));for(const r of rows){const tr=document.createElement('tr');const raster=(r.row!==undefined&&r.beam_cycle!==undefined)?`${r.row}:${r.beam_cycle}`:'—';tr.innerHTML=`<td>${esc(r.phase||sdTraceEventPhase(r))}${r.break?' <span class="badge">BREAK</span>':''}</td><td>${Number(r.cycle)>>>0}</td><td>+${sdTraceDelta(r.cycle,first)}</td><td class="addr">${esc(raster)}</td><td class="addr">${r.pc!==undefined?hex(r.pc,4):'—'}</td><td>${esc(r.detail||sdTraceDetail(r))}</td>`;if(opts.onEvent&&r.pc!==undefined){tr.classList.add('clickable');tr.title='Select AVR PC and beam position';tr.onclick=()=>opts.onEvent(r)}tbody.appendChild(tr)}body.appendChild(table);details.appendChild(body);el.appendChild(details)});
 if(!txs.length)el.innerHTML='<div class="muted">No SD protocol history captured.</div>'
}


function sdFsOpRange(o){
 const n=Number(o.sectors_started)||0,first=Number(o.first_sector)||0,last=Number(o.last_sector)||first;
 if(o.file_offset_valid){const a=Number(o.file_offset_start)||0,b=Number(o.file_offset_end)||a;return `+${hex(a,6)}..+${hex(Math.max(a,b)-1,6)} · ${(Math.max(0,b-a)).toLocaleString()} B`}
 return n>1?`sector ${first}…${last} · ${n} sectors`:`sector ${first}`
}
function sdFsOpCluster(o){
 const a=Number(o.first_cluster)||0,b=Number(o.last_cluster)||a,t=Number(o.cluster_transitions)||0,r=Number(o.physical_runs)||0;if(!a)return '—';let x=a===b?`cluster ${a}`:`cluster ${a} → ${b}`;if(t)x+=` · ${t} chain transition${t===1?'':'s'}`;if(r>1)x+=` · ${r} physical runs`;const nx=Number(o.next_cluster);if(Number.isFinite(nx)&&nx!==0&&nx!==0xffff)x+=` · next ${nx}`;else if(nx>=0xfff8||nx===0xffff)x+=' · EOC';return x
}
function sdFsOpStatus(o){const x=[];if(o.break)x.push('BREAK');if(o.fault)x.push('FAULT');if(o.crc_error)x.push('CRC');if(o.abort)x.push('ABORT');if(o.r1_error)x.push(`R1 ${hex(o.r1,2)}`);if(o.incomplete)x.push('INCOMPLETE');return x}
function renderSdFsOperations(root,data,opts={}){
 const el=typeof root==='string'?$(root):root;if(!el)return;if(!data||!data.ok){el.innerHTML='<div class="muted">Filesystem operation history unavailable.</div>';return}if(!data.built){el.innerHTML='<div class="muted">Filesystem operation history requires FLAG_SD_TRACE and debugger support.</div>';return}
 let ops=[...(data.operations||[])],access=String(opts.access||'ALL').toUpperCase(),kind=String(opts.kind||'ALL').toUpperCase(),needle=String(opts.path||'').toUpperCase();ops=ops.filter(o=>(access==='ALL'||String(o.access).toUpperCase()===access)&&(kind==='ALL'||(kind==='FILE'?o.role_name==='FILE':kind==='META'?o.role_name!=='FILE':String(o.activity).toUpperCase().includes(kind)))&&(!needle||String(o.source||'').toUpperCase().includes(needle)));
 const flow=ops.slice(-14).map(o=>`<span class="sd-fsop-chip ${o.incomplete?'warn':''}"><b>${esc(o.activity)}</b><small>${esc(o.source||o.role_name||'VFAT')}</small></span>`).join('<span class="sd-fsop-arrow">→</span>');
 let rows='';for(const o of [...ops].reverse()){const flags=sdFsOpStatus(o),done=Number(o.sectors_completed)||0,started=Number(o.sectors_started)||0,pc=Number(o.start_pc)||0,cy=Number(o.start_cycle)>>>0,dur=Number(o.duration_cycles)||0;rows+=`<tr data-seq="${Number(o.first_seq)||0}" class="${opts.onOperation?'clickable':''}"><td><b>${esc(o.activity)}</b>${flags.map(f=>` <span class="badge">${esc(f)}</span>`).join('')}</td><td>${esc(o.source||o.role_name||'VFAT')}</td><td>${esc(sdFsOpRange(o))}</td><td>${esc(sdFsOpCluster(o))}</td><td>${done}/${started} · ${(Number(o.bytes_completed)||0).toLocaleString()} B</td><td>${cy}<br><span class="muted">+${dur.toLocaleString()} cy</span></td><td class="addr">${hex(pc,4)}<br><span class="muted">${Number(o.start_row)||0}:${Number(o.start_beam_cycle)||0}</span></td></tr>`}
 el.innerHTML=`<div class="sd-fsop-summary"><span>${ops.length}/${Number(data.total_operations)||0} operation${Number(data.total_operations)===1?'':'s'} shown</span><span>mapping: ${esc(data.mapping||'current_vfat')}${data.truncated?' · newest operations retained':''}</span></div><div class="sd-fsop-flow">${flow||'<span class="muted">No matching filesystem operations.</span>'}</div><div class="sd-fsop-wrap"><table class="mono sd-fsop-table"><thead><tr><th>Operation</th><th>Owner / structure</th><th>Logical range</th><th>Cluster chain</th><th>Completed</th><th>Cycle</th><th>PC / raster</th></tr></thead><tbody>${rows||'<tr><td colspan="7" class="muted">No matching filesystem operations.</td></tr>'}</tbody></table></div>`;
 if(opts.onOperation){const visible=[...ops].reverse();[...el.querySelectorAll('tbody tr[data-seq]')].forEach((tr,i)=>tr.onclick=()=>opts.onOperation(visible[i]))}
}


function sdTimingPhase(label,cycles,cls=''){const n=Number(cycles)||0;if(!n)return '';return `<span class="sd-time-phase ${cls}" style="--sd-time:${Math.max(1,n)}" title="${esc(label)}: ${n.toLocaleString()} cycles"><b>${esc(label)}</b><small>${n.toLocaleString()} cy</small></span>`}
function sdTimingStatus(s){const a=[];if(!s.complete)a.push('INCOMPLETE');if(s.first_block&&!s.command_crc_ok)a.push('CMD CRC');if(Number(s.flags)&2)a.push('CRC');if(Number(s.flags)&8)a.push('LATENCY');if(Number(s.flags)&16)a.push('FAULT');if(Number(s.flags)&32)a.push('ABORT');if(s.busy_early)a.push('BUSY EARLY');return a}
function renderSdTimingAnalysis(root,data,opts={}){
 const el=typeof root==='string'?$(root):root;if(!el)return;if(!data||!data.ok){el.innerHTML='<div class="muted">SD timing analysis unavailable.</div>';return}if(!data.built){el.innerHTML='<div class="muted">SD timing analysis requires the SD protocol trace build.</div>';return}
 const sum=data.summary||{},m=data.model||{};let rows='';for(const s of [...(data.samples||[])].reverse()){const warn=sdTimingStatus(s),ph=[];if(s.first_block)ph.push(sdTimingPhase('CMD',s.command_span_cycles,'cmd'),sdTimingPhase('R1 wait',s.response_wait_cycles,'response'));ph.push(sdTimingPhase('token',s.token_wait_cycles,'token'),sdTimingPhase('512 B',s.data_cycles,'data'),sdTimingPhase('CRC',s.crc_cycles,'crc'),sdTimingPhase('busy',s.busy_cycles,'busy'));const title=`TX ${Number(s.transaction_id)||0}${s.first_block?'':' block'} · ${esc(s.command_name||('CMD'+s.cmd))} · sector ${Number(s.sector)||0}`;rows+=`<div class="sd-time-row ${warn.length?'warn':''}" data-seq="${Number(s.first_seq)||0}"><div class="sd-time-head"><span><b>${title}</b> · ${esc(s.direction||'NONE')} · total ${(Number(s.total_cycles)||0).toLocaleString()} cy${warn.map(x=>` <span class="badge">${esc(x)}</span>`).join('')}</span><span class="muted">PC ${hex(s.start_pc,4)} · ${Number(s.start_row)||0}:${Number(s.start_beam_cycle)||0}</span></div><div class="sd-time-track">${ph.join('')||'<span class="muted">response only</span>'}</div><div class="sd-time-detail mono">R1 ${hex(s.r1,2)} · command gaps ${Number(s.command_gap_min_cycles)||0}/${Number(s.command_gap_avg_cycles)||0}/${Number(s.command_gap_max_cycles)||0} min/avg/max cy${s.direction==='WRITE'?` · busy floor ${(Number(s.expected_busy_min_cycles)||0).toLocaleString()} cy`:''}</div></div>`}
 el.innerHTML=`<div class="sd-time-summary mono"><span>${Number(sum.samples)||0} sample(s) · ${Number(sum.errors)||0} warning(s) · ${Number(sum.incomplete)||0} incomplete</span><span>R1 avg/max ${(Number(sum.response_avg_cycles)||0).toLocaleString()}/${(Number(sum.response_max_cycles)||0).toLocaleString()} cy · token avg/max ${(Number(sum.token_avg_cycles)||0).toLocaleString()}/${(Number(sum.token_max_cycles)||0).toLocaleString()} cy · busy avg/max ${(Number(sum.busy_avg_cycles)||0).toLocaleString()}/${(Number(sum.busy_max_cycles)||0).toLocaleString()} cy</span><span>model: R1 ${Number(m.expected_response_slots)||0} byte slot(s) · read token ${Number(m.expected_read_token_slots)||0} slot(s) · busy ≥ ${(Number(m.write_busy_min_cycles)||0).toLocaleString()} cy</span></div><div class="sd-time-list">${rows||'<div class="muted">Arm SD history and perform card I/O to collect timing samples.</div>'}</div>`;
 if(opts.onSample){const visible=[...(data.samples||[])].reverse();[...el.querySelectorAll('.sd-time-row[data-seq]')].forEach((r,i)=>{r.classList.add('clickable');r.onclick=()=>opts.onSample(visible[i])})}
}


let sdReplayBuilt=false,sdReplayStatusData=null;
function sdReplayModeBadge(s){const m=String(s?.mode_name||'OFF');return m==='RECORD'?'<span class="badge">RECORDING</span>':m==='REPLAY'?'<span class="badge">REPLAY</span>':m==='ERROR'?'<span class="badge">MISMATCH</span>':m==='COMPLETE'?'<span class="badge">COMPLETE</span>':'off'}
function renderSdReplayEvents(data){const el=$('sdReplayEvents');if(!el)return;const ev=data?.events||[];if(!ev.length){el.innerHTML='<div class="muted">No recorded SD interaction events.</div>';return}let rows='';for(const e of [...ev].reverse()){let when='',io='';if(e.kind_name==='BYTE_START'){when=`start +${Number(e.recv_cycle_delta).toLocaleString()}`;io=`MISO ${hex(e.miso,2)} sampled`}else if(e.kind_name==='BYTE_END'){when=`end +${Number(e.send_cycle_delta).toLocaleString()}`;io=`MOSI ${hex(e.mosi,2)} delivered`}else if(e.kind_name==='CS'){when=`+${Number(e.cycle_delta).toLocaleString()}`;io=`CS ${e.cs_enabled?'LOW / selected':'HIGH / deselected'}`}else{when=`+${Number(e.cycle_delta).toLocaleString()}`;io='SD reset'}rows+=`<tr><td>${e.seq}</td><td>${esc(e.kind_name)}</td><td>${esc(when)}</td><td>${esc(io)}</td><td>${esc(e.state_name||e.state)} / ${esc(e.packet_state_name||e.packet_state)}</td><td>${Number(e.sector)}:${Number(e.packet_pos)}</td><td>${hex(e.r1,2)}</td></tr>`}el.innerHTML=`<div class="sd-payload-wrap"><table class="mono"><thead><tr><th>#</th><th>Event</th><th>Recorded timing</th><th>Card-visible I/O</th><th>Post state</th><th>Sector:byte</th><th>R1</th></tr></thead><tbody>${rows}</tbody></table></div>`}
async function refreshSdReplay(){const box=$('sdReplayStatus');if(!box)return null;const s=await command('SD_REPLAY STATUS');if(!s.ok){box.textContent=s.error||'SD replay unavailable';return s}sdReplayBuilt=!!s.built;sdReplayStatusData=s;const progress=Number(s.count)?`${Number(s.cursor)||0}/${Number(s.count)}`:`0/${Number(s.capacity)||0}`;let text=`${sdReplayModeBadge(s)} · ${progress} events · exact relative timing ±${Number(s.tolerance_cycles)||0} cy`;if(s.error_name&&s.error_name!=='NONE')text+=` · ${s.error_name} @ event ${s.mismatch_index} expected ${s.mismatch_expected} actual ${s.mismatch_actual}`;if(s.overflow)text+=' · capture overflow';box.innerHTML=text;const ids=['sdReplayRecord','sdReplayReplay','sdReplayStop','sdReplayClear','sdReplaySave','sdReplayLoad'];for(const id of ids){const e=$(id);if(e)e.disabled=!sdReplayBuilt}$('sdReplayReplay')&&($('sdReplayReplay').disabled=!sdReplayBuilt||!Number(s.count)||s.recording||s.replaying);if($('sdReplayBreakEnd'))$('sdReplayBreakEnd').checked=!!s.break_on_end;const n=Math.min(64,Number(s.count)||0),start=Math.max(0,(Number(s.count)||0)-n),r=await command(`SD_REPLAY READ ${start} ${n}`);if(r.ok)renderSdReplayEvents(r);return s}
async function startSdReplayRecording(){await command('SD_REPLAY RECORD',false);refreshSdReplay()}
async function stopSdReplay(){await command('SD_REPLAY STOP',false);refreshSdReplay()}
async function startSdReplay(){const t=Math.max(0,parseInt($('sdReplayTolerance')?.value||'0',0)||0);await command(`SD_REPLAY REPLAY ${t}`,false);refreshSdReplay()}
async function clearSdReplay(){await command('SD_REPLAY CLEAR',false);refreshSdReplay()}
async function setSdReplayBreakEnd(){await command(`SD_REPLAY BREAK_END ${$('sdReplayBreakEnd')?.checked?'ON':'OFF'}`,false);refreshSdReplay()}
async function saveSdReplay(){const p=$('sdReplayPath')?.value.trim();if(p)await command(`SD_REPLAY SAVE ${p}`,false);refreshSdReplay()}
async function loadSdReplay(){const p=$('sdReplayPath')?.value.trim();if(p)await command(`SD_REPLAY LOAD ${p}`,false);refreshSdReplay()}

function sdPayloadWindowStart(pos){let p=Math.max(0,Math.min(511,Number(pos)||0));return Math.max(0,Math.min(384,Math.floor((p-64)/16)*16))}
function sdPayloadAscii(v){v=Number(v)||0;return (v>=32&&v<127)?String.fromCharCode(v):'.'}
function sdFsFatValue(v){v=Number(v)&0xffff;if(v===0)return 'FREE';if(v===0xfff7)return 'BAD';if(v>=0xfff8)return 'EOC';if(v>=0xfff0&&v<=0xfff6)return 'RESERVED';return '→ '+v}
function sdFsShortName(a){if(!a||a.length<11)return '';if(a[0]===0)return '<end>';if(a[0]===0xe5)return '<deleted>';if((a[11]&0x0f)===0x0f)return '<LFN>';let b=String.fromCharCode(...a.slice(0,8)).replace(/\s+$/,''),e=String.fromCharCode(...a.slice(8,11)).replace(/\s+$/,'');return b+(e?'.'+e:'')}
function sdFsAttrText(v){v=Number(v)||0;if((v&0x0f)===0x0f)return 'LFN';let a=[];if(v&0x10)a.push('DIR');if(v&0x20)a.push('ARC');if(v&0x01)a.push('RO');if(v&0x02)a.push('HID');if(v&0x04)a.push('SYS');if(v&0x08)a.push('VOL');return a.join('|')||'—'}
function sdFsLe16(a,i){return (Number(a[i])||0)|((Number(a[i+1])||0)<<8)}
function sdFsLe32(a,i){return (sdFsLe16(a,i)|(sdFsLe16(a,i+2)<<16))>>>0}
function renderSdFilesystemContext(p){
 const f=p.fs||{},role=String(f.role||'UNKNOWN'),start=Number(p.start)||0,count=Number(p.count)||0,stream=p.stream||[],same=Number(p.sector)===Number(p.current_sector),cursor=Number(p.cursor_offset)||0;let meta=[],detail='';
 meta.push(`<span>VFAT <b>${esc(role)}</b></span>`);
 if(role==='BOOT'){const b=f.boot||{};meta.push(`<span>FAT16 geometry <b>${Number(b.bytes_per_sector)||0} B/sector · ${Number(b.sectors_per_cluster)||0} sectors/cluster · ${Number(b.fat_count)||0} FATs</b></span>`);detail=`<table class="sd-fs-table mono"><tbody><tr><th>Reserved</th><td>${Number(b.reserved_sectors)||0} sector(s)</td><th>Sectors/FAT</th><td>${Number(b.sectors_per_fat)||0}</td><th>Root entries</th><td>${Number(b.root_entries)||0}</td></tr><tr><th>Total sectors</th><td>${Number(b.total_sectors)||0}</td><th>Media</th><td>${hex(b.media,2)}</td><th>Signature</th><td>${hex(b.signature,4)}${Number(b.signature)===0xaa55?' ✓':''}</td></tr></tbody></table>`}
 else if(role==='FAT1'||role==='FAT2'){
  meta.push(`<span>copy <b>${Number(f.fat_copy)||0}</b></span><span>sector starts at cluster <b>${Number(f.fat_first_cluster)||0}</b></span>`);let rows='',first=Math.max(start,start&~1),last=start+count-2,center=same?cursor:start,lo=Math.max(first,(center&~1)-16),hi=Math.min(last,lo+30);if(hi-lo<30)lo=Math.max(first,hi-30);for(let off=lo;off<=hi;off+=2){let i=off-start;if(i<0||i+1>=stream.length)continue;let cl=(Number(f.fat_first_cluster)||0)+(off>>1),v=sdFsLe16(stream,i),cls=(same&&cursor>=off&&cursor<off+2)?' class="sd-fs-current"':'';rows+=`<tr${cls}><td>${cl}</td><td>${hex(v,4)}</td><td>${esc(sdFsFatValue(v))}</td></tr>`}detail=`<table class="sd-fs-table mono"><thead><tr><th>Cluster</th><th>FAT16</th><th>Meaning</th></tr></thead><tbody>${rows||'<tr><td colspan="3">No complete FAT entries in this window.</td></tr>'}</tbody></table>`
 }else if(role==='ROOT'||role==='DIRECTORY'){
  if(role==='ROOT')meta.push(`<span>root sector <b>${Number(p.sector)-Number(f.root_sector)+1}/32</b></span>`);else meta.push(`<span>directory <b>${esc(p.source||'')}</b></span><span>cluster <b>${Number(f.cluster)}</b> · sector <b>${Number(f.sector_in_cluster)}/63</b></span>`);let rows='',first=Math.ceil(start/32)*32,end=start+count;for(let off=first;off+32<=end;off+=32){let i=off-start;if(i<0||i+31>=stream.length)continue;let a=stream.slice(i,i+32),name=sdFsShortName(a),attr=Number(a[11])||0,cl=sdFsLe16(a,26),sz=sdFsLe32(a,28),slot=off>>5,idx=role==='ROOT'?((Number(p.sector)-Number(f.root_sector))*16+slot):slot,cls=(same&&cursor>=off&&cursor<off+32)?' class="sd-fs-current"':'';rows+=`<tr${cls}><td>${idx}</td><td>${esc(name)}</td><td>${esc(sdFsAttrText(attr))}</td><td>${cl}</td><td>${sz}</td></tr>`}detail=`<table class="sd-fs-table mono"><thead><tr><th>Entry</th><th>8.3 name</th><th>Attr</th><th>Start cluster</th><th>Size</th></tr></thead><tbody>${rows||'<tr><td colspan="5">No complete directory entries in this window.</td></tr>'}</tbody></table>`
 }else if(role==='FILE'){
  meta.push(`<span>file <b>${esc(p.source||'')}</b></span><span>cluster <b>${Number(f.cluster)}</b> · sector <b>${Number(f.sector_in_cluster)}/63</b></span><span>chain index <b>${Number(f.chain_index)}</b></span><span>file offset <b>${Number(p.file_offset).toLocaleString()}</b></span>`);detail=`<table class="sd-fs-table mono"><tbody><tr><th>Start cluster</th><td>${Number(f.start_cluster)}</td><th>Current FAT</th><td>${Number(f.next_cluster)>=0xfff8?'EOC':Number(f.next_cluster)}</td><th>Directory entry</th><td>sector ${Number(f.entry_sector)}, #${Number(f.entry_index)}</td></tr><tr><th>Parent cluster</th><td>${Number(f.parent_cluster)}</td><th>Attributes</th><td>${esc(sdFsAttrText(f.attr))}</td><th>Absolute sector</th><td>${Number(p.sector)}</td></tr></tbody></table>`
 }else if(role==='SPARSE'||role==='UNALLOCATED')meta.push(`<span>cluster <b>${Number(f.cluster)}</b> · sector <b>${Number(f.sector_in_cluster)}/63</b></span><span><b>${role==='SPARSE'?'in-memory sparse data':'no file/directory owner'}</b></span>`);
 return `<div class="sd-fs-context"><div class="sd-payload-meta">${meta.join('')}</div>${detail}</div>`
}

function renderSdPayloadInspector(root,p){
 const el=typeof root==='string'?$(root):root;if(!el)return;
 if(!p||!p.ok){el.innerHTML='<div class="muted">Payload snapshot unavailable.</div>';return}
 if(!p.built){el.innerHTML='<div class="muted">Payload inspector not built (ENABLE_DEBUGGER=0).</div>';return}
 const stream=p.stream||[],back=p.backing||[],start=Number(p.start)||0,count=Number(p.count)||0,cursor=Number(p.cursor_offset),last=Number(p.last_offset),sameSector=Number(p.sector)===Number(p.current_sector),compare=!!p.compare_requested&&!!p.backing_valid;
 const meta=`<div class="sd-payload-meta"><span>sector <b>${Number(p.sector)}</b></span><span>window <b>${start}..${Math.max(start,start+count-1)}</b></span><span>packet <b>${esc(p.direction||'NONE')} · ${esc(p.packet_state_name||p.packet_state)} · ${Number(p.data_done)||0}/512</b></span><span>CRC <b>${Number(p.crc_done)||0}/2</b></span><span>CRC calc <b>${hex(p.crc_calculated,4)}</b></span><span>received <b>${hex(p.crc_received,4)}</b></span><span>source <b>${esc(p.source||'VFAT')}</b></span>${p.compare_requested?`<span>mismatches <b>${p.backing_valid?Number(p.diff_count||0):'unavailable'}</b></span>`:''}</div>`;
 let rows='';for(let r=0;r<count;r+=16){const n=Math.min(16,count-r),off=start+r;let sh='',sa='',bh='',ba='';for(let i=0;i<n;i++){const a=off+i,sv=Number(stream[r+i]||0),bv=Number(back[r+i]||0);let cls='sd-payload-byte';if(sameSector&&a===cursor)cls+=' cursor';if(sameSector&&a===last)cls+=' last';if(compare&&sv!==bv)cls+=' diff';sh+=`<span class="${cls}" title="offset ${a}">${hex(sv,2).slice(2)}</span>`;sa+=sdPayloadAscii(sv);if(compare){let bc='sd-payload-byte';if(sv!==bv)bc+=' diff';bh+=`<span class="${bc}" title="offset ${a}">${hex(bv,2).slice(2)}</span>`;ba+=sdPayloadAscii(bv)}}rows+=`<tr><td class="off">${hex(off,3)}</td><td class="sd-payload-bytes">${p.stream_valid?sh:'-- stream buffer not loaded --'}</td><td class="sd-payload-ascii">${p.stream_valid?esc(sa):''}</td>${p.compare_requested?`<td class="sd-payload-bytes">${p.backing_valid?bh:'-- backing unavailable --'}</td><td class="sd-payload-ascii">${p.backing_valid?esc(ba):''}</td>`:''}</tr>`}
 el.innerHTML=meta+renderSdFilesystemContext(p)+`<div class="sd-payload-wrap"><table class="sd-payload"><thead><tr><th>Off</th><th>Card stream / working sector</th><th>ASCII</th>${p.compare_requested?'<th>VFAT backing</th><th>ASCII</th>':''}</tr></thead><tbody>${rows}</tbody></table></div>`;
}
