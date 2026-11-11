s=open('User/EthercatMotorControl.c','rb').read().decode('utf-8','replace').split('\n')
print('\n'.join('%d %s'%(i+1,s[i].rstrip()[:170]) for i in range(90,175)))
