"""Real linked ARM safety + parameter store; peripherals and power cuts simulated.
No hardware timing, retention endurance or mechanical stopping claim is made.
"""
from pathlib import Path
import json, struct
exec(compile(Path(__file__).with_name('verify_drive_current.py').read_text('utf8').split('\ndef defaults(')[0], 'protection_fixture','exec'))
PWR=0x40007004
TIM_BKDT=0x40013444
SLOT_A=0x0803F000
SLOT_B=0x0803F800
PAGE=0x800
passed=0
results=[]

def mem32(f,a,v=None):
 if v is not None:f.uc.mem_write(a,struct.pack('<I',v & 0xffffffff))
 return int.from_bytes(f.uc.mem_read(a,4),'little')
def sd(f,index,value=0,sub=0,size=4,write=False,ca=0):
 return sdo(f,index,value,size,sub,ca,write)
def dg(f,name):return f.get('gDriveParameterDiag',name)
def sg(f,name):return f.get('gDriveSafetyDiag',name)
def setp(f,name,value):f.put('LocalAxes',value,'Objects',name)
def cold_defaults(f):
 f.call('EthercatMotorControl_Init');f.call('DriveCurrent_Init');f.call('DriveFault_Init');f.call('DriveTelemetry_Init')
 f.write('gDriveBootState',0,2)
 f.write('gEcatMotorEnabled',0,1)
 f.write('nAlStatus',2,1)
 setp(f,'objControlWord',0)
 f.call('EthercatMotorControl_EmergencyStop')
 f.put('gEcatPulseDiag',0,'lastPhaseStep')
 f.call('DriveSafety_Init')
 f.call('DriveParameters_Init')
def make():
 f=Fixture();reset(f)
 f.uc.mem_map(0x42000000,0x200000) # SDK peripheral bit-band aliases
 f.uc.mem_write(SLOT_A,b'\xff'*(2*PAGE))
 f.bus=24000;f.sensor_status=3;f.sample_seq=1
 def sample(dest,*_):
  f.uc.mem_write(dest,struct.pack('<6I2H',f.sensor_status,f.sample_seq,0,f.bus,0,0,2068,2068))
 f.hook('DriveTelemetry_GetSafetySample',sample)
 f.programs=[];f.erases=[];f.fail_at=None;f.erase_fail=False;f.corrupt_at=None
 for name in ('FLASH_Unlock','FLASH_ClearFlag','FLASH_Lock'):
  f.hook(name,lambda *_:0)
 def erase(addr,*_):
  f.erases.append(addr)
  assert addr in (SLOT_A,SLOT_B)
  if f.erase_fail:return 7
  f.uc.mem_write(addr,b'\xff'*PAGE);return 6
 def program(addr,value,*_):
  f.programs.append((addr,value))
  n=len(f.programs)
  if f.fail_at==n:return 3
  previous=mem32(f,addr)
  assert previous & value==value
  mem32(f,addr,value ^ (1 if f.corrupt_at==n else 0));return 6
 f.hook('FLASH_EraseOnePage',erase);f.hook('FLASH_ProgramWord',program)
 cold_defaults(f)
 return f

def test(name,fn):
 global passed
 f=make()
 try:
  fn(f);passed+=1;results.append(name);print('PASS '+name,flush=True)
 finally:f.close()
def store(f,restore=False):
 assert sd(f,0x1011 if restore else 0x1010,0x64616f6c if restore else 0x65766173,sub=1,write=True)[0]==0
 assert dg(f,'status') in (2,3,5),(dg(f,'status'),dg(f,'error'))
def set_params(f):
 for idx,v,size,sub in [(0x20F1,1800,2,0),(0x20D5,50,2,0),(0x20F5,500,2,0),
  (0x6083,2500,4,0),(0x6084,2000,4,0),(0x6085,3500,4,0),
  (0x6098,21,1,0),(0x6099,650,4,1),(0x6099,90,4,2),
  (0x609A,45000,4,0),(0x607C,-123,4,0),(0x20E1,28000,4,0),(0x20E2,12000,4,0)]:
  assert sd(f,idx,v,sub,size,True)[0]==0,(hex(idx),v)

def blank(f):
 assert dg(f,'loadedSlot')==dg(f,'validMask')==0
 assert sd(f,0x20F1,size=2)==(0,3000)
 for i in (0x6083,0x6084,0x6085):assert sd(f,i)==(0,3000)
 for i in (0x1010,0x1011):
  assert sd(f,i,sub=0,size=1)==(0,1)
  assert sd(f,i,sub=1)==(0,1)
 assert sg(f,'restartInhibit')==1
 assert not f.call('DriveSafety_PowerAllowed')
