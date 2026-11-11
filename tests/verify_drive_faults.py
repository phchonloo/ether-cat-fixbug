"""Execute linked ARM fault detectors, reset/SDO/PDO paths without hardware.

Algorithm tests supply coherent synthetic telemetry. A separate end-to-end case
executes real DMA capture/conversion/age checking. Not a physical protection test.
"""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_pdi_faults.py').read_text('utf8')
             .split('\npassed = 0')[0], 'fault_fixture', 'exec'))

passed = 0


def setup(f):
    f.write('SystemCoreClock', 144000000)
    assert f.call('COE_ObjDictionaryInit') == 0
    assert f.call('CiA402_Init') == 0
    f.call('EthercatMotorControl_Init')
    f.call('DriveTelemetry_Init')
    f.call('DriveCurrent_Init')
    f.call('DriveFault_Init')
    f.write('nAlStatus', 2, 1)
    assert f.call('APPL_GenerateMapping', f.data + 128, f.data + 132) == 0
    f.pwm = [99] * 4
    for i in range(4):
        f.hook('TIM_SetCmp' + str(i+1),
               lambda tim, val, *_, k=i: f.pwm.__setitem__(k, val))
    f.vfo = 1
    f.hook('IoVfoGet', lambda *_: f.vfo)
    f.led = []
    f.hook('LedErrSet', lambda level, *_: f.led.append(level))
    end, size = f.loc('gDriveTelemetry', ('commandPositionError',))
    f.hook('DriveTelemetry_GetSnapshot', lambda dest, *_:
           f.uc.mem_write(dest, bytes(f.uc.mem_read(f.symbols['gDriveTelemetry'], end + size))))
    def phase_sample(dest, *_):
        f.uc.mem_write(dest, struct.pack('<III',
            f.get('gDriveTelemetry','phaseAMa'), f.get('gDriveTelemetry','phaseBMa'),
            f.get('gDriveTelemetry','sampleCount')))
        return int(bool(f.get('gDriveTelemetry','status') & 2))
    f.hook('DriveTelemetry_GetPhaseSample', phase_sample)
    def safety_sample(dest, *_):
        f.uc.mem_write(dest, struct.pack('<6I2H',
            f.get('gDriveTelemetry','status'), f.get('gDriveTelemetry','sampleCount'),
            0, f.get('gDriveTelemetry','busVoltageMv'),
            f.get('gDriveTelemetry','phaseAMa'), f.get('gDriveTelemetry','phaseBMa'), 2068, 2068))
    f.hook('DriveTelemetry_GetSafetySample', safety_sample)
    f.clock = 599
    f.seq = 0
    f.put('gDriveTelemetry', 7, 'status')
    f.put('gDriveTelemetry', 24000, 'busVoltageMv')


def step(f, n=1, mv=24000, a=0, b=0, status=7, fresh=True, gap=1):
    for _ in range(n):
        f.clock = (f.clock + gap) & 0xffffffff
        f.write('sMs', f.clock)
        f.put('gDriveFaultDiag',f.clock,'nowMs')
        if fresh: f.seq += 1
        for key, val in (('status', status), ('busVoltageMv', mv),
                         ('phaseAMa', a), ('phaseBMa', b), ('sampleCount', f.seq)):
            f.put('gDriveTelemetry', val, key)
        f.call('DriveFault_PhaseTick')
        f.call('DriveFault_Update')


def diag(f, name):
    return f.get('gDriveFaultDiag', name)


def code(f):
    return f.read('ErrNumFlag', 1)


def cfg(f, name, val):
    f.put('gDriveFaultConfig', val, name)


