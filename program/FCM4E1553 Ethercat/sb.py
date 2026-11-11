s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
out=[]
for i,l in enumerate(s):
    if 'ModesOfOperation' in l:
        out.append('%d %s'%(i+1,l.strip()[:170]))
print('\n'.join(out))
