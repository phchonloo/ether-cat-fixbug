"""Read-only HEX/ELF/linker/metadata checks; never talks to hardware."""
from pathlib import Path
import hashlib,json,struct,sys,xml.etree.ElementTree as E
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'tests/.pulse_test_deps'))
from elftools.elf.elffile import ELFFile
P=R/'program/FCM4E1553 Ethercat/MDK-ARM'
fw=P/'N32G455/N32G455.axf';hexpath=P/'N32G455/N32G455.hex'
hexdata={};base=0
for line in hexpath.read_text('ascii').splitlines():
 assert line.startswith(':')
 b=bytes.fromhex(line[1:]);assert sum(b)&255==0 and len(b)==b[0]+5
 count,high,low,kind=b[:4];addr=high*256+low;data=b[4:4+count]
 if kind==0:
  for i,v in enumerate(data):hexdata[base+addr+i]=v
 elif kind==4:base=int.from_bytes(data,'big')<<16
 elif kind==2:base=int.from_bytes(data,'big')<<4
assert hexdata and max(hexdata)<0x0803f000
with fw.open('rb') as f:
 elf=ELFFile(f);expected={}
 for s in elf.iter_segments():
  if s['p_type']=='PT_LOAD' and 0x08000000<=s['p_paddr']<0x08040000:
   for i,v in enumerate(s.data()):expected[s['p_paddr']+i]=v
 assert expected and all(hexdata.get(a)==v for a,v in expected.items())
 assert min(expected)==0x08000000 and max(expected)<0x0803f000
 for addr in hexdata:assert addr in expected # every HEX byte belongs to the linked image
 symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
 assert symbols['PVD_IRQHandler']!=symbols['HardFault_Handler']
 assert all(n in symbols for n in ('DriveSafety_Init','DriveSafety_Update','DriveParameters_Init','WriteDriveParameters'))
 assert not any(0x0803f000<=s['p_paddr']<0x08040000 and s['p_filesz'] for s in elf.iter_segments() if s['p_type']=='PT_LOAD')
for ext in ('axf','hex'):
 assert (R/('FCM_Protection_SysTickFix_20261103.'+ext)).read_bytes()==(P/('N32G455/N32G455.'+ext)).read_bytes()
project=E.parse(P/'Project.uvprojx')
assert project.findtext('.//LDads/useFile')=='1'
assert '0x0003F000' in (P/'N32G455/N32G455.sct').read_text('utf8')
for rel in ('PLC/FCM_Homing.plcproj','PLC/POUs/MAIN.TcPOU'):
 assert (R/rel).exists()
xml=E.parse(R/'FCM4E1553_CiA402_Protection_SysTickFix_20261103.xml').getroot()
old=E.parse(R/'tools/baseline_home_x1.xml').getroot()
for p in ['./Descriptions/Devices/Device/Type','./Descriptions/Devices/Device/RxPdo','./Descriptions/Devices/Device/TxPdo']:
 assert [E.canonicalize(E.tostring(e),strip_text=True) for e in old.findall(p)]==[E.canonicalize(E.tostring(e),strip_text=True) for e in xml.findall(p)]
files=['FCM_Protection_SysTickFix_20261103.axf','FCM_Protection_SysTickFix_20261103.hex','FCM4E1553_CiA402_Protection_SysTickFix_20261103.xml','PLC/FCM_Homing.plcproj']
result={'hex_bytes':len(hexdata),'maximum_flash_address':hex(max(hexdata)),'parameter_region':'0x0803F000..0x0803FFFF',
 'hex_matches_elf':True,'pdo_and_identity_unchanged':True,'sha256':{p:hashlib.sha256((R/p).read_bytes()).hexdigest() for p in files},'hardware_operated':False}
(R/'validation/release_check.json').write_text(json.dumps(result,indent=2),'utf8')
print('PASS release: HEX matches ELF, parameter pages excluded, top-level files match, fixed linker and unchanged PDO/identity')
