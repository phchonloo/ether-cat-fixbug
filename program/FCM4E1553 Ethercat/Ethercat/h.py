s=open('Inc/ecat_def.h','rb').read().decode('utf-8','replace')
lines=s.split('\n')
for i in range(875,915):
    print(i+1, lines[i][:120])
print('---- 0x1018 in coeappl.c')
t=open('src/coeappl.c','rb').read().decode('utf-8','replace').split('\n')
for i,l in enumerate(t):
    if '1018' in l or 'DEVICE_PROFILE' in l or 'OBJID' in l:
        print(i+1,l[:140])
