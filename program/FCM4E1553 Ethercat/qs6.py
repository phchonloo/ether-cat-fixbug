import re
h=open('Ethercat/Inc/cia402appl.h','rb').read().decode('utf-8','replace').split('\n')
for i,l in enumerate(h):
    if re.search(r'(CONTROLWORD_COMMAND|STATUSWORD_[A-Z_]+|STATUSWORD_STATE_MASK)',l) and '#define' in l:
        print('%d %s'%(i+1,l.strip()[:150]))
