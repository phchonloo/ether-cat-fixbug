"""Execute the new adapter AND the original library PI in the linked ARM image.

Only peripheral functions and ADC inputs are mocked; the Q12 table is generated
once by the real library initializer and reused between isolated fixtures.
these tests prove code paths/math, NOT physical current accuracy, PI stability,
ISR wall time, power-stage protection or suitability for a suspended load.
"""
import math
from pathlib import Path

exec(compile(Path(__file__).with_name('verify_pdi_faults.py').read_text('utf8')
             .split('\npassed = 0')[0], 'current_fixture', 'exec'))
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC

passed = 0
sine_table = None


def obj(f, name, value=None):
    if value is not None:
        f.put('LocalAxes', value, 'Objects', name)
    return f.get('LocalAxes', 'Objects', name)


def diag(f, name):
    return f.get('gDriveCurrentDiag', name)


def cfg(f, name, value):
    f.put('gDriveCurrentConfig', value, name)


def update(f):
    f.call('EthercatMotorControl_OnSyncCycle')
    f.call('EthercatMotorControl_Update')


def reset(f):
    global sine_table
    f.write('SystemCoreClock', 144000000)
    assert f.call('COE_ObjDictionaryInit') == 0
    assert f.call('CiA402_Init') == 0
    f.call('EthercatMotorControl_Init')
    f.call('DriveTelemetry_Init')
    f.call('DriveCurrent_Init')
    cfg(f, 'runPeakMa', 1000)  # Explicit scenario current; factory defaults tested separately.
    f.write('nAlStatus', 2, 1)
    assert f.call('APPL_GenerateMapping', f.data + 128, f.data + 132) == 0
    assert bytes(f.uc.mem_read(f.data + 128, 6)) == b'\x0c\x00\xa5\xa5\x0c\x00'
    f.write('ready_ad', 1, 1)
    f.write('EnaFlag', 0, 1)
    f.write('current_base1', 2068)
    f.write('current_base2', 2068)
    f.uc.mem_write(f.symbols['adc_value'], struct.pack('<10I',
                  2068,2068,584,0,2068,2068,2068,2068,2068,2068))
    if sine_table is None:
        f.uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
        f.uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        f.call('shuzu_initial', count=2000000)
        sine_table = bytes(f.uc.mem_read(f.symbols['sin_tab'], 601*2))
        table = struct.unpack('<601h', sine_table)
        assert table[0] == 0 and 4094 <= table[599] <= 4096
        assert all(abs(a-int(4096*math.sin(i*math.pi/1200))) <= 1
                   for i,a in enumerate(table))
    f.uc.mem_write(f.symbols['sin_tab'], sine_table)
    f.pwm = [0, 0, 0, 0]
    for i in range(4):
        f.hook('TIM_SetCmp' + str(i+1),
               lambda timer, val, *_, k=i: f.pwm.__setitem__(k, val))
    f.hook('IoVfoGet', lambda *_: 1)  # external fault pin healthy
    f.hook('LedErrSet', lambda *_: 0)  # red LED GPIO is a mocked peripheral
    f.hook('TIM_GetIntStatus', lambda *_: 1)
    f.hook('TIM_ClrIntPendingBit', lambda *_: 0)
    f.write('PwmFlag', 1, 1)


def enable(f, speed=0, mode=3):
    f.write('nAlStatus', 8, 1)
    f.put('LocalAxes', 1, 'bAxisIsActive')
    f.put('LocalAxes', 0x10, 'i16State')
    f.put('LocalAxes', 2000, 'u32CycleTime')
    obj(f, 'objControlWord', 15)
    obj(f, 'objModesOfOperation', mode)
    obj(f, 'objProfileAcceleration', 10000)
    obj(f, 'objTargetVelocity', speed)
    update(f)
    update(f)
    assert f.read('gEcatMotorEnabled', 1) == 1


def tick(f, n=1):
    # Entire real TIM8 ISR -> MainFunt -> PulseFunction -> PhaseGet ->
    # linked CurrentCal adapter -> original PI -> MotorWork -> UpPwm.
    for _ in range(n):
        f.call('TIM8_UP_IRQHandler')


