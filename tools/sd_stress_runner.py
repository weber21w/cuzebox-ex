#!/usr/bin/env python3
"""Deterministic CUzeBox SD timing/conformance sweep runner.

Examples:
  python3 tools/sd_stress_runner.py --frames 600 --sweep cmd_wait_bytes=0:12
  python3 tools/sd_stress_runner.py --frames 900 --threshold read_wait_bytes=0:128 \
      --expect-mem SRAM:0x10F:==:1 --trace
  python3 tools/sd_stress_runner.py --sweep cmd_wait_bytes=0,2,4,8 \
      --sweep write_busy_ms=10,50,100,250 --csv sd-matrix.csv
"""
from __future__ import annotations
import argparse, csv, itertools, json, pathlib, sys, time
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Tuple
from cuzebox_api import CUzeBoxApi

MODEL_KEYS=["init_ms","cmd_wait_bytes","read_wait_bytes","write_busy_ms","cs_high_ms","init_min_byte_cycles","init_max_byte_cycles"]
SETTINGS=set(MODEL_KEYS)
OPS={"==","=","eq","!=","ne","<","lt","<=","le",">","gt",">=","ge","&","and"}

def parse_int(s:str)->int:return int(s,0)
def parse_values(spec:str)->List[int]:
    spec=spec.strip()
    if ',' in spec:return [parse_int(x.strip()) for x in spec.split(',') if x.strip()]
    parts=spec.split(':')
    if len(parts) in (2,3):
        a,b=map(parse_int,parts[:2]);step=parse_int(parts[2]) if len(parts)==3 else (1 if b>=a else -1)
        if step==0:raise ValueError('step cannot be zero')
        if (b-a)*step<0:raise ValueError('step moves away from end')
        return list(range(a,b+(1 if step>0 else -1),step))
    return [parse_int(spec)]
def parse_sweep(text:str)->Tuple[str,List[int]]:
    if '=' not in text:raise ValueError('sweep must be SETTING=VALUES')
    k,v=text.split('=',1);k=k.strip()
    if k not in SETTINGS:raise ValueError(f'unknown SD setting {k}')
    vals=parse_values(v)
    if not vals:raise ValueError('empty sweep')
    return k,vals
def parse_threshold(text:str)->Tuple[str,int,int]:
    if '=' not in text:raise ValueError('threshold must be SETTING=LOW:HIGH')
    k,v=text.split('=',1);k=k.strip()
    if k not in SETTINGS:raise ValueError(f'unknown SD setting {k}')
    p=v.split(':')
    if len(p)!=2:raise ValueError('threshold needs exactly LOW:HIGH')
    a,b=parse_int(p[0]),parse_int(p[1])
    return k,min(a,b),max(a,b)
def compare(actual:int,op:str,expected:int)->bool:
    op=op.lower()
    if op in ('==','=','eq'):return actual==expected
    if op in ('!=','ne'):return actual!=expected
    if op in ('<','lt'):return actual<expected
    if op in ('<=','le'):return actual<=expected
    if op in ('>','gt'):return actual>expected
    if op in ('>=','ge'):return actual>=expected
    if op in ('&','and'):return (actual&expected)!=0
    raise ValueError(f'unknown operator {op}')
def parse_expect_mem(text:str)->Tuple[str,int,str,int]:
    p=text.split(':')
    if len(p)!=4 or p[2].lower() not in OPS:raise ValueError('expect-mem must be REGION:ADDR:OP:VALUE')
    return p[0].upper(),parse_int(p[1]),p[2],parse_int(p[3])

def preset_name(model:Dict[str,Any])->str:
    return str(model.get('preset_name','CUSTOM')).upper()

@dataclass
class Result:
    settings:Dict[str,int]; passed:bool; reason:str; elapsed_s:float; frames_remaining:int=0
    mem_actual:int|None=None; timing:Dict[str,Any]|None=None
    def flat(self)->Dict[str,Any]:
        d={**self.settings,'pass':int(self.passed),'reason':self.reason,'elapsed_s':round(self.elapsed_s,4),'frames_remaining':self.frames_remaining}
        if self.mem_actual is not None:d['mem_actual']=self.mem_actual
        if self.timing:
            sm=self.timing.get('summary',{});d.update({'timing_samples':sm.get('samples',0),'timing_errors':sm.get('errors',0),'r1_max_cycles':sm.get('response_max_cycles',0),'token_max_cycles':sm.get('token_max_cycles',0),'busy_max_cycles':sm.get('busy_max_cycles',0)})
        return d

