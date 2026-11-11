s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:150]) for i in range(1950,2000)))
