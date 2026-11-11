"""Offline ARMCC build; never opens a probe, downloads, or starts hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'program/FCM4E1553 Ethercat/MDK-ARM'
BIN = Path(os.environ.get('KEIL_ARMCC_BIN', 'D:/App/Keil_v5/ARM/ARMCC/bin'))
VALIDATION = ROOT / 'validation'
VALIDATION.mkdir(exist_ok=True)
xml = ET.parse(PROJECT / 'Project.uvprojx')
controls = xml.find('.//Cads/VariousControls')
includes = controls.findtext('IncludePath').split(';')
defines = [s.strip() for s in controls.findtext('Define').split(',')]
common = ['--cpu=Cortex-M4.fp.sp', '--apcs=interwork', '--library_type=microlib',
          '--c99', '-O0', '-g', '-D__MICROLIB']
common += ['-D' + s for s in defines]
common += ['-I' + s for s in includes]
log = []
errors = 0
compiled = 0
for entry in xml.findall('.//Groups/Group/Files/File'):
    source = Path(entry.findtext('FilePath').replace('\\', '/'))
    if source.suffix.lower() != '.c':
        continue
    destination = Path('N32G455') / (source.stem.lower() + '.o')
    command = [str(BIN / 'armcc.exe')] + common + ['-c', str(source), '-o', str(destination)]
    result = subprocess.run(command, cwd=PROJECT, capture_output=True, text=True,
                            errors='replace')
    log.append('COMPILE ' + str(source) + '\n' + result.stdout + result.stderr)
    errors += result.returncode != 0
    compiled += 1
    if result.returncode:
        print(log[-1], flush=True)
if errors:
    (VALIDATION / 'build.log').write_text('\n'.join(log), 'utf8')
    raise SystemExit(f'{errors} compilation failures')
# Startup assembly and the vendor libraries are unchanged. Use Keil's existing
# scatter file and link list; all C translation units above were rebuilt.
command = [str(BIN / 'armlink.exe'), '--via', 'N32G455/N32G455.lnp',
           '--libpath=' + str(BIN.parent / 'lib')]
result = subprocess.run(command, cwd=PROJECT, capture_output=True, text=True,
                        errors='replace')
log.append('LINK\n' + result.stdout + result.stderr)
(VALIDATION / 'build.log').write_text('\n'.join(log), 'utf8')
print(result.stdout + result.stderr, flush=True)
result.check_returncode()
axf = PROJECT / 'N32G455/N32G455.axf'
hexfile = PROJECT / 'N32G455/N32G455.hex'
subprocess.run([str(BIN / 'fromelf.exe'), '--i32', '--output', str(hexfile), str(axf)], check=True)
shutil.copyfile(axf, ROOT / 'FCM_Protection_SysTickFix_20261103.axf')
shutil.copyfile(hexfile, ROOT / 'FCM_Protection_SysTickFix_20261103.hex')
print(f'BUILD PASS: {compiled} C files; startup assembled; unchanged vendor libraries; AXF + HEX.', flush=True)
