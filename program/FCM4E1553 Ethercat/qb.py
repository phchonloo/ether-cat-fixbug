s=open('User/EthercatMotorControl.c','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:170]) for i in range(176,215)))
print('---- 700-760 IsStopped/apply')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:170]) for i in range(700,770)))
