"""Package this standalone 15A release locally; never upload or touch hardware."""
from pathlib import Path
import os,zipfile,hashlib,json
R=Path(__file__).resolve().parents[1]
dest=R.parent/'FCM_Protection_UndervoltageFix_20261111.zip'
results=json.loads((R/'validation/run_result.json').read_text('utf8'))
assert len(results)==14 and all(x['exit']==0 for x in results),'Final regression must pass before packaging'
excluded={'node_modules','__pycache__','.git','.workbuddy'}
files=[]
for current,dirs,names in os.walk(R):
 dirs[:]=[d for d in dirs if d not in excluded and not d.startswith('.') and not d.startswith('_build')]
 for name in names:
  p=Path(current)/name
  if '.uvguix.' in name or p.is_symlink() or name.startswith('.') or name.endswith(('.tmp','.inspect.ndjson','.o','.d','.crf','.lst','.bak','.uvgui.pc')):continue
  if name.startswith('FCM1_'):continue # user device dumps stay in their backup directory
  files.append(p)
manifest={p.relative_to(R).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files) if p.name!='MANIFEST_SHA256.json'}
(R/'MANIFEST_SHA256.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf8')
if R/'MANIFEST_SHA256.json' not in files:files.append(R/'MANIFEST_SHA256.json')
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=7) as z:
 for p in sorted(files):z.write(p,Path(R.name)/p.relative_to(R))
with zipfile.ZipFile(dest) as z:
 assert z.testzip() is None
 for required in ('FCM_Protection_SysTickFix_20261103.hex','FCM_Protection_SysTickFix_20261103.axf','README.md','烧录与验证.md','validation/VALIDATION.md','program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx'):
  assert R.name+'/'+required in z.namelist(),required
 assert not any('/node_modules/' in n or '/.pulse_test_deps/' in n or '/.homing_build_' in n for n in z.namelist())
 for p,h in manifest.items():assert hashlib.sha256(z.read(R.name+'/'+p)).hexdigest()==h,p
result={'package':dest.name,'bytes':dest.stat().st_size,'files':len(files),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'hardware_operated':False}
(R.parent/'SHA256_15A_20261111.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8')
print(json.dumps(result,ensure_ascii=False))
