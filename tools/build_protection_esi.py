from pathlib import Path
import xml.etree.ElementTree as E
R=Path(__file__).resolve().parents[1]
src=R/'tools/baseline_home_x1.xml'
tree=E.parse(src);doc=tree.getroot();device=doc.find('./Descriptions/Devices/Device')
dic=device.find('./Profile/Dictionary');types=dic.find('DataTypes');objects=dic.find('Objects')
def sub(p,n,t=None,**kw):
 e=E.SubElement(p,n,kw)
 if t is not None:e.text=str(t)
 return e
def access(p,rw):
 f=sub(p,'Flags');sub(f,'Access',rw,**({'WriteRestrictions':'PreOP'} if rw=='rw' else {}))
for idx,title in [(0x1010,'Store supported parameters'),(0x1011,'Restore supported factory parameters on next boot')]:
 dt=sub(types,'DataType');name='DTParameterCommand%04X'%idx
 sub(dt,'Name',name);sub(dt,'BitSize',48)
 for n,bits,typ,off in [(0,8,'USINT',0),(1,32,'UDINT',16)]:
  it=sub(dt,'SubItem')
  for key,val in [('SubIdx',n),('Name','Number of entries' if not n else 'All supported parameters'),('Type',typ),('BitSize',bits),('BitOffs',off)]:sub(it,key,val)
  access(it,'ro' if not n else 'rw')
 ob=sub(objects,'Object');sub(ob,'Index','#x%04X'%idx);sub(ob,'Name',title);sub(ob,'Type',name);sub(ob,'BitSize',48)
 info=sub(ob,'Info')
 for n,bits in [(0,8),(1,32)]:
  it=sub(info,'SubItem');sub(it,'Name','Number of entries' if not n else 'All supported parameters')
  sub(sub(it,'Info'),'DefaultData',(1).to_bytes(bits//8,'little').hex().upper())
 access(ob,'rw')
names=['Parameter command status','Parameter generation','Parameter slot 0 defaults 1 A 2 B',
 'Parameter command error','Unsaved supported parameters','Restart inhibit requires fresh disabled RxPDO',
 'MCU reset flags at boot','Directional limit deceleration active','Protection interface version','Valid parameter page mask']
for i,name in enumerate(names):
 ob=sub(objects,'Object');sub(ob,'Index','#x%04X'%(0x20C0+i));sub(ob,'Name',name);sub(ob,'Type','UDINT');sub(ob,'BitSize',32)
 sub(sub(ob,'Info'),'DefaultData',([0,0,0,0,1,1,0,0,0x20261111,0][i]).to_bytes(4,'little').hex().upper());access(ob,'ro')
device.find('Name').text='FCM4E1553 CiA402 Drive - Protection and retained parameters'
E.indent(tree,space='  ')
dest=R/'FCM4E1553_CiA402_Protection_SysTickFix_20261103.xml';tree.write(dest,encoding='utf-8',xml_declaration=True)
indices=[o.findtext('Index') for o in objects]
assert len(indices)==len(set(indices))
old=E.parse(src).getroot()
for path in ['./Descriptions/Devices/Device/Type','./Descriptions/Devices/Device/RxPdo','./Descriptions/Devices/Device/TxPdo']:
 assert [E.canonicalize(E.tostring(e),strip_text=True) for e in old.findall(path)]==[E.canonicalize(E.tostring(e),strip_text=True) for e in doc.findall(path)]
print('PASS ESI: unique dictionary, store/restore metadata, unchanged identity and every PDO')
