"""Actual compiled TIM8/PI/ADC boot test. Synthetic sensor data, no hardware.

Does not certify physical gain, zero stability, ISR deadlines, mechanical safety
or power-stage protection. The production main calls DriveBoot_Init explicitly.
"""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_drive_current.py').read_text('utf8')
             .split('\ndef defaults(')[0], 'boot_fixture', 'exec'))


def boot_setup(f):
    reset(f)
    f.call('DriveFault_Init')
    f.call('DriveBoot_Init')
    f.write('ready_ad', 0, 1)  # exercise original library power-off ADC startup
    f.hook('DMA_GetCurrDataCounter', lambda *_: 10)
    f.ms = 0
    f.any_pwm = False
    f.pwm_max = 0
    f.bus = 584


def boot_diag(f, name):
    return f.get('gDriveBootDiag', name)


def state(f):
    return f.read('gDriveBootState', 2)


def advance(f, count, mode='healthy', foreground=8, motion=False):
    for _ in range(count):
        f.ms += 1
        a, b = 2039, 2045
        if mode == 'noisy_zero' and state(f) == 2:
            a += 20 if f.ms % 2 else -20
        if mode == 'bad_zero': a = 2500
        if state(f) == 3:
            if mode == 'healthy': a += 117; b += 117
            elif mode == 'missing_a': b += 117
            elif mode == 'missing_b': a += 117
            elif mode == 'weak': a += 35; b += 35
            elif mode == 'overcurrent': a += 300; b += 117
        skip_frame = mode == 'dma_gap' and state(f) == 2 and f.ms % 5 == 0
        for sub in range(16):
            cycle = ((f.ms - 1) * 16 + sub + 1) * 9000
            f.uc.mem_write(0xE0001004, struct.pack('<I', cycle & 0xffffffff))
            if sub == 0 and not skip_frame and not (mode == 'stale' and state(f) == 3):
                f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                    a,b,f.bus,0,a,b,a,b,a,b))
                f.call('DriveTelemetry_AdcComplete')
            f.call('TIM8_UP_IRQHandler')
            if not motion and any(f.pwm):
                assert state(f) == 3, (state(f), f.pwm)
                f.any_pwm = True
                f.pwm_max = max(f.pwm_max, *f.pwm)
            if not motion:
                assert f.read('gEcatMotorEnabled',1) == 0
        if f.ms % foreground == 0:
            f.call('DriveTelemetry_Update')
            f.call('DriveFault_Update')
            f.call('EthercatMotorControl_Update')


passed=0
results=[]
def check(name,fn):
 global passed
 f=Fixture()
 try:
  boot_setup(f);fn(f);passed+=1;results.append(name);print('PASS '+name,flush=True)
 finally:f.close()
def healthy(f):
 angle=f.read('Iangle');advance(f,1250)
 assert state(f)==5 and boot_diag(f,'normalReleased')==1
 assert boot_diag(f,'zeroValid')==1 and boot_diag(f,'zeroSamples')==256
 assert f.read('current_base1')==2039 and f.read('current_base2')==2045
 assert not f.any_pwm and f.pwm==[0]*4 and f.read('Iangle')==angle
 assert f.call('DriveBoot_PowerAllowed')==0
check('cold boot calibrates zero without energizing either phase',healthy)
def held(f):
 obj(f,'objControlWord',15);advance(f,1250)
 assert state(f)==5 and not boot_diag(f,'normalReleased')
 assert not f.any_pwm and not f.call('EthercatMotorControl_PowerAllowed')
 obj(f,'objControlWord',0);advance(f,2)
 assert boot_diag(f,'normalReleased')==1 and f.pwm==[0]*4
check('retained enable cannot borrow calibration permission',held)
def bad(f,mode):
 advance(f,1000,mode)
 assert state(f)==6 and f.read('gDriveFaultReason',2)==9
 assert not f.any_pwm and not boot_diag(f,'zeroValid')
check('out-of-range ADC baseline fails with PWM off',lambda f:bad(f,'bad_zero'))
check('noisy ADC baseline fails with PWM off',lambda f:bad(f,'noisy_zero'))
def voltage(f,bus,code):
 f.bus=bus;advance(f,700)
 assert f.read('ErrNumFlag',1)==code and not f.any_pwm
 assert f.read('gDriveSafetyDiag',offset=8)==1
check('24 V startup under-voltage remains latched and disabled',lambda f:voltage(f,400,13))
check('24 V startup over-voltage remains latched and disabled',lambda f:voltage(f,800,14))


def undervoltage_recovery(f):
    led = []
    f.hook('LedErrSet', lambda level, *_: led.append(level))
    f.write('nAlStatus', 8, 1)
    f.write('nPdOutputSize', 12, 2)
    f.call('DriveSafety_RequireRearm')
    advance(f, 770)
    assert boot_diag(f, 'zeroValid')

    def send(control, velocity=0):
        payload = struct.pack('<HiiBB', control, 0, velocity, 3, 0)
        def read_pdo(dst, address, size, *_):
            assert size == len(payload)
            f.uc.mem_write(dst, payload)
            return 1
        f.hook('HW_EscReadIsr', read_pdo)
        f.call('PDO_OutputMapping')
        f.call('CiA402_StateMachine')
        f.call('EthercatMotorControl_Update')

    def start():
        for control in (6, 7, 15):
            send(control, 60)
        assert obj(f, 'objStatusWord') & 0x006F == 0x0027
        angle = f.read('Iangle')
        advance(f, 25, motion=True)
        assert any(f.pwm) and f.read('Iangle') != angle

    send(0)
    start()
    f.bus = 400
    advance(f, 10, motion=True)
    assert f.pwm == [0] * 4
    assert f.read('ErrNumFlag', 1) == 13 and led[-1] == 0
    # Fault must be reportable and resettable without CiA402_Application/Sync0.
    f.call('APPL_InputMapping', f.data)
    assert struct.unpack('<H', f.uc.mem_read(f.data, 2))[0] & 0x004F == 0x0008
    assert sdo(f, 0x603F) == (0, 0x3220)
    send(128)
    assert f.read('ErrNumFlag', 1) == 13
    f.bus = 584
    advance(f, 210)
    send(128)
    assert f.read('ErrNumFlag', 1) == 13  # A rejected edge cannot retry itself.
    send(0)
    send(128)
    assert sdo(f, 0x603F) == (0, 0)
    assert sdo(f, 0x206C) == (0, 0) and sdo(f, 0x2030) == (0, 0)
    assert obj(f, 'objStatusWord') & 0x006F == 0x0040
    send(15, 60)
    advance(f, 2)
    assert led[-1] == 1 and obj(f, 'objStatusWord') & 0x006F == 0x0040
    send(0)
    send(6)
    advance(f, 850, mode='dma_gap')
    assert boot_diag(f, 'zeroValid') and boot_diag(f, 'zeroSamples') == 256
    assert f.read('ErrNumFlag', 1) == 0 and led == [0, 1]
    start()


check('running motor: undervoltage PDO, reset edge, red LED clears and fresh motion after DMA gaps',
      undervoltage_recovery)
import json
(ROOT/'validation/boot_result.json').write_text(json.dumps({'passed':passed,'cases':results,'hardware_operated':False},indent=2),'utf8')
print(f'PASS {passed} unpowered-startup ARM tests.')
