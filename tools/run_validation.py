"""Offline-only regression; no probe, download, activation or motor operation."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
import subprocess,sys,json
R=Path(__file__).resolve().parents[1]
(R/'validation').mkdir(exist_ok=True)
def run(script):
 p=subprocess.run([sys.executable,str(R/script)],cwd=R,capture_output=True,text=True,errors='replace')
 (R/'validation'/('run_'+Path(script).stem+'.log')).write_text(p.stdout+'\nSTDERR:\n'+p.stderr,'utf8')
 print(('PASS ' if not p.returncode else 'FAIL ')+script,flush=True)
 return {'script':script,'exit':p.returncode}
results=[run('tools/build_protection_esi.py')]
if results[-1]['exit']:raise SystemExit(1)
commands=['tests/verify_systick_watchdog.py','tests/verify_protection.py','tests/verify_limits.py',
 'tests/verify_drive_boot.py','tests/verify_plc_protection_bridge.py','tests/verify_drive_current.py',
 'tests/verify_drive_faults.py','tests/verify_homing.py','tests/verify_pulse_position.py',
 'tests/verify_pdi_faults.py','tests/verify_plc_home_switch.py','plc_tests/verify.py']
with ThreadPoolExecutor(max_workers=3) as pool:
 futures=[pool.submit(run,s) for s in commands]
 for future in as_completed(futures):
  results.append(future.result())
  (R/'validation/run_result.json').write_text(json.dumps(results,indent=2),'utf8')
if not any(x['exit'] for x in results):results.append(run('tools/check_release.py'))
(R/'validation/run_result.json').write_text(json.dumps(results,indent=2),'utf8')
raise SystemExit(1 if any(x['exit'] for x in results) else 0)