test('blank Flash uses 3000 defaults and leaves output interlocked',blank)

def access(f):
 for i in (0x1010,0x1011):
  for kw in ({'value':0,'sub':1},{'value':0x65766173,'sub':0},{'value':0x65766173,'sub':2},
             {'value':0x65766173,'sub':1,'size':2},{'value':0x65766173,'sub':1,'ca':1}):
   assert sd(f,i,write=True,**kw)[0]!=0
 for a in (1,4,8):
  f.write('nAlStatus',a,1);assert sd(f,0x1010,0x65766173,1,write=True)[0]!=0
 f.write('nAlStatus',2,1)
 for bad in ('enabled','moving','boot','fault','cw','voltage','stale','pvd'):
  if bad=='enabled':f.write('gEcatMotorEnabled',1,1)
  if bad=='moving':f.put('gEcatPulseDiag',1,'lastPhaseStep')
  if bad=='boot':f.write('gDriveBootState',2,2)
  if bad=='fault':f.write('ErrNumFlag',13,1)
  if bad=='cw':setp(f,'objControlWord',15)
  if bad=='voltage':f.bus=18500
  if bad=='stale':f.sensor_status=0
  if bad=='pvd':mem32(f,PWR,4)
  assert sd(f,0x1010,0x65766173,1,write=True)[0]!=0,bad
  f.write('gEcatMotorEnabled',0,1);f.put('gEcatPulseDiag',0,'lastPhaseStep')
  f.write('gDriveBootState',0,2);f.write('ErrNumFlag',0,1);setp(f,'objControlWord',0)
  f.bus=24000;f.sensor_status=3;mem32(f,PWR,0)
 assert not f.programs and not f.erases
 for i in range(0x20c0,0x20ca):
  assert sd(f,i)[0]==0 and sd(f,i,1,write=True)[0]!=0
 test_result=sd(f,0x20c8)[1];assert test_result==0x20261111
 test_result=sd(f,0x20c5)[1];assert test_result==1
test('real SDO signatures, permissions, health and command length checks',access)

def persist(f):
 set_params(f);store(f)
 assert dg(f,'loadedSlot')==1 and dg(f,'generation')==1
 assert len(f.programs)==33 and f.programs[-1]==(SLOT_A+128,0x434f4d54)
 setp(f,'objControlWord',15);setp(f,'objTargetVelocity',500);setp(f,'objTargetPosition',123456)
 f.write('gEcatMotorEnabled',1,1)
 cold_defaults(f)
 assert dg(f,'loadedSlot')==1 and dg(f,'generation')==1
 assert sd(f,0x20F1,size=2)==(0,1800) and sd(f,0x6098,size=1)==(0,21)
 assert sd(f,0x6083)==(0,2500) and sd(f,0x6084)==(0,2000) and sd(f,0x6085)==(0,3500)
 assert sd(f,0x607C)==(0,(-123)&0xffffffff)
 assert sd(f,0x6099,sub=1)==(0,650) and sd(f,0x6099,sub=2)==(0,90)
 assert f.get('LocalAxes','Objects','objControlWord')==0
 assert f.get('LocalAxes','Objects','objTargetPosition')==0 and f.get('gEcatPulseDiag','countedPosition')==0
 assert f.read('gEcatMotorEnabled',1)==0 and sg(f,'restartInhibit')==1
 assert f.get('gDriveHomingDiag','state')==0
 assert sd(f,0x609A)==(0,45000) and sd(f,0x20E1)==(0,28000)
 for i in (0x6083,0x6084,0x6085):assert sd(f,i)[1]!=3000
 test_status=dg(f,'status');assert test_status==0
 test_phase=f.read('Iangle');assert test_phase==0
test('save/load retains settings and never restores enable, target, position or HM success',persist)

def unchanged(f):
 store(f);count=len(f.programs);erase=len(f.erases)
 for _ in range(10):store(f)
 assert dg(f,'status')==3 and len(f.programs)==count and len(f.erases)==erase
 assert dg(f,'generation')==1
 assert sd(f,0x20F1,2000,size=2,write=True)[0]==0
 f.call('DriveParameters_Update');assert sd(f,0x20c4)==(0,1)
 store(f);assert dg(f,'loadedSlot')==2 and dg(f,'generation')==2
 assert len(f.erases)==2
 cold_defaults(f);assert sd(f,0x20F1,size=2)==(0,2000)
test('unchanged saves cause no Flash wear; changes alternate A/B with newest generation',unchanged)

