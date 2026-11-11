s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
for i,l in enumerate(s):
    if '0x6060' in l or 'WriteMotionParameter' in l or 'WriteControlWord' in l:
        print(i+1, l.strip()[:170])