def sdo(f, index, value=0, size=2, sub=0, ca=0, write=False):
    handle = f.call('OBJ_GetObjectHandle', index)
    assert handle
    f.uc.mem_write(f.data, struct.pack('<I', value & 0xffffffff))
    f.uc.mem_write(0x2003F000, struct.pack('<II', f.data, ca))
    ret = f.call('OBJ_Write' if write else 'OBJ_Read', index, sub, size, handle)
    f.uc.mem_write(0x2003F000, bytes(8))
    return ret, int.from_bytes(f.uc.mem_read(f.data, size), 'little')


def test(name, fn):
    global passed
    f = Fixture()
    try:
        reset(f)
        fn(f)
        passed += 1
        print('PASS ' + name, flush=True)
    finally:
        f.close()


def defaults(f):
    f.call('DriveCurrent_Init')
    for idx, value in ((0x20F1,3000),(0x20D5,100),(0x20F5,200)):
        assert sdo(f, idx) == (0,value)
    assert sdo(f, 0x6075, size=4) == (0,3000)
    for index in (0x6083, 0x6084, 0x6085):
        assert sdo(f, index, size=4) == (0,3000)
    # Changing motion units preserves the new physical 3000 rpm/s defaults.
    assert sdo(f, 0x2000, 1, write=True)[0] == 0
    for index in (0x6083, 0x6084, 0x6085):
        assert sdo(f, index, size=4) == (0, f.get('gEcatPulseDiag','resolution') * 50)
    assert sdo(f, 0x2000, 0, write=True)[0] == 0
    for index in (0x6083, 0x6084, 0x6085):
        assert sdo(f, index, size=4) == (0,3000)
    assert diag(f, 'version') == 0x20261020
    assert diag(f, 'state') == 0
    assert f.call('EthercatMotorControl_PowerAllowed') == 0
    cfg(f, 'runPeakMa', 2000)
    f.call('DriveCurrent_Init')
    assert sdo(f, 0x20F1) == (0,3000)
test('defaults and reset: 3 A peak / 100 percent / 200 ms; no normal enable', defaults)


def access(f):
    for idx, accepted, rejected in (
        (0x20F1, (0,100,500,3000), (1,99,3001,6000,65535)),
        (0x20D5, (10,50,100), (0,9,101,120,65535)),
        (0x20F5, (1,200,30000), (0,30001,65535))):
        for value in accepted:
            assert sdo(f, idx, value, write=True)[0] == 0
            assert sdo(f, idx) == (0,value)
        old = sdo(f, idx)
        for value in rejected:
            assert sdo(f, idx, value, write=True)[0] != 0
            assert sdo(f, idx) == old
        for kwargs in ({'size':1},{'size':4},{'sub':1},{'ca':1}):
            assert sdo(f, idx, 100, write=True, **kwargs)[0] != 0
            assert sdo(f, idx) == old
test('real CoE access: parameter limits, signed negatives, lengths, subindex and CA', access)


def states(f):
    for al in (1,4,8):
        f.write('nAlStatus', al, 1)
        for idx in (0x20F1,0x20D5,0x20F5):
            assert sdo(f, idx, 100, write=True)[0] != 0
    f.write('nAlStatus', 2, 1)
    f.write('gEcatMotorEnabled', 1, 1)
    assert sdo(f, 0x20F1, 100, write=True)[0] != 0
    f.write('gEcatMotorEnabled', 0, 1)
    f.put('gEcatPulseDiag', 1, 'lastPhaseStep')
    assert sdo(f, 0x20F1, 100, write=True)[0] != 0
test('writes require PREOP, disabled and no emitted motion', states)


def actual_path(f, ma):
    assert sdo(f, 0x20F1, ma, write=True)[0] == 0
    enable(f)
    tick(f)
    assert diag(f, 'adapterCalls') == 1, 'library call bypassed the adapter'
    assert diag(f, 'referencePeakMa') == ma
    refs = [f.read('AlphaRef', signed=True), f.read('BeitaRef', signed=True)]
    peak = ma * 200000 * 8190 / (3300 * 3000000)
    assert abs(math.hypot(*refs) - peak) < 2, (refs, peak)
    assert any(f.pwm) and max(f.pwm) <= 2250
    assert f.read('Igive', signed=True) == 20  # library still called, not replaced
    assert f.read('WorkValue', offset=0) == 20
    assert f.read('TimeStop', signed=True) == 1
    assert f.read('LockFlag', 1) == 0
for ma in (100,500,1000,2000,3000):
    test('real ISR/library PI/PWM receives ' + str(ma) + ' mA nominal peak',
         lambda f, ma=ma: actual_path(f, ma))


