import re
s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace')
lines=s.split('\n')
for i,l in enumerate(lines):
    if re.search(r'(6060|6075|6502|SDO_ABORT|ABORT)',l) and not l.strip().startswith('//'):
        print(i+1, l.rstrip()[:150])