def sdo(f, idx, val=0, sub=0, size=2, write=False, ca=0):
    h = f.call('OBJ_GetObjectHandle', idx)
    assert h, hex(idx)
    f.uc.mem_write(f.data, struct.pack('<I', val))
    f.uc.mem_write(0x2003F000, struct.pack('<II', f.data, ca))
    ret = f.call('OBJ_Write' if write else 'OBJ_Read', idx, sub, size, h)
    f.uc.mem_write(0x2003F000, bytes(8))
    return ret, int.from_bytes(f.uc.mem_read(f.data, size), 'little')


def test(name, fn):
    global passed
    if len(sys.argv) > 1 and sys.argv[1] not in name:
        return
    f = Fixture()
    try:
        setup(f)
        fn(f)
        passed += 1
        print('PASS ' + name, flush=True)
    finally:
        f.close()


def defaults(f):
    assert sdo(f, 0x2020) == (0,2)
    assert sdo(f, 0x2021) == (0,4000)
    for idx in (0x2030,0x2031,0x206C,0x603F):
        assert sdo(f, idx) == (0,0)
        assert sdo(f, idx, 1, write=True)[0] != 0
    step(f, 220)
    assert sdo(f, 0x2031) == (0,0)  # no latched motor fault to reset
    assert diag(f, 'version') == 0x01080001
    step(f, 600, a=8000, b=0)  # normal software OC remains OFF; phase requires enable
    assert code(f) == 0
test('defaults: voltage and stationary phase enabled; diagnostic objects are read-only', defaults)


def access(f):
    for idx, good, bad in ((0x2020,(0,1,2,3),(4,65535)),
                          (0x2021,(500,4000,6000),(0,499,6001,65535))):
        for val in good:
            assert sdo(f, idx, val, write=True)[0] == 0
        old = sdo(f, idx)
        for val in bad:
            assert sdo(f, idx, val, write=True)[0] != 0
            assert sdo(f, idx) == old
        for kw in ({'sub':1},{'size':1},{'size':4},{'ca':1}):
            assert sdo(f, idx, 0, write=True, **kw)[0] != 0
        for state in (1,4,8):
            f.write('nAlStatus', state, 1)
            assert sdo(f, idx, good[0], write=True)[0] != 0
        f.write('nAlStatus', 2, 1)
        f.write('gEcatMotorEnabled', 1, 1)
        assert sdo(f, idx, good[0], write=True)[0] != 0
        f.write('gEcatMotorEnabled', 0, 1)
test('real SDO bounds, length, CA and PREOP/disabled checks', access)


def voltage(f, mv, limit, expected, reason):
    step(f, limit - 1, mv=mv)
    assert code(f) == 0
    f.write('ready_ad', 1, 1)
    step(f, mv=mv)
    assert code(f) == expected
    assert f.read('gDriveFaultReason', 2) == reason
    assert f.read('u16VendorErrorCode', 2) == expected
    assert f.read('ready_ad', 1) == 0 and f.pwm == [0]*4
    assert diag(f, 'firstValid') == 1 and diag(f, 'firstBusMv') == mv
test('undervoltage <18 V confirmed for 50 ms stops PWM and latches 13', lambda f: voltage(f,17999,50,13,5))
test('overvoltage >30 V confirmed for 10 ms stops PWM and latches 14', lambda f: voltage(f,30001,10,14,6))


def voltage_noise(f):
    step(f, 100, mv=18000)
    step(f, 100, mv=30000)
    step(f, 49, mv=17000)
    step(f, mv=24000)
    step(f, 9, mv=31000)
    step(f, mv=24000)
    assert code(f) == 0
    step(f, 20, mv=31000, gap=10)
    assert code(f) == 0  # no accumulating unobserved gaps
test('threshold equality, glitches and long scheduler gaps do not falsely accumulate', voltage_noise)


def startup(f):
    f.clock = 0
    step(f, 499, mv=0)
    assert code(f) == 0
    step(f, 49, mv=0)
    assert code(f) == 0
    step(f, mv=0)
    assert code(f) == 13
test('500 ms startup undervoltage grace, then 50 ms confirmation', startup)


