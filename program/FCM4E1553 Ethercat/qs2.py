s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
def show(a,b):
    print('----- %d-%d'%(a,b))
    print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:160]) for i in range(a-1,min(b,len(s)))))
show(850,960)
show(1260,1300)