def blocked(f):
 assert f.call('DriveParameters_Request',0)==0
 for idx,value,size,sub in [(0x20F1,2000,2,0),(0x6083,2000,4,0),(0x2000,1,2,0),
                           (0x6098,21,1,0),(0x2010,3300,4,0),(0x2021,3500,2,0)]:
  assert sd(f,idx,value,sub,size,True)[0]!=0,hex(idx)
 assert sd(f,0x1010,0x65766173,1,write=True)[0]!=0
 f.write('nAlStatus',8,1);f.call('DriveSafety_OnValidPdo',0);assert sg(f,'restartInhibit')==1
 f.write('nAlStatus',2,1)
 f.call('DriveParameters_Update',count=2000000);assert dg(f,'status')==2
 test_power=f.call('DriveSafety_PowerAllowed');assert test_power==0
test('pending save locks config writers and power output, never only half a snapshot',blocked)

def cuts(f):
 store(f);old=bytes(f.uc.mem_read(SLOT_A,2*PAGE))
 for n in range(1,34):
  f.uc.mem_write(SLOT_A,old);cold_defaults(f)
  assert sd(f,0x20F1,1800,size=2,write=True)[0]==0
  f.programs=[];f.fail_at=n
  assert f.call('DriveParameters_Request',0)==0
  f.call('DriveParameters_Update',count=2000000)
  assert dg(f,'status')==4,(n,dg(f,'status'))
  assert bytes(f.uc.mem_read(SLOT_A,PAGE))==old[:PAGE]
  f.fail_at=None;cold_defaults(f)
  assert dg(f,'loadedSlot')==1 and sd(f,0x20F1,size=2)==(0,3000),n
 test_n=n;assert test_n==33
test('interruption at every one of 33 program/commit words preserves the previous complete save',cuts)

def corruption(f):
 store(f);assert sd(f,0x20F1,1800,size=2,write=True)[0]==0;store(f)
 b=bytes(f.uc.mem_read(SLOT_B,132))
 for i in (0,4,8,12,16,20,100,124,128):
  f.uc.mem_write(SLOT_B,b);mem32(f,SLOT_B+i,mem32(f,SLOT_B+i)^1)
  cold_defaults(f);assert dg(f,'loadedSlot')==1 and sd(f,0x20F1,size=2)==(0,3000),i
 f.uc.mem_write(SLOT_A,b'\xff'*PAGE);f.uc.mem_write(SLOT_B,b'\xff'*PAGE)
 cold_defaults(f);assert dg(f,'loadedSlot')==0
 test_status=sg(f,'restartInhibit');assert test_status==1
test('CRC/header/schema/commit corruption falls back to the last valid page or factory defaults',corruption)

def restore(f):
 set_params(f);store(f);cold_defaults(f)
 store(f,True)
 assert dg(f,'status')==5 and dg(f,'restorePending')==1
 assert sd(f,0x20F1,size=2)==(0,1800) # current session not silently changed
 assert sd(f,0x20F1,1500,size=2,write=True)[0]!=0
 assert not f.call('DriveSafety_PowerAllowed')
 cold_defaults(f)
 assert dg(f,'restorePending')==0 and sd(f,0x20F1,size=2)==(0,3000)
 assert sd(f,0x6098,size=1)==(0,37) and sd(f,0x6083)==(0,3000)
test('1011 restore is itself transactional, takes effect next boot and inhibits intervening motion',restore)

def save_failure(f):
 store(f);assert sd(f,0x20F1,1800,size=2,write=True)[0]==0
 for why in ('erase','program','verify','voltage','state'):
  f.programs=[];f.erase_fail=why=='erase';f.fail_at=1 if why=='program' else None;f.corrupt_at=1 if why=='verify' else None
  assert f.call('DriveParameters_Request',0)==0
  if why=='voltage':f.bus=17000
  if why=='state':f.write('nAlStatus',8,1)
  f.call('DriveParameters_Update',count=2000000)
  assert dg(f,'status')==4,why
  f.bus=24000;f.write('nAlStatus',2,1);f.erase_fail=False;f.fail_at=f.corrupt_at=None
  cold_defaults(f);assert sd(f,0x20F1,size=2)==(0,3000)
  assert sd(f,0x20F1,1800,size=2,write=True)[0]==0
 test_counter=len(f.erases);assert test_counter>=4
test('erase/program/readback failure and conditions changing after SDO acceptance do not publish a bad page',save_failure)

