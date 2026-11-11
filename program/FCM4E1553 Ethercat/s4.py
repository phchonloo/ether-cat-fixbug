import re
h=open('User/DriveTelemetryObjects.h','rb').read().decode('utf-8','replace').split('\n')
for i,l in enumerate(h):
    if '6075' in l or 'dtDesc6075' in l:
        print('T',i+1,l.strip()[:180])
print('---- cia402appl.h 6061')
c=open('Ethercat/Inc/cia402appl.h','rb').read().decode('utf-8','replace').split('\n')
for i,l in enumerate(c):
    if '0x6061' in l or '6061' in l and 'Entry' in l:
        print('C',i+1,l.strip()[:180])
print('---- DriveTelemetry.c 6075 handler')
d=open('User/DriveTelemetry.c','rb').read().decode('utf-8','replace').split('\n')
for i in range(405,450):
    print(i+1,d[i].rstrip()[:150])
