import re
s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace')
lines=s.split('\n')
for i,l in enumerate(lines):
    if re.search(r'(0x1C12|0x1C13|u16RxPdoAssign|u16TxPdoAssign|OBJID_PDO)',l):
        print(i+1, l.strip()[:130])
