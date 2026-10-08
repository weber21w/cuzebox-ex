#!/usr/bin/env python3
from __future__ import annotations
import pathlib,sys
from types import SimpleNamespace
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import sd_stress_runner as s

class FakeApi:
 def __init__(self,breaks=None,trace_built=1,trace_enabled=0):
  self.calls=[];self.breaks=breaks or {};self.trace_built=trace_built;self.trace_enabled=trace_enabled
 def command(self,cmd):
  self.calls.append(cmd)
  if cmd=='SD_STATUS':
   return {'ok':1,'model':{'preset_name':'NORMAL','init_ms':500,'cmd_wait_bytes':1,'read_wait_bytes':1,'write_busy_ms':100,'cs_high_ms':1,'init_min_byte_cycles':8,'init_max_byte_cycles':0}}
  if cmd=='SD_REPLAY STATUS':return {'ok':1,'mode':0}
  if cmd=='SD_FAULT STATUS':return {'ok':1,'active':0}
  if cmd=='SD_TRACE STATUS':return {'ok':1,'built':self.trace_built,'enabled':self.trace_enabled,'breaks':self.breaks}
  return {'ok':1}

def runner_args(**kw):
 d=dict(allow_faults=False,allow_breaks=False,trace=False,conformance=False,rom=None)
 d.update(kw);return SimpleNamespace(**d)


def main():
 assert s.parse_values('0:4')==[0,1,2,3,4]
 assert s.parse_values('4:0:-2')==[4,2,0]
 assert s.parse_values('0,2,8')==[0,2,8]
 assert s.parse_sweep('cmd_wait_bytes=0:2')==('cmd_wait_bytes',[0,1,2])
 assert s.parse_threshold('read_wait_bytes=1:9')==('read_wait_bytes',1,9)
 assert s.compare(5,'>=',5) and s.compare(5,'&',1) and not s.compare(4,'&',1)
 assert s.parse_expect_mem('SRAM:0x100:==:0x42')==('SRAM',0x100,'==',0x42)
 vals=list(s.cartesian([('cmd_wait_bytes',[0,1]),('read_wait_bytes',[2,4])]))
 assert vals==[{'cmd_wait_bytes':0,'read_wait_bytes':2},{'cmd_wait_bytes':0,'read_wait_bytes':4},{'cmd_wait_bytes':1,'read_wait_bytes':2},{'cmd_wait_bytes':1,'read_wait_bytes':4}]
 # Stress preflight refuses inherited SD breakpoints by default.
 f=FakeApi({'cmd_mask_lo':1})
 try:s.StressRunner(f,runner_args()).preflight();assert False
 except RuntimeError as e:assert 'break conditions' in str(e)
 # --allow-breaks permits them, while --trace requires the feature to exist.
 f=FakeApi({'sector_on':1})
 s.StressRunner(f,runner_args(allow_breaks=True)).preflight()
 f=FakeApi(trace_built=0)
 try:s.StressRunner(f,runner_args(trace=True)).preflight();assert False
 except RuntimeError as e:assert 'not built' in str(e)
 # A trace-enabled stress session restores the incoming enabled/disabled state.
 f=FakeApi(trace_built=1,trace_enabled=0);r=s.StressRunner(f,runner_args(trace=True));r.preflight();r.restore()
 assert 'SD_TRACE DISABLE' in f.calls
 f=FakeApi(trace_built=1,trace_enabled=1);r=s.StressRunner(f,runner_args(trace=True));r.preflight();r.restore()
 assert 'SD_TRACE ENABLE' in f.calls
 # Conformance mode promotes trace-analysis errors to a case failure.
 class CaseRunner(s.StressRunner):
  def cmd(self,cmd):
   if cmd=='GET_STATE':return {'ok':1,'paused':1,'run_frames_remaining':0}
   if cmd.startswith('SD_TIMING_ANALYSIS'):return {'ok':1,'summary':{'errors':2}}
   return {'ok':1}
 cr=CaseRunner(None,SimpleNamespace(trace=True,conformance=True,frames=1,case_timeout=.1,expect_mem=None))
 rr=cr.run_case({});assert not rr.passed and rr.reason=='SD conformance errors: 2'
 print('SD stress runner regression: PASS');return 0
if __name__=='__main__':raise SystemExit(main())
