s=open('Ethercat/src/cia402appl.c','rb').read().decode('utf-8','replace').split('\n')
for i in range(95,215):
    print(i+1, s[i].rstrip()[:150])
