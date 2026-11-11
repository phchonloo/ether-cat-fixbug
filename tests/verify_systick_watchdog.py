"""Execute the actual linked ARM timer setup, IRQs, delays and watchdog gates.
Peripheral time/interrupt arrival are simulated; this is not an on-board test.
"""
from pathlib import Path
import ast,json,struct,sys
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'tests/.pulse_test_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
source=ast.parse((R/'tests/verify_pulse_position.py').read_text('utf8'))
exec(compile(ast.Module(body=[n for n in source.body if isinstance(n,(ast.ClassDef,ast.FunctionDef)) and n.name in ('ArmImage','unwrap_type','field')],type_ignores=[]),'arm_helpers','exec'))
NEW=R/'FCM_Protection_SysTickFix_20261103.axf'
OLD=R.parent/'15_FCM_Protection_20261102/FCM_Protection_20261102.axf'
CTRL=0xE000E010;LOAD=CTRL+4;VAL=CTRL+8;CYCCNT=0xE0001004
class Image(ArmImage):
 def __init__(self,path):
  super().__init__(path)
  self.uc.mem_map(0x40000000,0x100000)
  self.uc.mem_map(0x42000000,0x200000)
  self.uc.mem_map(0xE0000000,0x100000)
  self.write('SystemCoreClock',144000000)
 def word(self,addr,value=None):
  if value is not None:self.uc.mem_write(addr,struct.pack('<I',value&0xffffffff))
  return int.from_bytes(self.uc.mem_read(addr,4),'little')
 def diag(self,name):
  off,size=field(self,'gDriveSafetyDiag',name)
  return self.read('gDriveSafetyDiag',size,offset=off)
results=[]
def test(name,fn):
 f=Image(NEW)
 try:fn(f);results.append(name);print('PASS '+name,flush=True)
 finally:f.close()
old_result={}
if OLD.exists():
 f=Image(OLD)
 try:
  f.call('systick_config')
  old_result={'ctrl':f.word(CTRL),'load':f.word(LOAD),'periodic_irq_enabled':bool(f.word(CTRL)&3==3)}
  assert f.word(CTRL)&3==0,old_result
  print('PASS old 15 reproduction: real systick_config leaves ENABLE/TICKINT clear',flush=True)
 finally:f.close()
def setup(f):
 f.call('systick_config')
 assert f.word(CTRL)&7==7 and f.word(LOAD)==143999
 assert f.word(0xE000EDFC)&0x1000000 and f.word(0xE0001000)&1
 assert f.word(0x08000000+15*4)==f.symbols['SysTick_Handler']|1
 priority=f.uc.mem_read(0xE000ED23,1)[0]
 assert priority==0xf0,hex(priority)
 before=f.diag('systickTicks');f.call('SysTick_Handler')
 assert f.diag('systickTicks')==before+1
 # Both main() clock-configuration calls can safely repeat initialization.
 f.call('systick_config');assert f.word(LOAD)==143999 and f.word(CTRL)&7==7
test('1 ms periodic SysTick, lowest priority, actual vector and handler heartbeat',setup)
def delays(f):
 f.call('systick_config');before=bytes(f.uc.mem_read(CTRL,12));writes=[];reads=[0]
 def cycle(uc,access,address,size,value,data):
  if address<=CYCCNT<address+size:
   reads[0]+=1;f.word(CYCCNT,f.word(CYCCNT)+14400)
 def write(uc,access,address,size,value,data):writes.append((address,value))
 h=f.uc.hook_add(UC_HOOK_MEM_READ,cycle,begin=CYCCNT,end=CYCCNT+3)
 w=f.uc.hook_add(UC_HOOK_MEM_WRITE,write,begin=CTRL,end=VAL+3)
 for name,arg in [('delay_us',0),('delay_ms',0),('delay_us',2500),('delay_ms',3),('delay_xms',2),('delay_1ms',1),('delay_us',70000)]:
  f.word(CYCCNT,0xffffff00)
  f.call(name,arg,count=1000000)
 assert reads[0]>100 and not writes,writes
 assert bytes(f.uc.mem_read(CTRL,12))==before
 f.call('SysTick_Handler');assert f.diag('systickTicks')==1
 f.uc.hook_del(h);f.uc.hook_del(w)
test('zero, multi-chunk and wrapping delays never reconfigure or stop SysTick',delays)
def gate(f):
 f.call('systick_config');f.call('DriveSafety_Init');feeds=[]
 addr=f.symbols['IWDG_ReloadKey']&~1
 h=f.uc.hook_add(UC_HOOK_CODE,lambda uc,a,s,d:feeds.append(a),begin=addr,end=addr)
 f.call('DriveSafety_StartWatchdog');assert len(feeds)==1
 for _ in range(1000):
  f.call('SysTick_Handler');f.call('DriveSafety_MotorTick');f.call('DriveSafety_Update')
 assert len(feeds)==1001 and f.diag('watchdogFeeds')==1000
 assert f.diag('systickTicks')==1000 and f.diag('restartInhibit')==1
 for _ in range(10):f.call('DriveSafety_MotorTick');f.call('DriveSafety_Update')
 assert len(feeds)==1001 # missing SysTick
 f.call('SysTick_Handler');f.call('DriveSafety_Update');assert len(feeds)==1002
 for _ in range(10):f.call('SysTick_Handler');f.call('DriveSafety_Update')
 assert len(feeds)==1002 # missing motor IRQ progress
 for _ in range(10):f.call('SysTick_Handler');f.call('DriveSafety_MotorTick')
 assert len(feeds)==1002 # missing foreground completion
 assert f.word(0x40003000)&0xffff==0xaaaa
 f.uc.hook_del(h)
test('1000 real SysTick IRQ heartbeats feed watchdog; each stalled path blocks feeding',gate)
def faults(f):
 for slot,name in [(3,'HardFault_Handler'),(4,'MemManage_Handler'),(5,'BusFault_Handler'),(6,'UsageFault_Handler')]:
  assert f.word(0x08000000+slot*4)==f.symbols[name]|1
  f.word(0x40013444,0x8000)
  for a in (0x40013434,0x40013438,0x4001343c,0x40013440):f.word(a,1500)
  returns=[];called=[];target=f.symbols['DriveSafety_FatalOutputOff']&~1
  def hook(uc,a,size,data):
   if a==target:called.append(a);returns.append(uc.reg_read(UC_ARM_REG_LR)&~1)
   elif returns and a==returns[-1]:uc.emu_stop()
  h=f.uc.hook_add(UC_HOOK_CODE,hook)
  f.uc.reg_write(UC_ARM_REG_SP,0x2003f000);f.uc.reg_write(UC_ARM_REG_LR,f.stop|1)
  f.uc.emu_start(f.symbols[name]|1,f.stop,count=100000)
  f.uc.hook_del(h)
  assert called and not f.word(0x40013444)&0x8000,name
  assert all(f.word(a)==0 for a in (0x40013434,0x40013438,0x4001343c,0x40013440)),name
test('all four actual fatal vectors invoke immediate PWM output-off',faults)
(R/'validation/systick_watchdog_result.json').write_text(json.dumps({'passed':len(results),'cases':results,'old_15_observation':old_result,'firmware':NEW.name,'hardware_tested':False},indent=2),'utf8')
print('PASS %d timer/watchdog integration groups'%len(results))
