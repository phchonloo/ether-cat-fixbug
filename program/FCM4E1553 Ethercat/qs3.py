s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:170]) for i in range(1299,1360)))
print('==== TransitionAction')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:170]) for i in range(1180,1266)))
