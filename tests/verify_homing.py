"""Real ARM image, GPIO input registers and PhaseGet; no motor/peripheral timing."""
import ast
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests/.pulse_test_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
helpers = ast.parse((ROOT/'tests/verify_pulse_position.py').read_text('utf8'))
helpers.body = [n for n in helpers.body if isinstance(n,(ast.ClassDef,ast.FunctionDef))
                and n.name in ('ArmImage','unwrap_type','field')]
exec(compile(helpers,'helpers','exec'))
FW = ROOT/'program/FCM4E1553 Ethercat/MDK-ARM/N32G455/N32G455.axf'
BIN = Path(os.environ.get('KEIL_ARMCC_BIN','D:/App/Keil_v5/ARM/ARMCC/bin'))
BUILD = ROOT/('tests/.homing_build_'+str(os.getpid()))
BUILD.mkdir(exist_ok=True)
passed = 0

class Motor(ArmImage):
    locations = {}
    runner_image = None
    runner_entry = None
    def __init__(self):
        super().__init__(FW)
        self.uc.mem_map(0x40000000,0x100000)
        self.uc.mem_map(0xE0000000,0x100000)
        self.data=0x20030000
        self.write('SystemCoreClock',144000000)
        # Component fixture starts after the independently tested restart interlock.
        self.write('gDriveSafetyDiag',0,offset=8)
        self.gpio(0)
        assert self.call('COE_ObjDictionaryInit') == 0
        assert self.call('CiA402_Init') == 0
        self.call('EthercatMotorControl_Init')
        self.call('DriveCurrent_Init')
        self.write('nAlStatus',2,1)
        assert self.call('APPL_GenerateMapping', self.data+128, self.data+132)==0
        if Motor.runner_image is None:
            code='\n'.join('#define %s %s' % pair for pair in [
                ('begin','((void (*)(int))0x%Xu)'%self.symbols['EthercatMotorControl_BeginPhaseCycle']),
                ('pulse','((void (*)(void))0x%Xu)'%self.symbols['PulseFunction']),
                ('phase','((void (*)(int))0x%Xu)'%self.symbols['PhaseGet']),
                ('end','((void (*)(int))0x%Xu)'%self.symbols['EthercatMotorControl_EndPhaseCycle']),
                ('angle','(*(volatile int *)0x%Xu)'%self.symbols['Iangle']),
                ('step','(*(volatile int *)0x%Xu)'%self.symbols['BaseRpm'])])
            code+='\nunsigned runner(unsigned n){unsigned i;for(i=0;i<n;i++){begin(angle);pulse();phase(step);end(angle);}return 0;}\n'
            (BUILD/'runner.c').write_text(code,'ascii')
            subprocess.run([str(BIN/'armcc.exe'),'--cpu=Cortex-M4','--c99','-O2','--library_type=microlib',
                '-c',str(BUILD/'runner.c'),'-o',str(BUILD/'runner.o')],check=True)
            subprocess.run([str(BIN/'armlink.exe'),'--cpu=Cortex-M4','--library_type=microlib',
                '--entry=runner','--ro_base=0x08060000','--rw_base=0x20028000',
                '--libpath='+str(BIN.parent/'lib'),str(BUILD/'runner.o'),'-o',str(BUILD/'runner.axf')],check=True)
            with (BUILD/'runner.axf').open('rb') as stream:
                elf=ELFFile(stream)
                Motor.runner_image=[(s['sh_addr'],s.data()) for s in elf.iter_sections()
                                    if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS']
                Motor.runner_entry=next(s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name=='runner')
        for address,data in Motor.runner_image:
            self.uc.mem_write(address,data)
    def get(self,var,*names,signed=False):
        key=(var,)+names
        if key not in self.locations:self.locations[key]=field(self,var,*names)
        off,size=self.locations[key]
        return self.read(var,size,signed,off)
    def put(self,var,value,*names):
        key=(var,)+names
        if key not in self.locations:self.locations[key]=field(self,var,*names)
        off,size=self.locations[key]
        self.write(var,value,size,off)
    def obj(self,name,value=None):
        if value is not None:self.put('LocalAxes',value,'Objects',name)
        return self.get('LocalAxes','Objects',name)
    def diag(self,name):return self.get('gDriveHomingDiag',name)
    def gpio(self,mask):
        # X1..X4 are active-low GPIOA PA0..PA3, PID offset 8.
        self.uc.mem_write(0x40010808,struct.pack('<I',0xFFFF ^ (mask&15)))
    def sdo(self,index,value=0,sub=0,size=4,write=False,ca=0):
        handle=self.call('OBJ_GetObjectHandle',index)
        assert handle,hex(index)
        self.uc.mem_write(self.data,struct.pack('<I',value&0xFFFFFFFF))
        self.uc.mem_write(0x2003F000,struct.pack('<II',self.data,ca))
        ret=self.call('OBJ_Write' if write else 'OBJ_Read',index,sub,size,handle)
        self.uc.mem_write(0x2003F000,bytes(8))
        return ret,int.from_bytes(self.uc.mem_read(self.data,size),'little')
    def update(self):
        self.call('EthercatMotorControl_OnSyncCycle')
        self.call('EthercatMotorControl_Update')
    def enable(self,method=17,control=15):
        assert self.sdo(0x6098,method,size=1,write=True)[0]==0
        self.write('nAlStatus',8,1)
        self.put('LocalAxes',1,'bAxisIsActive')
        self.put('LocalAxes',0x10,'i16State')
        self.put('LocalAxes',2000,'u32CycleTime')
        self.obj('objModesOfOperation',6)
        self.obj('objControlWord',control)
        self.update();self.update()
        self.tick(5)
        assert self.read('gEcatMotorEnabled',1)==1
    def tick(self,ms=1):
        self.call(Motor.runner_entry,ms*16,count=1000000000)
        self.update()
    def start(self):
        self.obj('objControlWord',31)
        self.tick(2)

def test(name,body):
    global passed
    f=Motor()
    try:
        body(f)
        passed+=1
        print('PASS '+name,flush=True)
    finally:f.close()

def defaults(f):
    for idx,sub,size,value in [(0x6098,0,1,37),(0x6099,0,1,2),(0x6099,1,4,500),
        (0x6099,2,4,83),(0x609A,0,4,50000),(0x607C,0,4,0),
        (0x20E1,0,4,30000),(0x20E2,0,4,10000),(0x20E4,0,4,0x20261102),
        (0x20F1,0,2,3000),(0x6083,0,4,3000),(0x6084,0,4,3000),(0x6085,0,4,3000)]:
        assert f.sdo(idx,sub=sub,size=size)==(0,value),(hex(idx),sub)
    assert f.obj('objSupportedDriveModes')&0x20
test('defaults, standard homing objects and 3000 parameter defaults',defaults)

def gpio(f):
    for mask in range(16):
        f.gpio(mask)
        expected=((mask>>1)&1)|(((mask>>2)&1)<<1)|((mask&1)<<2)|(mask<<16)
        assert f.sdo(0x60FD)==(0,expected),(mask,f.sdo(0x60FD))
test('all 16 real GPIOA input combinations and 60FD mapping',gpio)

def access(f):
    for method in (17,18,19,20,21,22,37):assert f.sdo(0x6098,method,size=1,write=True)[0]==0
    for method in (0,1,16,23,30,35,255):assert f.sdo(0x6098,method,size=1,write=True)[0]!=0
    for idx,value in [(0x6099,0),(0x609A,0),(0x20E1,99),(0x20E1,600001),(0x20E2,0)]:
        assert f.sdo(idx,value,sub=1 if idx==0x6099 else 0,write=True)[0]!=0
    assert f.sdo(0x6099,500,sub=3,write=True)[0]!=0
    assert f.sdo(0x609A,50000,size=2,write=True)[0]!=0
    assert f.sdo(0x6099,500,sub=1,ca=1,write=True)[0]!=0
    f.enable()
    assert f.sdo(0x6098,18,size=1,write=True)[0]!=0
test('real SDO bounds, lengths, unsupported methods, CA and write locking',access)

def cycle(f,method=17,active=False):
    pin=2 if method==17 else 4
    if active:f.gpio(pin)
    f.enable(method)
    f.start()
    if not active:
        f.tick(30)
        assert f.diag('state')==1
        assert (f.read('BaseRpm',signed=True)<0)==(method==17)
        f.gpio(pin)
        for _ in range(20):
            f.tick()
            if f.diag('state')==3:break
    assert f.diag('state')==3,f.diag('state')
    f.tick(10)
    assert (f.read('BaseRpm',signed=True)>0)==(method==17)
    f.gpio(0)
    for _ in range(20):
        f.tick()
        if f.diag('state')==5:break
    assert f.diag('state')==5 and f.diag('error')==0
    assert f.read('BaseRpm',signed=True)==0
    assert f.obj('objStatusWord')&0x1400==0x1400
    before=f.read('Iangle',signed=True)
    position=f.get('gEcatPulseDiag','countedPosition',signed=True)
    f.tick(50)
    assert f.diag('startCount')==1 and f.read('Iangle',signed=True)==before
    assert abs(position)<=1,position
for method in (17,18):
    for active in (False,True):
        test('HM %d %s switch, reverse release, exact phase accounting and held-start nonrepeat'%
             (method,'active initial' if active else 'inactive initial'),
             lambda f,m=method,a=active:cycle(f,m,a))

def current(f):
    assert f.sdo(0x607C,123,write=True)[0]==0
    f.enable(37)
    angle=f.read('Iangle',signed=True)
    f.start();f.tick(5)
    assert f.diag('state')==5 and f.diag('error')==0
    assert f.get('gEcatPulseDiag','countedPosition',signed=True)==123
    assert f.read('Iangle',signed=True)==angle
test('method 37 offset rebases counts without moving Iangle',current)

def held(f):
    f.enable(control=31)
    f.tick(50)
    assert f.diag('startCount')==0 and f.read('BaseRpm',signed=True)==0
    f.obj('objControlWord',15);f.tick(2);f.start()
    assert f.diag('startCount')==1
test('held start on enable cannot start motion without a new low/high edge',held)

def error_case(f,scenario):
    if scenario=='timeout':assert f.sdo(0x20E1,100,write=True)[0]==0
    if scenario=='travel':assert f.sdo(0x20E2,1,write=True)[0]==0
    if scenario=='two_limits':f.gpio(6)
    f.enable();f.start()
    if scenario=='opposite':
        f.tick(20);f.gpio(2);f.tick(20);assert f.diag('state')==3;f.gpio(4)
    f.tick(150)
    assert f.diag('state')==7
    assert f.diag('error')=={'timeout':5,'travel':6,'two_limits':3,'opposite':4}[scenario]
    assert f.read('BaseRpm',signed=True)==0
    assert f.obj('objStatusWord')&0x2000 and not f.obj('objStatusWord')&0x1000
for scenario in ('timeout','travel','two_limits','opposite'):
    test('bounded homing failure: '+scenario,lambda f,s=scenario:error_case(f,s))

def cancel(f,halt=False):
    f.enable();f.start();f.tick(30)
    assert f.read('BaseRpm',signed=True)<0
    f.obj('objControlWord',0x11F if halt else 15)
    f.tick(30)
    assert f.read('BaseRpm',signed=True)==0 and f.diag('state')==0
    assert not f.obj('objStatusWord')&0x1000
test('bit 4 low cancels with deceleration, no false attained',cancel)
test('Halt cancels with deceleration, no automatic restart',lambda f:cancel(f,True))

def inhibit(f,kind):
    f.enable();f.start();f.tick(30)
    if kind=='op':f.write('nAlStatus',4,1)
    elif kind=='fault':
        f.write('ErrNumFlag',13,1)
        f.update()  # foreground fault handoff; full TIM8 inhibit covered by fault/current suites
    else:f.obj('objControlWord',0)
    f.tick(5)
    assert f.read('BaseRpm',signed=True)==0
    assert f.call('EthercatMotorControl_PowerAllowed')==0
for kind in ('op','fault','disable'):
    test('existing immediate power/motion inhibit: '+kind,lambda f,k=kind:inhibit(f,k))

def handoff(f):
    cycle(f)
    actual=f.get('gEcatPulseDiag','countedPosition',signed=True)
    f.obj('objControlWord',15);f.obj('objModesOfOperation',8);f.obj('objTargetPosition',actual)
    f.update();f.tick(2);f.update();f.tick(10)
    assert abs(f.get('gEcatPulseDiag','countedPosition',signed=True)-actual)<=1
    assert f.obj('objModesOfOperationDisplay')==8
test('HM to CSP with fresh actual-position target does not replay a stale target',handoff)


def rates(f):
    # HM acceleration is always counts/s², independent of legacy motion units.
    assert f.sdo(0x609A,1000,write=True)[0]==0  # 60 rpm/s
    assert f.sdo(0x2000,1,size=2,write=True)[0]==0
    f.enable();f.start();f.tick(98)
    # 100 ms at 60 rpm/s: approx 6 rpm in 1/16 rpm units.
    assert 92 <= abs(f.read('BaseRpm',signed=True)) <= 97
    f.tick(500)
    assert abs(f.read('BaseRpm',signed=True))==480
test('HM 609A fractional ramp keeps physical units independently of 2000',rates)

def moving(f):
    f.enable()
    f.write('sAppliedBaseRpm',160)
    f.put('gEcatPulseDiag',160,'lastPhaseStep')
    f.start();f.tick(20)
    assert f.diag('state')==7 and f.diag('error')==2
    assert f.read('BaseRpm',signed=True)==0
test('moving start is rejected and decelerates using initialized HM ramp',moving)

def bounce(f):
    f.enable();f.start();f.tick(30)
    f.gpio(2);f.tick(1);f.gpio(0);f.tick(5)
    assert f.diag('state')==1
    f.gpio(2);f.tick(20)
    assert f.diag('state')==3
test('short limit pulse is debounced, stable limit begins backoff',bounce)

def ro(f):
    for idx,size in [(0x60FD,4),(0x20E0,2),(0x20E3,2),(0x20E4,4)]:
        assert f.sdo(idx,1,size=size,write=True)[0]!=0
test('digital input and homing diagnostics cannot be written via SDO',ro)


def home_cycle(f, method, initial_on):
    if initial_on:f.gpio(1)
    f.enable(method);f.start()
    reference_dir={19:-1,20:1,21:1,22:-1}[method]
    target_on=method in (20,22)
    needs_search=(initial_on==target_on)
    if needs_search:
        f.tick(25)
        assert f.diag('state')==1
        assert (f.read('BaseRpm',signed=True)>0)==(reference_dir<0)
        f.gpio(0 if initial_on else 1)
        for _ in range(30):
            f.tick()
            if f.diag('state')==3:break
    assert f.diag('state')==3,(method,initial_on,f.diag('state'))
    f.tick(10)
    assert (f.read('BaseRpm',signed=True)>0)==(reference_dir>0)
    assert abs(f.read('BaseRpm',signed=True))==79
    f.gpio(1 if target_on else 0)
    for _ in range(15):
        f.tick()
        if f.diag('state')==5:break
    assert f.diag('state')==5 and f.diag('error')==0,(method,f.diag('state'),f.diag('error'))
    assert f.obj('objStatusWord')&0x1400==0x1400
    assert abs(f.get('gEcatPulseDiag','countedPosition',signed=True))<=1
    angle=f.read('Iangle',signed=True)
    f.tick(25)
    assert f.diag('startCount')==1 and f.read('Iangle',signed=True)==angle
for method in (19,20,21,22):
    for initial in (False,True):
        test('HOME %d initial %s: correct speed/direction, selected edge, hold and counted reference'%
             (method,'ON' if initial else 'OFF'),lambda f,m=method,i=initial:home_cycle(f,m,i))

def home_bounce(f, entering=False):
    f.enable(20 if entering else 21);f.start()
    f.gpio(1);f.tick(1);f.gpio(0);f.tick(5)
    assert f.diag('state')==(3 if entering else 1)
    if not entering:
        f.gpio(1);f.tick(20);assert f.diag('state')==3
        f.gpio(0);f.tick(1);f.gpio(1);f.tick(5);assert f.diag('state')==3
        f.gpio(0)
    else:f.gpio(1)
    f.tick(10)
    assert f.diag('state')==5
test('HOME X1 rising and falling short pulses are debounced',home_bounce)
test('HOME entry-reference short pulse cannot falsely complete',lambda f:home_bounce(f,True))

def home_limit(f, method, initial):
    ref={19:-1,20:1,21:1,22:-1}[method]
    target=method in (20,22)
    direction=-ref if initial==target else ref
    pin=2 if direction<0 else 4
    f.gpio((1 if initial else 0)|pin)
    f.enable(method);f.start();f.tick(10)
    assert f.diag('error')==(8 if direction<0 else 9)
    assert f.diag('state')==7 and f.read('BaseRpm',signed=True)==0
    assert f.get('gEcatPulseDiag','countedPosition',signed=True)==0
for method in (19,20,21,22):
    for initial in (False,True):
        test('HOME %d initial %s stops before moving into its active directional limit'%
             (method,initial),lambda f,m=method,i=initial:home_limit(f,m,i))

def away_limit(f):
    f.gpio(3)  # HOME and negative limit active; method 21 moves positive away.
    f.enable(21);f.start();f.tick(20)
    assert f.diag('state')==3 and f.diag('error')==0
    assert f.read('BaseRpm',signed=True)>0
    f.gpio(2);f.tick(10)
    assert f.diag('state')==5 and f.diag('error')==0
test('HOME may move away from an active opposite-direction limit',away_limit)

def home_bound(f, travel=False):
    assert f.sdo(0x20E2 if travel else 0x20E1,1 if travel else 100,write=True)[0]==0
    f.gpio(1);f.enable(21);f.start();f.tick(150)
    assert f.diag('state')==7 and f.diag('error')==(6 if travel else 5)
    assert f.read('BaseRpm',signed=True)==0
test('HOME held ON produces the existing bounded timeout error 5',home_bound)
test('HOME reference move retains the maximum-excursion protection',lambda f:home_bound(f,True))

def lost_window(f):
    f.enable(21);f.start();f.tick(30)
    f.gpio(1);f.tick(3)
    assert f.diag('state')==2
    f.gpio(0);f.tick(30)
    assert f.diag('error')==10 and f.diag('state')==7
    assert not f.obj('objStatusWord')&0x1000
test('HOME pulse lost during search braking cannot select the wrong reference edge',lost_window)

def home_offset(f):
    assert f.sdo(0x607C,-123,write=True)[0]==0
    f.gpio(1);f.enable(21);f.start();f.tick(20);f.gpio(0);f.tick(10)
    assert f.diag('state')==5 and abs(f.get('gEcatPulseDiag','countedPosition',signed=True)+123)<=1
test('HOME release reference applies signed 607C without replaying excitation',home_offset)


def reversal_limit(f, method):
    # Search is allowed away from this active limit; reversal into it must not
    # emit even a single phase step before the next 1 kHz input check.
    pin=4 if method==21 else 2
    f.gpio(pin);f.enable(method);f.start();f.tick(30)
    f.gpio(pin|1);f.tick(35)
    assert f.diag('error')==(9 if method==21 else 8)
    assert f.diag('state')==7
    field_name='positiveStepCycles' if method==21 else 'negativeStepCycles'
    assert f.get('gEcatPulseDiag',field_name)==0
test('HOME 21 reversal cannot emit a positive step into an already active X3',
     lambda f:reversal_limit(f,21))
test('HOME 19 reversal cannot emit a negative step into an already active X2',
     lambda f:reversal_limit(f,19))

result={'passed':passed,'firmware':str(FW),'hardware_operated':False}
(ROOT/'validation/homing_result.json').write_text(json.dumps(result,indent=2),'utf8')
print(str(passed)+' homing ARM tests passed; no hardware was operated.',flush=True)
