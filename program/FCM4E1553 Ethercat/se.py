s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
print('lines',len(s))
lo=max(0,len(s)-70)
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:150]) for i in range(lo,len(s))))