def reset(f):
    step(f, 50, mv=17000)
    assert code(f) == 13
    assert f.call('ErrorHandle_TryReset') == 0
    step(f, 220, mv=18500)
    assert f.call('ErrorHandle_TryReset') == 0  # 1 V reset hysteresis
    step(f, 199)
    assert f.call('ErrorHandle_TryReset') == 0
    step(f)
    assert f.read('gDriveFaultResetReady', 2) == 1
    f.vfo = 0
    assert f.call('ErrorHandle_TryReset') == 0
    f.vfo = 1
    assert f.call('ErrorHandle_TryReset') == 1
    assert code(f) == 0 and f.read('u16VendorErrorCode',2) == 0
    assert f.read('gDriveFaultReason',2) == 0
    assert f.read('gEcatMotorEnabled',1) == 0
    assert diag(f,'firstCode') == 13  # history retained
test('reset requires 19..29 V for 200 ms and released VFO; no automatic enable', reset)


def first_latch(f):
    f.uc.reg_write(UC_ARM_REG_PRIMASK, 1)
    f.call('ErrorHandle_ReportFaultReason', 10, 1)
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 1
    f.uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    step(f, 60, mv=17000)
    assert code(f) == 10 and f.read('gDriveFaultReason',2) == 1
    assert diag(f,'firstCode') == 10
    f.call('ErrorHandle_ReportFaultReason', 99, 99)
    assert code(f) == 10
test('first fault wins across ISR/foreground; PRIMASK preserved and unknown codes rejected', first_latch)


def overcurrent(f):
    cfg(f,'currentEnable',1)
    step(f, 9, a=-4001)
    assert code(f) == 0
    step(f, a=-4001)
    assert code(f) == 10 and f.read('gDriveFaultReason',2) == 2
    f.call('EthercatMotorControl_Update')
    assert f.get('LocalAxes','Objects','objErrorCode') == 0x2310
test('enabled per-phase overcurrent handles negative current and reports standard 2310h', overcurrent)


def phase_setup(f, a=100, b=0):
    cfg(f,'currentEnable',2)
    f.write('gEcatMotorEnabled',1,1)
    f.put('gDriveCurrentDiag',3,'state')
    f.put('gDriveCurrentDiag',a,'phaseAReferenceCounts')  # ~1.2 A
    f.put('gDriveCurrentDiag',b,'phaseBReferenceCounts')


def missing_phase(f, channel):
    phase_setup(f,100 if channel == 0 else 0,100 if channel else 0)
    step(f,499,a=0,b=0)
    assert code(f) == 0
    step(f)
    assert code(f) == 11
    assert f.read('gDriveFaultReason',2) == 3 + channel
test('stationary phase A missing: 300 ms settled reference plus 200 ms confirmation',lambda f: missing_phase(f,0))
test('stationary phase B missing: independent nonzero-reference gate',lambda f: missing_phase(f,1))


def no_phase_false_alarm(f):
    phase_setup(f,a=20)  # <500 mA expected, not testable
    step(f,600)
    assert code(f) == 0
    phase_setup(f)
    f.hook('EthercatMotorControl_CurrentMotionActive',lambda *_: 1)
    step(f,600)
    assert code(f) == 0
    f.hook('EthercatMotorControl_CurrentMotionActive',lambda *_: 0)
    step(f,600,a=1000)  # >20% expected, healthy
    assert code(f) == 0
    f.write('gEcatMotorEnabled',0,1)
    step(f,600)
    assert code(f) == 0
test('phase test excludes low reference, rotation, healthy current and disabled motor', no_phase_false_alarm)