def hold(f):
    cfg(f, 'holdPercent', 50)  # explicitly test optional reduction; boot default is 100
    enable(f)
    tick(f,1021)
    assert diag(f, 'state') == 2 and diag(f, 'referencePeakMa') == 1000
    assert f.read('LockFlag',1) == 0  # no second hard-coded idle reduction
    tick(f,2178)
    assert diag(f, 'stationaryTicks') == 3199 and diag(f, 'state') == 2
    tick(f)
    assert diag(f, 'state') == 3 and diag(f, 'referencePeakMa') == 500
    assert abs(math.hypot(f.read('AlphaRef',signed=True),
                         f.read('BeitaRef',signed=True)) - 500*8190*200000/9900000000) < 2
    obj(f, 'objTargetVelocity', 60)
    update(f)
    tick(f)
    assert diag(f, 'state') == 1 and diag(f, 'stationaryTicks') == 0
    assert diag(f, 'referencePeakMa') == 1000
    assert f.read('BaseRpm',signed=True) > 0
test('200 ms hold boundary, no legacy 63.75 ms drop; first moving cycle restores run', hold)


def disconnected_motor(f):
    f.call('DriveFault_Init')
    f.put('gDriveFaultConfig',2,'currentEnable')
    cfg(f,'holdPercent',100)
    f.hook('DMA_GetCurrDataCounter',lambda *_:10)
    enable(f)
    for i in range(1,12001):
        f.uc.mem_write(0xE0001004,struct.pack('<I',i*9000))
        if i % 16 == 0:
            f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                2042,2048,587,0,2042,2048,2042,2048,2042,2048))
            f.call('DriveTelemetry_AdcComplete')
        tick(f)
        if i % 128 == 0:  # mailbox-loaded foreground: 8 ms between updates
            f.call('DriveTelemetry_Update')
            f.call('DriveFault_Update')
            f.call('EthercatMotorControl_Update')
        if f.read('ErrNumFlag',1): break
    assert f.read('ErrNumFlag',1) == 11, i
    assert f.read('gDriveFaultReason',2) in (3,4)
    assert 11000 <= i <= 11400, i  # 200 ms hold + 300 ms settle + 200 ms confirm
    assert f.pwm == [0]*4
    f.call('EthercatMotorControl_Update')
    assert obj(f,'objErrorCode') == 0x3321
test('actual ISR, library reference and raw ADC detect absent motor despite 8 ms foreground',disconnected_motor)


def hold_bounds(f):
    cfg(f,'holdDelayMs',1)
    cfg(f,'holdPercent',10)
    enable(f)
    tick(f,15)
    assert diag(f,'state') == 2
    tick(f)
    assert diag(f,'state') == 3 and diag(f,'referencePeakMa') == 100
    # Accelerate only the waiting counter for the 30 s boundary test.
    cfg(f,'holdDelayMs',30000)
    cfg(f,'holdPercent',100)
    f.put('gDriveCurrentDiag',479998,'stationaryTicks')
    tick(f)
    assert diag(f,'state') == 2
    tick(f)
    assert diag(f,'state') == 3 and diag(f,'referencePeakMa') == 1000
test('hold delay and percent boundaries do not overflow or shut off holding current', hold_bounds)


def normalize_only(f):
    enable(f)
    tick(f)
    original = (f.read('AlphaRef'),f.read('BeitaRef'))
    for ma in (0,200,3000,10000):
        f.put('gDriveTelemetryConfig',ma,'ratedCurrentMa')
        tick(f)
        assert (f.read('AlphaRef'),f.read('BeitaRef')) == original
    assert diag(f,'referencePeakMa') == 1000
test('6075 affects no current-loop reference, even when zero', normalize_only)


def feedback(f):
    enable(f)
    tick(f)
    first = list(f.pwm)
    refs = [f.read('AlphaRef',signed=True),f.read('BeitaRef',signed=True)]
    a,b = [2068 + r for r in refs]
    f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',a,b,584,0,a,b,a,b,a,b))
    tick(f)
    assert f.read('EssAlphaIn',signed=True) == 0
    assert f.read('EssBeitaIn',signed=True) == 0
    assert f.pwm != first  # real PI consumes the sensor ADC, not fabricated current
test('original PI reacts to measured phase ADC error, not telemetry/command feedback', feedback)