class StressRunner:
    def __init__(self,api:CUzeBoxApi,args:argparse.Namespace):
        self.api=api;self.args=args;self.original=None;self.trace_original=None
    def cmd(self,s:str)->Dict[str,Any]:
        r=self.api.command(s)
        if not r.get('ok'):raise RuntimeError(f'{s}: {r.get("error","API error")}')
        return r
    def preflight(self)->None:
        sd=self.cmd('SD_STATUS');self.original=dict(sd.get('model') or {})
        rp=self.cmd('SD_REPLAY STATUS')
        if int(rp.get('mode',0)) in (1,2):raise RuntimeError('SD replay/recording is active; stop it before a stress sweep')
        fault=self.cmd('SD_FAULT STATUS')
        if fault.get('active') and not self.args.allow_faults:raise RuntimeError('SD fault injection is active; clear faults or pass --allow-faults')
        self.trace_original=self.cmd('SD_TRACE STATUS')
        br=dict(self.trace_original.get('breaks') or {})
        break_active=(int(br.get('cmd_mask_lo',0))!=0 or int(br.get('cmd_mask_hi',0))!=0 or bool(br.get('sector_on')) or
                      bool(br.get('init_fail')) or bool(br.get('crc')) or bool(br.get('latency_on')) or
                      int(br.get('fs_role_mask',0))!=0 or bool(str(br.get('fs_path','')).strip()))
        if break_active and not self.args.allow_breaks:
            raise RuntimeError('SD protocol/filesystem break conditions are armed; clear them or pass --allow-breaks')
        if self.args.trace and not self.trace_original.get('built'):
            raise RuntimeError('--trace requested but SD protocol tracing is not built into this CUzeBox')
        if self.args.rom:self.cmd(f'LOAD_ROM WAIT {self.args.rom}')
    def restore(self)->None:
        if self.original:
            try:
                p=preset_name(self.original)
                if p in {'SLOW','NORMAL','FAST'}:self.cmd(f'SD_PRESET {p}')
                else:
                    for k in MODEL_KEYS:
                        if k in self.original:self.cmd(f'SD_SET {k} {int(self.original[k])}')
                self.cmd('SD_RESET')
            except Exception as e:print(f'warning: could not restore SD model: {e}',file=sys.stderr)
        if self.args.trace and self.trace_original is not None:
            try:self.cmd('SD_TRACE ENABLE' if self.trace_original.get('enabled') else 'SD_TRACE DISABLE')
            except Exception as e:print(f'warning: could not restore SD trace state: {e}',file=sys.stderr)
    def wait_case(self,timeout:float)->Tuple[bool,str,int]:
        deadline=time.monotonic()+timeout;last={}
        while time.monotonic()<deadline:
            last=self.cmd('GET_STATE');rem=int(last.get('run_frames_remaining',0));paused=bool(last.get('paused',0))
            if paused:
                if rem==0:return True,'frame budget completed',0
                return False,'debugger stopped before frame budget completed',rem
            time.sleep(0.01)
        return False,'timeout waiting for frame budget',int(last.get('run_frames_remaining',-1))
    def run_case(self,settings:Dict[str,int])->Result:
        t0=time.monotonic();self.cmd('PAUSE');self.cmd('RESET')
        for k,v in settings.items():self.cmd(f'SD_SET {k} {v}')
        self.cmd('SD_RESET')
        if self.args.trace:
            self.cmd('SD_TRACE CLEAR');self.cmd('SD_TRACE ENABLE')
        self.cmd(f'RUN_FRAMES {self.args.frames}')
        passed,reason,rem=self.wait_case(self.args.case_timeout)
        mem_actual=None
        if passed and self.args.expect_mem:
            reg,addr,op,expected=self.args.expect_mem
            r=self.cmd(f'READ_MEM {reg} {addr} 1');vals=r.get('values') or []
            if not vals:passed=False;reason='success memory value unavailable'
            else:
                mem_actual=int(vals[0],0) if isinstance(vals[0],str) else int(vals[0]);passed=compare(mem_actual,op,expected)
                reason=f'{reg}[{addr:#x}]={mem_actual:#x} {op} {expected:#x}' + (' PASS' if passed else ' FAIL')
        timing=None
        if self.args.trace:
            try:timing=self.cmd('SD_TIMING_ANALYSIS 16')
            except Exception:timing=None
        if passed and self.args.conformance:
            if timing is None:
                passed=False;reason='SD conformance analysis unavailable'
            else:
                terr=int((timing.get('summary') or {}).get('errors',0))
                if terr:
                    passed=False;reason=f'SD conformance errors: {terr}'
        return Result(dict(settings),passed,reason,time.monotonic()-t0,rem,mem_actual,timing)