def phase_submillisecond_change(f, motion):
    phase_setup(f)
    step(f,450)
    assert diag(f,'phaseAMs') > 100
    if motion:
        f.hook('EthercatMotorControl_CurrentMotionActive',lambda *_:1)
    else:
        f.put('gDriveCurrentDiag',101,'phaseAReferenceCounts')
    f.call('DriveFault_PhaseTick')  # another motor cycle, same millisecond
    if motion:
        f.hook('EthercatMotorControl_CurrentMotionActive',lambda *_:0)
    else:
        f.put('gDriveCurrentDiag',100,'phaseAReferenceCounts')
    f.call('DriveFault_PhaseTick')
    assert diag(f,'settleMs') == diag(f,'phaseAMs') == 0
    step(f,450)
    assert code(f) == 0
test('brief motion between millisecond samples restarts phase settling',lambda f: phase_submillisecond_change(f,True))
test('brief reference change between millisecond samples restarts phase settling',lambda f: phase_submillisecond_change(f,False))


def phase_raw_setup(f):
    del f.callbacks[f.symbols['DriveTelemetry_GetSafetySample'] & ~1]
    del f.callbacks[f.symbols['DriveTelemetry_GetPhaseSample'] & ~1]
    del f.callbacks[f.symbols['DriveTelemetry_GetSnapshot'] & ~1]
    f.hook('DMA_GetCurrDataCounter',lambda *_:10)
    phase_setup(f,a=117,b=117)  # about 707 mA each, 1 A vector reference


def phase_raw_step(f, i, a=2042, b=2048, capture=True):
    f.write('sMs',600+i)
    f.uc.mem_write(0xE0001004,struct.pack('<I',i*144000))
    if capture:
        f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
            a,b,587,0,a,b,a,b,a,b))
        f.call('DriveTelemetry_AdcComplete')
    f.call('DriveFault_PhaseTick')
    if i % 8 == 0:
        f.call('DriveTelemetry_Update')
        f.call('DriveFault_Update')


def phase_raw_jitter(f):
    phase_raw_setup(f)
    for i in range(1,521): phase_raw_step(f,i)
    assert code(f) == 11 and f.read('gDriveFaultReason',2) == 4
    assert 700 <= diag(f,'phaseReferenceBMa') <= 710
    assert diag(f,'phaseBMs') >= 200 and f.pwm == [0]*4
    f.call('EthercatMotorControl_Update')
    assert f.get('LocalAxes','Objects','objErrorCode') == 0x3321
    for _ in range(16): f.call('DriveFault_Tick')
    assert f.led[-1] == 0
test('real ADC phase detection survives 8 ms foreground gaps and starts red alarm',phase_raw_jitter)


def phase_raw_healthy(f):
    phase_raw_setup(f)
    for i in range(1,521): phase_raw_step(f,i,a=2185,b=2185)
    assert code(f) == 0
    assert diag(f,'phaseBlockMask') & 0x600 == 0x600
test('fixed-rate raw phase detector accepts measured current at the reference',phase_raw_healthy)


def phase_raw_stale(f):
    phase_raw_setup(f)
    for i in range(1,401): phase_raw_step(f,i)
    for i in range(401,521): phase_raw_step(f,i,capture=False)
    assert code(f) == 15 and f.read('gDriveFaultReason',2) == 7
    assert diag(f,'phaseAMs') == diag(f,'phaseBMs') == 0
test('stopped ADC invalidates phase qualification and reports sensor failure',phase_raw_stale)


def phase_raw_dma_error(f):
    phase_raw_setup(f)
    for i in range(1,401): phase_raw_step(f,i)
    f.call('DriveTelemetry_AdcError')
    phase_raw_step(f,401,capture=False)
    assert diag(f,'phaseBlockMask') & 0x10
    assert diag(f,'settleMs') == diag(f,'phaseBMs') == 0
    for i in range(402,852): phase_raw_step(f,i)
    assert code(f) == 0
test('DMA error resets phase confirmation; new good samples must settle again',phase_raw_dma_error)


def invalid(f):
    step(f,99,status=0)
    assert code(f) == 0
    step(f,status=0)
    assert code(f) == 15 and f.read('gDriveFaultReason',2) == 7
    f.call('EthercatMotorControl_Update')
    assert f.get('LocalAxes','Objects','objErrorCode') == 0x7300
