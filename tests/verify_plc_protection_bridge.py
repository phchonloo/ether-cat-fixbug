"""Delivered PLC MAIN ST + actual ARM CiA402/motion/interlock. Only bus,
GPIO and timer cadence are fixtures. No TwinCAT runtime or hardware is driven."""
from pathlib import Path
import importlib.util,json,sys
ROOT=Path(__file__).resolve().parents[1]
exec(compile((ROOT/'tests/verify_homing.py').read_text('utf8').split('\ndef defaults(')[0],'bridge_arm','exec'))
sys.path.insert(0,str(ROOT/'plc_tests'))
spec=importlib.util.spec_from_file_location('plc_bridge',ROOT/'plc_tests/verify.py')
plc=importlib.util.module_from_spec(spec);spec.loader.exec_module(plc)
passed=0
cases=[]
def motor():
 f=Motor()
 assert f.sdo(0x6098,21,size=1,write=True)[0]==0
 f.write('nAlStatus',8,1);f.put('LocalAxes',1,'bAxisIsActive');f.put('LocalAxes',2,'i16State')
 f.put('LocalAxes',2000,'u32CycleTime');f.put('gDriveSafetyDiag',1,'restartInhibit')
 f.obj('objStatusWord',0x240)
 return f
def exchange(f,b,n=1):
 for _ in range(n):
  for plc_name,obj_name in [('iStatusWord','objStatusWord'),('iActualPosition','objPositionActualValue'),
      ('iActualVelocity','objVelocityActualValue'),('iModeDisplay','objModesOfOperationDisplay')]:b.io[plc_name]=f.obj(obj_name)
  b.scan()
  for plc_name,obj_name in [('qControlWord','objControlWord'),('qTargetPosition','objTargetPosition'),
      ('qTargetVelocity','objTargetVelocity'),('qMode','objModesOfOperation')]:f.obj(obj_name,b.io[plc_name])
  f.call('DriveSafety_OnValidPdo',b.io['qControlWord'])
  f.call('CiA402_StateMachine');f.call('CiA402_Application',f.symbols['LocalAxes'])
  f.update();f.tick(2)
def ready(f,b):
 exchange(f,b,3);b.g['bEnable']=True;exchange(f,b,40)
 assert b.g['bReady'] and f.get('LocalAxes','i16State')==0x10,b.g
 assert f.get('gDriveSafetyDiag','restartInhibit')==0

def run(name,fn):
 global passed
 fn();passed+=1;cases.append(name);print('PASS '+name,flush=True)
def completion():
 f=motor();b=plc.Bench()
 try:
  ready(f,b);b.g['bStart']=True;exchange(f,b,10)
  assert b.g['bBusy'] and f.diag('state')==1
  f.gpio(1);exchange(f,b,20);assert f.diag('state')==3
  f.gpio(0);exchange(f,b,20)
  assert b.g['bDone'] and not b.g['bError'] and b.io['qControlWord']==15,b.g
  assert f.get('LocalAxes','i16State')==0x10 and f.read('gEcatMotorEnabled',1)==1
 finally:f.close()
run('real CiA402 + delivered PLC: disabled PDO, manual enable, HOME21 success and enabled hold',completion)
def reboot(busy):
 f=motor();b=plc.Bench()
 try:
  ready(f,b)
  if busy:b.g['bStart']=True;exchange(f,b,5);assert b.g['bBusy']
 finally:f.close()
 # New MCU instance while the PLC keeps bEnable=TRUE / its old state.
 f=motor()
 try:
  exchange(f,b,50)
  assert b.g['bError'] and b.g['nState']==90 and b.io['qControlWord']==0,b.g
  assert f.diag('startCount')==0 and f.read('gEcatMotorEnabled',1)==0
  b.g['bEnable']=False;exchange(f,b,3)
  b.g['bEnable']=True;exchange(f,b,40)
  assert b.g['bReady'] and not b.g['bError'] and f.diag('startCount')==0,b.g
 finally:f.close()
run('MCU restart with old PLC enable: no automatic restart while ready',lambda:reboot(False))
run('MCU restart during HOME: latched PLC error, new enable edge required',lambda:reboot(True))
def bus_loss():
 f=motor();b=plc.Bench()
 try:
  ready(f,b);b.io['iWcState']=True;exchange(f,b,5)
  assert b.g['bError'] and b.io['qControlWord']==0 and not f.read('gEcatMotorEnabled',1)
  b.io['iWcState']=False;exchange(f,b,20)
  assert b.g['bError'] and not b.g['bReady'] and b.io['qControlWord']==0
 finally:f.close()
run('valid bus recovery alone cannot resume the PLC motion sequence',bus_loss)
(ROOT/'validation/plc_protection_result.json').write_text(json.dumps({'passed':passed,'cases':cases,'hardware_operated':False},indent=2),'utf8')
print(f'PASS {passed} PLC ST + actual ARM CiA402/protection integration cases.')