def cartesian(sweeps:List[Tuple[str,List[int]]])->Iterable[Dict[str,int]]:
    if not sweeps:yield {};return
    keys=[k for k,_ in sweeps]
    for vals in itertools.product(*(v for _,v in sweeps)):yield dict(zip(keys,vals))
def threshold_search(runner:StressRunner,key:str,lo:int,hi:int)->Tuple[int|None,List[Result]]:
    results=[];best=None
    while lo<=hi:
        mid=(lo+hi)//2;res=runner.run_case({key:mid});results.append(res);print_result(res)
        if res.passed:best=mid;lo=mid+1
        else:hi=mid-1
    return best,results
def print_result(r:Result)->None:
    cfg=' '.join(f'{k}={v}' for k,v in r.settings.items()) or 'baseline'
    print(f'[{"PASS" if r.passed else "FAIL"}] {cfg} · {r.reason} · {r.elapsed_s:.2f}s')
def write_results(path:str|None,rows:List[Result],as_json:bool)->None:
    if not path:return
    p=pathlib.Path(path);flat=[r.flat() for r in rows]
    if as_json:p.write_text(json.dumps(flat,indent=2,sort_keys=True)+'\n');return
    keys=[]
    for d in flat:
        for k in d:
            if k not in keys:keys.append(k)
    with p.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(flat)

def main()->int:
    ap=argparse.ArgumentParser(description='Sweep CUzeBox SD timing parameters and find ROM tolerance thresholds.')
    ap.add_argument('--host',default='127.0.0.1');ap.add_argument('--port',type=int,default=24680);ap.add_argument('--timeout',type=float,default=5.0)
    ap.add_argument('--rom',help='optional ROM path loaded once in WAIT mode before testing')
    ap.add_argument('--frames',type=int,default=600,help='frame budget per case (default: 600)');ap.add_argument('--case-timeout',type=float,default=20.0)
    ap.add_argument('--sweep',action='append',default=[],metavar='SETTING=VALUES',help='VALUES: 0:8, 0:16:2, or 0,1,2,4')
    ap.add_argument('--threshold',metavar='SETTING=LOW:HIGH',help='binary-search largest passing value; increasing values must be monotonically harder')
    ap.add_argument('--expect-mem',type=parse_expect_mem,metavar='REGION:ADDR:OP:VALUE',help='optional success condition checked after the frame budget')
    ap.add_argument('--trace',action='store_true',help='arm SD protocol history for each case and include timing maxima')
    ap.add_argument('--conformance',action='store_true',help='enable trace analysis and fail cases that contain SD protocol/conformance errors')
    ap.add_argument('--allow-faults',action='store_true',help='allow already-armed SD fault rules to participate')
    ap.add_argument('--allow-breaks',action='store_true',help='allow already-armed SD protocol/filesystem break conditions to stop cases')
    ap.add_argument('--csv');ap.add_argument('--json')
    args=ap.parse_args()
    if args.frames<1:ap.error('--frames must be >= 1')
    if args.conformance:args.trace=True
    try:sweeps=[parse_sweep(x) for x in args.sweep];threshold=parse_threshold(args.threshold) if args.threshold else None
    except ValueError as e:ap.error(str(e))
    if threshold and sweeps:ap.error('--threshold and --sweep are mutually exclusive')
    api=CUzeBoxApi(args.host,args.port,args.timeout);runner=StressRunner(api,args);rows=[]
    try:
        api.connect();runner.preflight()
        best=None
        if threshold:
            key,lo,hi=threshold;best,rows=threshold_search(runner,key,lo,hi);print(f'largest passing {key}: {best if best is not None else "none"}')
        else:
            for cfg in cartesian(sweeps):res=runner.run_case(cfg);rows.append(res);print_result(res)
        write_results(args.csv,rows,False);write_results(args.json,rows,True)
        if threshold:return 0 if best is not None else 2
        return 0 if all(r.passed for r in rows) else 2
    except Exception as e:print(f'error: {e}',file=sys.stderr);return 1
    finally:
        try:runner.restore()
        finally:api.close()
if __name__=='__main__':raise SystemExit(main())
