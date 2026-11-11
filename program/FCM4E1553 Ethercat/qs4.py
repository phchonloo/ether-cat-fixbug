h=open('Ethercat/Inc/cia402appl.h','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,h[i].rstrip()[:170]) for i in range(320,360)))
print('==== defaults 1195-1230')
print('\n'.join('%d %s'%(i+1,h[i].rstrip()[:150]) for i in range(1193,1232)))