def restart(f):
 f.write('nAlStatus',8,1);f.put('LocalAxes',1,'bAxisIsActive');setp(f,'objModesOfOperation',3)
 for _ in range(4):
  setp(f,'objControlWord',15);f.call('CiA402_StateMachine');f.call('EthercatMotorControl_Update')
 assert f.get('LocalAxes','i16State')==2 and f.read('gEcatMotorEnabled',1)==0
 setp(f,'objControlWord',0);f.call('EthercatMotorControl_OnSyncCycle')
 assert sg(f,'restartInhibit')==1 # sampling an OD word is not a received PDO
 assert sd(f,0x6040,0,size=2,write=True)[0]==0
 assert sg(f,'restartInhibit')==1
 f.call('DriveSafety_OnValidPdo',0);assert sg(f,'restartInhibit')==0
 enable(f,60);tick(f,20);assert any(f.pwm)
 f.call('APPL_StopOutputHandler');assert sg(f,'restartInhibit')==1
 setp(f,'objControlWord',15);f.call('EthercatMotorControl_Update');tick(f)
 assert not any(f.pwm) and f.read('gEcatMotorEnabled',1)==0
 test_rearms=sg(f,'rearmCount');assert test_rearms==1
test('retained enable, SDO disable and cached Sync0 values cannot release the restart interlock',restart)

def watchdog(f):
 feeds=[];f.hook('IWDG_ReloadKey',lambda *_:feeds.append(1))
 f.call('DriveSafety_StartWatchdog');assert len(feeds)==1
 assert mem32(f,0x40003000)&0xffff==0xcccc
 assert mem32(f,0x40007008)&0x400
 for _ in range(4):f.call('DriveSafety_Update')
 assert len(feeds)==1
 f.call('DriveSafety_MotorTick');f.call('DriveSafety_Update');assert len(feeds)==1
 f.call('DriveSafety_SysTick');f.call('DriveSafety_Update');assert len(feeds)==2
 for _ in range(5):f.call('DriveSafety_MotorTick');f.call('DriveSafety_Update')
 assert len(feeds)==2
 f.call('DriveSafety_SysTick');f.call('DriveSafety_Update');assert len(feeds)==3
 for _ in range(5):f.call('DriveSafety_SysTick');f.call('DriveSafety_Update')
 assert len(feeds)==3 and sg(f,'watchdogFeeds')==2
test('watchdog is hardware-enabled and feeds only after main, TIM8 and SysTick all progress',watchdog)

def bus_sample(f,seq,mv,status=3):
 f.uc.mem_write(f.data,struct.pack('<6I2H',status,seq,0,mv,0,0,2068,2068))
 f.call('DriveSafety_ObserveBus',f.data)
def power_fail(f):
 f.write('nAlStatus',8,1);f.call('DriveSafety_OnValidPdo',0);enable(f,60);tick(f,30)
 assert any(f.pwm)
 bus_sample(f,1,24000)
 bus_sample(f,2,17000);bus_sample(f,3,17000)
 assert f.read('ErrNumFlag',1)==0
 bus_sample(f,3,17000);assert f.read('ErrNumFlag',1)==0 # repeated frame resets qualification
 bus_sample(f,4,17000);bus_sample(f,5,17000);bus_sample(f,6,17000)
 assert f.read('ErrNumFlag',1)==13 and f.read('gDriveFaultReason',2)==15
 assert not any(f.pwm) and not(mem32(f,TIM_BKDT)&0x8000)
 assert sg(f,'restartInhibit')==sg(f,'powerLossLatched')==1
 bus_sample(f,7,24000);assert f.read('ErrNumFlag',1)==13
 assert not f.programs and not f.erases
 f.call('EthercatMotorControl_Update');assert sd(f,0x603F,size=2)==(0,0x3220)
test('3 consecutive fresh low-bus frames remove PWM and latch 3220 without emergency Flash writes',power_fail)

def pvd(f):
 mem32(f,TIM_BKDT,0x8000);mem32(f,PWR,4)
 f.hook('EXTI_GetITStatus',lambda *_:1);f.hook('EXTI_ClrITPendBit',lambda *_:0)
 f.call('PVD_IRQHandler')
 assert not(mem32(f,TIM_BKDT)&0x8000)
 assert f.read('gDriveFaultReason',2)==14 and sg(f,'powerLossLatched')==1
 assert f.call('DriveSafety_ResetBlockMask') & 0x100
 mem32(f,PWR,0);f.call('DriveSafety_MotorTick')
 assert sg(f,'powerLossLatched')==1
 assert f.call('DriveSafety_ResetBlockMask') & 0x100==0
 assert not any(f.pwm) and not f.programs
 test_mask=f.call('DriveFault_GetResetBlockMask');assert test_mask!=0
test('PVD logic-supply fall cuts MOE immediately and never self-clears on voltage recovery',pvd)