test('invalid voltage is sampling fault 15, not a fabricated 0 V/phase fault', invalid)


def repeated(f):
    step(f,mv=31000)
    step(f,200,mv=31000,fresh=False)
    assert code(f) == 0
    assert diag(f,'healthyMs') == 0
test('repeated sample cannot qualify overvoltage or healthy recovery', repeated)


def asynchronous_frames(f):
    for _ in range(6):
        step(f, mv=31000)
        step(f, mv=31000, fresh=False)
    assert code(f) == 0
test('repeated raw frames cannot qualify sustained overvoltage', asynchronous_frames)


def clock_wrap(f):
    step(f, 2)
    f.clock = 0xffffffe0
    step(f)
    step(f, 50, mv=17000)
    assert code(f) == 13
test('millisecond wrap does not reintroduce startup undervoltage grace', clock_wrap)


def foreground_led(f):
    # Execute the true /16 tick divider, without updating foreground.
    for _ in range(601*16): f.call('DriveFault_Tick')
    assert code(f) == 15 and f.read('gDriveFaultReason',2) == 8
    assert f.led == [0]
    for _ in range(500*16): f.call('DriveFault_Tick')
    assert f.led[-1] == 1
    assert f.call('ErrorHandle_TryReset') == 0
    # Legacy APIs must not touch Y1 or toggle red out of sequence.
    old = list(f.led)
    f.call('LedRunSet',0)
    f.call('LedError',15)
    assert f.led == old
test('actual 16 kHz divider, foreground stall latch, 500 ms red phase and no Y1 lamp writes', foreground_led)


def pdo(f):
    # Replace SM3's single assignment; leave SM2 and original maps untouched.
    f.write('sTxPDOassign',0x1A04,2,offset=2)
    assert f.call('APPL_GenerateMapping',f.data+128,f.data+132) == 0
    assert int.from_bytes(f.uc.mem_read(f.data+128,2),'little') == 20
    assert int.from_bytes(f.uc.mem_read(f.data+132,2),'little') == 12
    step(f,50,mv=17000)
    f.call('EthercatMotorControl_Update')
    f.call('CiA402_StateMachine')
    f.call('CiA402_StateMachine')
    f.call('APPL_InputMapping',f.data)
    out = bytes(f.uc.mem_read(f.data,20))
    assert struct.unpack_from('<4H',out,12) == (0x3220,13,5,0)
    assert struct.unpack_from('<H',out)[0] & 8
    assert sdo(f,0x206C) == (0,13)
    assert sdo(f,0x603F) == (0,0x3220)
    assert sdo(f,0x1001,size=1) == (0,5)
test('real CiA402 fault, 603F/1001/206C reads and 20-byte optional PDO bytes',pdo)


