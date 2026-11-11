"""Execute delivered PLC MAIN ST against the linked HOME ARM image (offline).
CiA402 enabled-state low status bits and bus inputs are a fixture; actual HM,
GPIO read functions, phase counts and HM upper status bits execute on the ARM.
"""
from pathlib import Path
import importlib.util
import json
import sys
ROOT=Path(__file__).resolve().parents[1]
exec(compile((ROOT/'tests/verify_homing.py').read_text('utf8').split('\ndef defaults(')[0],'arm_fixture','exec'))
BUILD=ROOT/'tests/.plc_home_build'
BUILD.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'plc_tests'))
spec=importlib.util.spec_from_file_location('delivered_plc_tests',ROOT/'plc_tests/verify.py')
plc=importlib.util.module_from_spec(spec);spec.loader.exec_module(plc)
passed=0

def exchange(f,b,n=1):
    for _ in range(n):
        b.io['iStatusWord']=f.obj('objStatusWord')
        b.io['iActualPosition']=f.obj('objPositionActualValue')
        b.io['iActualVelocity']=f.obj('objVelocityActualValue')
        b.io['iModeDisplay']=f.obj('objModesOfOperationDisplay')
        b.scan()
        f.obj('objControlWord',b.io['qControlWord'])
        f.obj('objModesOfOperation',b.io['qMode'])
        f.obj('objTargetPosition',b.io['qTargetPosition'])
        f.obj('objTargetVelocity',b.io['qTargetVelocity'])
        f.tick(2)

def fixture(f,method=21,initial=False):
    f.gpio(1 if initial else 0);f.enable(method);f.obj('objStatusWord',0x427)
    b=plc.Bench();b.enable()
    assert b.g['nInterfaceRevision']==2
    b.g['bStart']=True
    return b

def cycle(method,initial):
    f=Motor()
    try:
        b=fixture(f,method,initial)
        exchange(f,b,10)
        if initial==(method in (20,22)):
            assert f.diag('state')==1
            f.gpio(0 if initial else 1);exchange(f,b,15)
        assert f.diag('state')==3 and b.g['bBusy']
        f.gpio(1 if method in (20,22) else 0);exchange(f,b,10)
        assert b.g['bDone'] and not b.g['bError'],b.g
        assert f.diag('state')==5 and f.diag('error')==0
        assert abs(b.g['nActualPosition'])<=1
        b.g['bStart']=True;exchange(f,b,20)
        assert f.diag('startCount')==1
    finally:f.close()
for m in (19,20,21,22):
    for initial in (False,True):
        cycle(m,initial);passed+=1
        print('PASS delivered PLC + ARM: HOME %d initial %s -> bDone / one start'%(m,initial),flush=True)

for error in ('timeout','limit'):
    f=Motor()
    try:
        if error=='timeout':
            assert f.sdo(0x20E1,100,write=True)[0]==0
            b=fixture(f,21,True)
        else:
            b=fixture(f,21,False);f.gpio(2)
        exchange(f,b,75)
        assert f.diag('error')==(5 if error=='timeout' else 8)
        assert b.g['bError'] and b.g['nState']==90 and b.g['nError']==6,b.g
        assert b.io['qControlWord']==0 and f.call('EthercatMotorControl_PowerAllowed')==0
        passed+=1
        print('PASS delivered PLC + ARM: HOME '+error+' -> nError=6, disabled, no restart',flush=True)
    finally:f.close()
(ROOT/'validation/plc_arm_result.json').write_text(json.dumps({'passed':passed,'hardware_operated':False},indent=2),'utf8')
