"""Replay master ramps through the real ARM firmware, including PhaseGet.

No peripheral timing or shaft mechanics are simulated. --baseline records the
old algorithm's discontinuities; normal runs enforce the regression limits.
"""
import ast
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/.pulse_test_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
from elftools.elf.elffile import ELFFile
helpers = ast.parse((ROOT / 'tests/verify_pulse_position.py').read_text('utf8'))
helpers.body = [n for n in helpers.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))
               and n.name in ('ArmImage', 'unwrap_type', 'field')]
exec(compile(helpers, 'pulse_helpers', 'exec'))
BUILD = ROOT / 'tests/.csp_stop_build'
BUILD.mkdir(exist_ok=True)
BASELINE = '--baseline' in sys.argv
FW = BUILD / 'before.axf' if BASELINE else ROOT / 'program/FCM4E1553 Ethercat/MDK-ARM/N32G455/N32G455.axf'
BIN = Path(os.environ.get('KEIL_ARMCC_BIN', 'D:/App/Keil_v5/ARM/ARMCC/bin'))

def run_case(speed, rate, direction=1, resolution=1000, reversal=False, quickstop=False, sample_us=2000):
    f = ArmImage(FW)
    def put(var, value, *names):
        off, size = field(f, var, *names) if names else (0, 4)
        f.write(var, value, size, off)
    def addr(var, *names):
        return f.symbols[var] + (field(f, var, *names)[0] if names else 0)
    put('sxifen', resolution)
    f.call('EthercatMotorControl_Init')
    f.write('nAlStatus', 8, 1)
    put('LocalAxes', 1, 'bAxisIsActive')
    put('LocalAxes', 0x10, 'i16State')
    put('LocalAxes', 8, 'Objects', 'objModesOfOperation')
    put('LocalAxes', 15, 'Objects', 'objControlWord')
    put('LocalAxes', sample_us, 'u32CycleTime')
    put('LocalAxes', 6, 'Objects', 'objQuickStopOptionCode')
    put('LocalAxes', 300, 'Objects', 'objQuickStopDeclaration')
    for _ in range(2):
        f.call('EthercatMotorControl_OnSyncCycle')
        f.call('EthercatMotorControl_Update')
    assert sample_us in (1000, 2000)
    dt = sample_us / 1000000.0
    macros = ['#define MOTOR_TICKS %d' % (sample_us * 16000 // 1000000)]
    for alias, name, signature in (
        ('begin', 'EthercatMotorControl_BeginPhaseCycle', 'void (*)(int)'),
        ('pulse', 'PulseFunction', 'void (*)(void)'),
        ('phase', 'PhaseGet', 'void (*)(int)'),
        ('end', 'EthercatMotorControl_EndPhaseCycle', 'void (*)(int)'),
        ('sync', 'EthercatMotorControl_OnSyncCycle', 'void (*)(void)'),
        ('update', 'EthercatMotorControl_Update', 'void (*)(void)'),
        ('state', 'CiA402_StateMachine', 'void (*)(void)'),
        ('app', 'CiA402_Application', 'void (*)(void *)')):
        macros.append('#define %s ((%s)0x%Xu)' % (alias, signature, f.symbols[name]))
    for alias, location in (
        ('angle', addr('Iangle')), ('step', addr('BaseRpm')),
        ('target', addr('LocalAxes', 'Objects', 'objTargetPosition')),
        ('actual', addr('gEcatPulseDiag', 'countedPosition'))):
        macros.append('#define %s (*(volatile int *)0x%Xu)' % (alias, location))
    macros.append('#define control (*(volatile unsigned short *)0x%Xu)' % addr('LocalAxes', 'Objects', 'objControlWord'))
    macros.append('#define axisptr ((void *)0x%Xu)' % addr('LocalAxes'))
    code = '\n'.join(macros) + r'''
unsigned runner(unsigned count, const int *positions, int *out, unsigned stop_at) {
    unsigned i, k; int old = 0, delta, jump, maxjump = 0, zeros = 0, peak = 0, rises = 0;
    int steady_start = out[6], steady_end = out[7];
    int lag_sum = 0, samples = 0, low = 16001, high = -16001;
    for (i = 0; i < count; ++i) {
        if (stop_at && i == stop_at) { control = 2; state(); }
        if (stop_at && i >= stop_at) { app(axisptr); state(); }
        target = positions[i]; sync(); update();
        for (k = 0; k < MOTOR_TICKS; ++k) {
            begin(angle); pulse(); phase(step); end(angle);
            jump = step - old; if (jump < 0) jump = -jump;
            if (jump > maxjump) maxjump = jump;
            delta = i ? positions[i] - positions[i-1] : 0;
            if ((!stop_at || i < stop_at) && (delta > 5 || delta < -5) && step == 0) ++zeros;
            if (stop_at && i >= stop_at && step > old) ++rises;
            if (i >= steady_start && i < steady_end) {
                if (step < low) low = step;
                if (step > high) high = step;
            }
            old = step;
        }
        delta = target - actual; if (delta < 0) delta = -delta;
        if (delta > peak) peak = delta;
        if (i >= steady_start && i < steady_end) { lag_sum += delta; ++samples; }
    }
    out[0] = maxjump; out[1] = zeros; out[2] = peak;
    out[3] = actual; out[4] = step; out[5] = rises;
    out[6] = lag_sum; out[7] = samples; out[8] = low; out[9] = high;
    return 0;
}
'''
    src = BUILD / 'runner.c'
    src.write_text(code, 'ascii')
    subprocess.run([str(BIN/'armcc.exe'), '--cpu=Cortex-M4', '--c99', '-O2',
                    '--library_type=microlib', '-c', str(src), '-o', str(BUILD/'runner.o')], check=True)
    subprocess.run([str(BIN/'armlink.exe'), '--cpu=Cortex-M4', '--library_type=microlib',
                    '--entry=runner', '--ro_base=0x08060000', '--rw_base=0x20028000',
                    '--libpath='+str(BIN.parent/'lib'), str(BUILD/'runner.o'),
                    '-o', str(BUILD/'runner.axf')], check=True)
    with open(BUILD/'runner.axf', 'rb') as stream:
        elf = ELFFile(stream)
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                f.uc.mem_write(section['sh_addr'], section.data())
        entry = next(s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name == 'runner')
    positions = []
    velocity = 0.0
    position = 0.0
    def segment(goal, hold):
        nonlocal velocity, position
        while abs(goal - velocity) > 1e-8:
            old = velocity
            velocity += max(-rate*dt, min(rate*dt, goal-velocity))
            position += (old+velocity)*.5*resolution/60*dt
            positions.append(round(position))
        for _ in range(hold):
            position += velocity*resolution/60*dt
            positions.append(round(position))
    segment(direction*speed, 500)
    steady_start, steady_end = len(positions)-250, len(positions)
    stop_at = len(positions) if quickstop else 0
    if reversal:
        segment(-direction*speed, 500)
    segment(0, 1000)
    assert len(positions)*4 < 0x1C000
    f.uc.mem_write(0x20010000, struct.pack('<%di' % len(positions), *positions))
    f.uc.mem_write(0x2002D000+24, struct.pack('<2i', steady_start, steady_end))
    f.call(entry, len(positions), 0x20010000, 0x2002D000, stop_at, count=2000000000)
    jump, zeros, peak, actual, last, rises, lag_sum, samples, low, high = struct.unpack('<10i', f.uc.mem_read(0x2002D000, 40))
    result = dict(rpm=speed, ramp=rate, direction=direction, resolution=resolution,
                  sample_us=sample_us, steady_error_mean_turns=lag_sum/samples/resolution,
                  steady_min_rpm=low/16, steady_max_rpm=high/16,
                  reversal=reversal, quickstop=quickstop, max_tick_jump_rpm=jump/16,
                  zero_ticks_while_moving=zeros, peak_error_turns=peak/resolution,
                  endpoint_error=actual-positions[-1], final_step=last)
    print(json.dumps(result), flush=True)
    f.close()
    if not BASELINE:
        assert jump <= 16, result  # No large single-tick velocity discontinuity.
        assert zeros == 0, result
        assert last == 0, result
        if quickstop:
            assert rises == 0, result
        else:
            assert actual == positions[-1], result
    return result

cases = [(700, 300), (1000, 300), (1000, 600), (700, 1000), (700, 200)]
if '--quick' in sys.argv:
    cases = cases[:1]
results = [run_case(speed, rate) for speed, rate in cases]
if '--quick' not in sys.argv:
    results += [run_case(700, 300, -1), run_case(700, 300, reversal=True),
                run_case(1000, 600, resolution=40000), run_case(700, 300, quickstop=True)]
(BUILD / ('baseline.json' if BASELINE else 'fixed.json')).write_text(json.dumps(results, indent=2), 'utf8')