def reset_communication_gate(f):
    f.write('nAlStatus',8,1)
    step(f,50,mv=17000)
    f.call('EthercatMotorControl_Update')
    f.call('CiA402_StateMachine')
    f.call('CiA402_Application',f.symbols['LocalAxes'])
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes','i16State') == 0x80
    step(f,200)
    assert f.call('DriveFault_CanReset') == 1
    f.hook('ECAT_TryResetPdiFault',lambda *_:0)
    f.put('LocalAxes',0,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    f.put('LocalAxes',0x80,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 13 and sdo(f,0x206C) == (0,13)
    assert sdo(f,0x603F) == (0,0x3220)
    assert f.get('LocalAxes','i16State') == 0x80
    f.hook('ECAT_TryResetPdiFault',lambda *_:1)
    f.call('CiA402_StateMachine')  # held reset is not a new edge
    assert code(f) == 13
    f.put('LocalAxes',0,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    f.put('LocalAxes',0x80,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 0 and sdo(f,0x603F) == (0,0)
    assert f.read('gDriveFaultResetReady',2) == 0
    assert f.get('LocalAxes','i16State') == 2
    assert f.read('gEcatMotorEnabled',1) == 0
test('rejected communication re-arm retains motor report; a new reset edge is required',reset_communication_gate)


def motor_fault(f, recover=True):
    f.write('nAlStatus',8,1)
    step(f,50,mv=17000)
    f.call('EthercatMotorControl_Update')
    f.call('CiA402_StateMachine')
    f.call('CiA402_Application',f.symbols['LocalAxes'])
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes','i16State') == 0x80
    if recover: step(f,200)


def mailbox_reset(f):
    motor_fault(f)
    assert sdo(f,0x2031) == (0,1)
    assert sdo(f,0x2032) == (0,0)
    assert sdo(f,0x6040,0,write=True)[0] == 0
    f.call('CiA402_StateMachine')
    assert sdo(f,0x6040,128,write=True)[0] == 0
    # A normal cyclic 0x000F overwrites the OD word before processing.
    f.put('LocalAxes',15,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 0 and sdo(f,0x603F) == (0,0)
    assert sdo(f,0x206C) == (0,0) and sdo(f,0x2030) == (0,0)
    assert f.get('LocalAxes','i16State') == 2
    for _ in range(3):
        f.call('EthercatMotorControl_Update')
        f.call('CiA402_StateMachine')
    assert f.get('LocalAxes','i16State') == 2
    assert f.read('gEcatMotorEnabled',1) == 0
test('mailbox 6040=128 survives a cyclic overwrite and cannot automatically enable',mailbox_reset)


def mailbox_early(f):
    motor_fault(f,False)
    assert sdo(f,0x6040,0,write=True)[0] == 0
    assert sdo(f,0x6040,128,write=True)[0] == 0
    f.put('LocalAxes',0,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 13 and diag(f,'lastResetBlockMask') & 6
    step(f,210)
    f.call('CiA402_StateMachine')
    assert code(f) == 13  # no automatic retry when voltage later recovers
    assert sdo(f,0x6040,128,write=True)[0] == 0
    f.put('LocalAxes',0,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 0
test('early mailbox reset is rejected once; recovery requires a new explicit request',mailbox_early)


def mailbox_pdi(f):
    motor_fault(f)
    f.put('gEscPdiDiag',1,'faultLatched')
    assert sdo(f,0x6040,0,write=True)[0] == 0
    assert sdo(f,0x6040,128,write=True)[0] == 0
    f.put('LocalAxes',0,'Objects','objControlWord')
    f.call('CiA402_StateMachine')
    assert code(f) == 13 and f.pdi('faultLatched') == 1
test('mailbox reset cannot bypass the fresh PDO gate for a PDI communication fault',mailbox_pdi)


def mailbox_expired(f):
    motor_fault(f)
    assert sdo(f,0x6040,0,write=True)[0] == 0
    assert sdo(f,0x6040,128,write=True)[0] == 0
    f.put('LocalAxes',0,'Objects','objControlWord')
    step(f,101)
    f.call('CiA402_StateMachine')
    assert code(f) == 13
test('unprocessed mailbox reset expires after 100 ms',mailbox_expired)


def reset_masks(f):
    motor_fault(f)
    assert sdo(f,0x2032) == (0,0)
    f.vfo = 0
    assert sdo(f,0x2032)[1] & 8 and sdo(f,0x2031) == (0,0)
    f.vfo = 1
    f.write('ErrI_sum',15,1)
    assert sdo(f,0x2032)[1] & 16
    f.write('ErrI_sum',16,1)
    step(f,mv=18500)
    assert sdo(f,0x2032)[1] & 6 == 6
    step(f,status=0)
    assert sdo(f,0x2032)[1] & 1
    step(f,210)
    assert sdo(f,0x2031) == (0,1)
    cfg(f,'currentEnable',1)
    step(f,status=1)
    assert sdo(f,0x2032)[1] & 64
    step(f,a=4000)
    assert sdo(f,0x2032)[1] & 128
    assert sdo(f,0x2032,1,write=True)[0] != 0
test('2032 reports independent voltage, qualification, VFO, stale and phase reset blockers',reset_masks)


def adc_recovery_jitter(f):
    del f.callbacks[f.symbols['DriveTelemetry_GetSafetySample'] & ~1]
    del f.callbacks[f.symbols['DriveTelemetry_GetSnapshot'] & ~1]
    f.hook('DMA_GetCurrDataCounter',lambda *_:10)
    f.call('ErrorHandle_ReportFault',13)
    for i in range(1,241):
        f.write('sMs',i)
        f.put('gDriveFaultDiag',i,'nowMs')
        f.uc.mem_write(0xE0001004,struct.pack('<I',i*144000))
        f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                       2068,2068,566,0,2068,2068,2068,2068,2068,2068))
        f.call('DriveTelemetry_AdcComplete')
        f.call('DriveFault_PhaseTick')
        if i % 8 == 0:
            f.call('DriveTelemetry_Update')
            f.call('DriveFault_Update')
    assert diag(f,'healthyMs') >= 200  # fixed-rate recovery with current detection ON
    assert f.call('DriveFault_CanReset') == 1
    assert sdo(f,0x2031) == (0,1)
    # One bad raw sample breaks recovery immediately, even before the next
    # foreground conversion. Fresh old converted data must not hide it.
    f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                   2068,2068,400,0,2068,2068,2068,2068,2068,2068))
    f.call('DriveTelemetry_AdcComplete')
    assert f.call('DriveFault_CanReset') == 0
    for i in range(241,451):
        f.write('sMs',i)
        f.uc.mem_write(0xE0001004,struct.pack('<I',i*144000))
        f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                       2068,2068,566,0,2068,2068,2068,2068,2068,2068))
        f.call('DriveTelemetry_AdcComplete')
        f.call('DriveFault_PhaseTick')
        if i % 8 == 0: f.call('DriveFault_Update')
    # Configuration changes invalidate an old healthy interval.
    cfg(f,'currentEnable',1)
    assert f.call('DriveFault_CanReset') == 0
    cfg(f,'currentEnable',0)
    f.call('DriveTelemetry_AdcError')
    assert f.call('DriveFault_CanReset') == 0
    f.uc.mem_write(0xE0001004,struct.pack('<I',246*144000))
    assert f.call('DriveFault_CanReset') == 0
test('periodic voltage/phase recovery survives foreground jitter but rejects stale data',adc_recovery_jitter)


def adc_integration(f):
    del f.callbacks[f.symbols['DriveTelemetry_GetSafetySample'] & ~1]
    del f.callbacks[f.symbols['DriveTelemetry_GetSnapshot'] & ~1]
    f.hook('DMA_GetCurrDataCounter',lambda *_:10)
    for i in range(1,251):
        f.clock = i
        f.write('sMs',i)
        f.uc.mem_write(0xE0001004,struct.pack('<I',i*144000))
        f.uc.mem_write(f.symbols['adc_value'],struct.pack('<10I',
                       2068,2068,584,0,2068,2068,2068,2068,2068,2068))
        f.call('DriveTelemetry_AdcComplete')
        f.call('DriveTelemetry_Update')
        f.call('DriveFault_PhaseTick')
        f.call('DriveFault_Update')
    assert f.call('DriveFault_CanReset') == 1 and code(f) == 0
    f.uc.mem_write(0xE0001004,struct.pack('<I',260*144000))
    assert f.call('DriveFault_CanReset') == 0  # stale at reset despite old healthy timer
test('real DMA -> converted snapshot -> detector and reset-time 5 ms freshness check', adc_integration)

print(f'PASS {passed} compiled ARM fault tests (no hardware was operated).')
