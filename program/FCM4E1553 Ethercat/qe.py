s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:160]) for i in range(775,860)))
print('---- 780-840 stopping override / 1040-1070')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:160]) for i in range(788,845)))