def signed(f):
    enable(f,60)
    signs = set()
    for _ in range(800):
        tick(f)
        a,b = f.read('AlphaRef',signed=True),f.read('BeitaRef',signed=True)
        signs.add((a>=0,b>=0))
        # Real table/phase generator must cover each quadrant without excess.
        assert abs(a) <= 166 and abs(b) <= 166
        assert max(f.pwm) <= 2250
    assert len(signs) == 4, signs
    assert diag(f,'state') == 1
test('all phase quadrants retain signs and bounded references/PWM', signed)


def inhibit(f, cause):
    enable(f,60)
    tick(f,20)
    assert any(f.pwm)
    if cause == 'zero':
        cfg(f,'runPeakMa',0)
    elif cause == 'gain':
        f.put('gDriveTelemetryConfig',0,'sensitivityUvA')
    elif cause == 'headroom':
        f.put('gDriveTelemetryConfig',1000000,'sensitivityUvA')
        cfg(f,'runPeakMa',3000)
    elif cause == 'fault':
        f.put('gEscPdiDiag',1,'faultLatched')
    elif cause == 'hardware':
        f.write('ErrNumFlag',10,1)
    elif cause == 'al':
        f.write('nAlStatus',4,1)
    else:
        obj(f,'objControlWord',0)
    angle = f.read('Iangle')
    tick(f)
    assert f.call('EthercatMotorControl_PowerAllowed') == 0
    assert f.pwm == [0,0,0,0], f.pwm
    assert diag(f,'state') == 0 and diag(f,'referencePeakMa') == 0
    assert f.read('Iangle') == angle
    for name in ('EssAlphaIn','EssAlphaOut','EssBeitaIn','EssBeitaOut'):
        assert f.read(name) == f.read(name,offset=4) == 0
for cause in ('zero','gain','headroom','fault','hardware','al','disable'):
    test('power inhibit clears PI and PWM without phase emission: '+cause,
         lambda f,c=cause: inhibit(f,c))


def csp_pending(f):
    cfg(f,'holdDelayMs',1)
    enable(f,mode=8)
    tick(f,16)
    assert diag(f,'state') == 3
    # A new CSP target must restore run current immediately. The low-lag
    # smoother may emit its first fractional phase step in this same tick.
    obj(f,'objProfileAcceleration',1)
    obj(f,'objTargetPosition',1)
    update(f)
    tick(f)
    assert abs(f.read('BaseRpm',signed=True)) <= 16000
    assert diag(f,'state') == 1 and diag(f,'referencePeakMa') == 1000
test('pending CSP motion restores full current on its first tick', csp_pending)


def calibration(f):
    for mv,gain in ((3300,100000),(3300,400000),(3000,200000)):
        f.write('nAlStatus',2,1)
        f.write('gEcatMotorEnabled',0,1)
        assert sdo(f,0x2010,mv,size=4,write=True)[0] == 0
        assert sdo(f,0x2011,gain,size=4,write=True)[0] == 0
        enable(f)
        tick(f)
        actual = math.hypot(f.read('AlphaRef',signed=True),f.read('BeitaRef',signed=True))
        assert abs(actual-1000*gain*8190/(mv*3000000)) < 2
    refs = (f.read('AlphaRef'),f.read('BeitaRef'))
    f.put('gDriveTelemetryConfig',1500000,'zeroAUv')
    f.put('gDriveTelemetryConfig',1700000,'zeroBUv')
    tick(f)
    assert (f.read('AlphaRef'),f.read('BeitaRef')) == refs
    assert f.read('current_base1') == f.read('current_base2') == 2068
test('VDDA/gain update cached control scaling; display zeros do not alter PI auto-zero', calibration)


def readiness(f, flag, value):
    enable(f)
    tick(f)
    assert any(f.pwm)
    f.write(flag,value,1)
    angle = f.read('Iangle')
    tick(f)
    assert f.pwm == [0,0,0,0] and f.read('Iangle') == angle
    assert diag(f,'state') == 0 and diag(f,'referencePeakMa') == 0
    assert f.read('EssAlphaOut') == f.read('EssBeitaOut') == 0
for flag,value in (('ready_ad',0),('EnaFlag',1)):
    test('library inhibit clears current PI/output: '+flag,
         lambda f,flag=flag,value=value: readiness(f,flag,value))

print(str(passed)+' current-control ARM tests passed (offline only).')
