"""Real ARM motion/PhaseGet and real GPIO registers; no mechanics/hardware timing."""
from pathlib import Path
import json
exec(compile(Path(__file__).with_name('verify_homing.py').read_text('utf8').split('\ndef test(')[0],'limit_fixture','exec'))
passed=0
cases=[]
def prep(f,mode,sign=1):
 assert f.sdo(0x6081,1000,write=True)[0]==0
 f.write('nAlStatus',8,1);f.put('LocalAxes',1,'bAxisIsActive');f.put('LocalAxes',0x10,'i16State')
 f.put('LocalAxes',2000,'u32CycleTime');f.obj('objControlWord',15);f.obj('objModesOfOperation',mode)
 f.obj('objTargetVelocity',sign*1000);f.obj('objTargetPosition',sign*100000)
 f.update();f.update()
 if mode==1:f.obj('objControlWord',63);f.update()
 f.tick(500)
 assert f.read('gEcatMotorEnabled',1)==1
 assert f.get('gEcatPulseDiag','lastPhaseStep',signed=True)*sign>0

def dg(f,n):return f.get('gDriveSafetyDiag',n)
def pos(f):return f.get('gEcatPulseDiag','countedPosition',signed=True)
def test(name,fn):
 global passed
 f=Motor()
 try:fn(f);passed+=1;cases.append(name);print('PASS '+name,flush=True)
 finally:f.close()
def stopping(f,mode,sign):
 prep(f,mode,sign)
 before=pos(f);velocity=abs(f.get('gEcatPulseDiag','lastPhaseStep',signed=True))
 f.gpio(2 if sign<0 else 4);f.tick(1)
 assert dg(f,'limitStopping')==1 and not f.read('ErrNumFlag',1)
 after=abs(f.get('gEcatPulseDiag','lastPhaseStep',signed=True))
 assert after<velocity
 assert f.obj('objStatusWord') & 0x800
 # Contact bounce/changed target/retained enable must never cancel the stop.
 f.gpio(0);f.obj('objTargetPosition',-sign*100000);f.obj('objTargetVelocity',-sign*1000);f.update()
 f.tick(400)
 assert f.read('ErrNumFlag',1)==15
 assert f.read('gDriveFaultReason',2)==(11 if sign<0 else 12)
 assert dg(f,'limitStopping')==0 and dg(f,'restartInhibit')==1 and dg(f,'limitTrips')==1
 assert f.obj('objErrorCode')==(0xff01 if sign<0 else 0xff02)
 assert f.obj('objStatusWord') & 0x800
 distance=(pos(f)-before)*sign
 assert 0<=distance<=2800,(mode,sign,distance)
 stopped=pos(f);f.tick(50);assert pos(f)==stopped
for mode in (1,3,8,9):
 for sign in (-1,1):test(f'mode {mode} direction {sign}: 6085 brake, bounce-resistant latch, vendor error and no automatic reverse',lambda f,m=mode,s=sign:stopping(f,m,s))

def away(f,mode,sign):
 f.gpio(2 if sign>0 else 4)
 prep(f,mode,sign)
 assert not f.read('ErrNumFlag',1) and dg(f,'limitTrips')==0
 assert pos(f)*sign>0
for mode in (1,3,8,9):
 for sign in (-1,1):test(f'mode {mode} direction {sign}: may leave the opposite active limit',lambda f,m=mode,s=sign:away(f,m,s))

def start_into(f,mode,sign):
 f.gpio(2 if sign<0 else 4)
 f.write('nAlStatus',8,1);f.put('LocalAxes',1,'bAxisIsActive');f.put('LocalAxes',0x10,'i16State')
 f.put('LocalAxes',2000,'u32CycleTime');f.obj('objControlWord',15);f.obj('objModesOfOperation',mode)
 f.obj('objTargetVelocity',sign*1000);f.obj('objTargetPosition',sign*100000)
 f.update();f.update()
 if mode==1:f.obj('objControlWord',63);f.update()
 f.tick(2)
 assert pos(f)==0 and f.read('ErrNumFlag',1)==15
for mode in (1,3,8,9):
 for sign in (-1,1):test(f'mode {mode} direction {sign}: initial active limit blocks first motion',lambda f,m=mode,s=sign:start_into(f,m,s))

def both(f):
 prep(f,8,1);before=pos(f)
 f.gpio(6);f.tick(1)
 assert f.read('gDriveFaultReason',2)==13 and f.obj('objErrorCode')==0xff03
 assert before==pos(f) and dg(f,'restartInhibit')==1
 assert f.call('DriveSafety_ResetBlockMask') & 0x200
 f.gpio(2);assert not(f.call('DriveSafety_ResetBlockMask') & 0x200)
test('contradictory two limits stop immediately and block reset until one is released',both)

def reverse(f):
 f.gpio(2);prep(f,8,1)
 # Departing X2 is legal; reversing the CSP trajectory must not emit any
 # negative phase step, including a one-cycle sign crossing in the filter.
 f.obj('objTargetPosition',-100000);f.update()
 for i in range(1600):
  f.call(Motor.runner_entry,1,count=2000000)
  assert f.get('gEcatPulseDiag','lastPhaseStep',signed=True)>=0,i
  if f.read('ErrNumFlag',1):break
 assert f.read('ErrNumFlag',1)==15 and f.read('gDriveFaultReason',2)==11
 test_count=i;assert test_count<1600
test('CSP departure then reversal cannot emit its first step back into an active limit',reverse)

def quick_rate(f):
 assert f.sdo(0x6085,6000,write=True)[0]==0
 prep(f,9,1);before=pos(f);f.gpio(4);f.tick(170)
 assert f.read('ErrNumFlag',1)==15
 assert 0<=pos(f)-before<=1400
test('limit brake follows 6085 independently of 6084',quick_rate)
(ROOT/'validation/limit_result.json').write_text(json.dumps({'passed':passed,'cases':cases,'hardware_operated':False},indent=2),'utf8')
print(f'PASS {passed} compiled ARM directional-limit tests.')
