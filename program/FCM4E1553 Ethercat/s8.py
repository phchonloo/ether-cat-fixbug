s=open('Ethercat/Inc/sdoserv.h','rb').read().decode('utf-8','replace').split('\n')
for i in range(315,350):
    print(i+1, s[i].strip()[:120])