def fatal(f):
 mem32(f,TIM_BKDT,0x8000)
 for off in (0x34,0x38,0x3c,0x40):mem32(f,0x40013400+off,1500)
 f.call('DriveSafety_FatalOutputOff')
 assert not(mem32(f,TIM_BKDT)&0x8000)
 for off in (0x34,0x38,0x3c,0x40):assert mem32(f,0x40013400+off)==0
test('fatal exception output-off clears MOE and all four compares without relying on ADC state',fatal)


def crc_record(f,addr):
 import zlib
 mem32(f,addr+124,zlib.crc32(bytes(f.uc.mem_read(addr,124))))
def semantic_invalid(f):
 store(f);assert sd(f,0x20F1,1800,size=2,write=True)[0]==0;store(f)
 b=bytes(f.uc.mem_read(SLOT_B,132))
 for offset,value in [(16,2000),(20,2),(20+3*4,0),(20+6*4,3500),(20+9*4,23),(20+12*4,501),(20+16*4,1000)]:
  f.uc.mem_write(SLOT_B,b);mem32(f,SLOT_B+offset,value);crc_record(f,SLOT_B)
  cold_defaults(f);assert dg(f,'loadedSlot')==1 and sd(f,0x20F1,size=2)==(0,3000),(offset,value)
test('even a correct CRC cannot load wrong-resolution or out-of-range parameters',semantic_invalid)
def rescan(f):
 store(f);assert sd(f,0x20F1,1800,size=2,write=True)[0]==0;store(f)
 mem32(f,SLOT_B+124,mem32(f,SLOT_B+124)^1) # newest page suffers runtime corruption
 assert sd(f,0x20F1,1600,size=2,write=True)[0]==0
 f.programs=[];f.fail_at=1
 assert sd(f,0x1010,0x65766173,1,write=True)[0]!=0
 assert f.erases[-1]==SLOT_B # never erase the surviving A fallback
 f.fail_at=None;cold_defaults(f)
 assert sd(f,0x20F1,size=2)==(0,3000) and dg(f,'loadedSlot')==1
test('save rechecks both pages and reports SDO failure rather than erasing the sole fallback',rescan)
def wrap(f):
 store(f);mem32(f,SLOT_A+12,0xffffffff);crc_record(f,SLOT_A)
 cold_defaults(f);assert dg(f,'generation')==0xffffffff
 assert sd(f,0x20F1,1700,size=2,write=True)[0]==0;store(f)
 assert dg(f,'generation')==0
 cold_defaults(f);assert dg(f,'loadedSlot')==2 and sd(f,0x20F1,size=2)==(0,1700)
test('generation serial arithmetic selects the newest record across a 32-bit wrap',wrap)

def units(f):
 assert sd(f,0x2000,1,size=2,write=True)[0]==0
 assert sd(f,0x6083,42000,write=True)[0]==0
 assert sd(f,0x6084,51000,write=True)[0]==0
 store(f);cold_defaults(f)
 assert sd(f,0x2000,size=2)==(0,1) and sd(f,0x6083)==(0,42000) and sd(f,0x6084)==(0,51000)
 assert sd(f,0x6085)==(0,50000)
test('count units and matching speed/ramp values reload together without reinterpretation',units)

def early_enable(f):
 f.write('nAlStatus',8,1);f.put('LocalAxes',1,'bAxisIsActive');f.put('LocalAxes',2,'i16State')
 f.call('DriveSafety_OnValidPdo',0)
 f.write('gDriveBootState',1,2);f.put('gDriveBootDiag',0,'normalReleased')
 setp(f,'objControlWord',6)
 for _ in range(5):f.call('CiA402_StateMachine');f.call('EthercatMotorControl_Update')
 assert f.get('LocalAxes','i16State')==2 and not f.read('gEcatMotorEnabled',1)
 # The completed zero-only task retires its gate on an actually disabled CW.
 f.write('gDriveBootState',5,2)
 f.uc.mem_write(f.data,struct.pack('<6I2H',3,1,0,24000,0,0,2068,2068))
 f.call('DriveBoot_Tick',f.data,1)
 assert f.get('gDriveBootDiag','normalReleased')==1
 for cw in (6,7,15):setp(f,'objControlWord',cw);f.call('CiA402_StateMachine')
 assert f.get('LocalAxes','i16State')==0x10
test('an early PLC enable waits in disabled state until unpowered boot calibration has released',early_enable)
(ROOT/'validation/protection_result.json').write_text(json.dumps({'passed':passed,'cases':results,'hardware_operated':False},ensure_ascii=False,indent=2),'utf8')
print(f'PASS {passed} compiled ARM protection/parameter tests; no hardware operated.')
