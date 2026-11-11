import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if 'STATUSWORD' in s.upper() and ('0x0021' in s or '0x21' in s or 'STATE_READY' in s):
                lines=s.split('\n')
                hits=[(i+1,l.strip()[:140]) for i,l in enumerate(lines)
                      if re.search(r'(STATUSWORD|0x002[0-9A-F]|0x004[0-9A-F]|STATE_READY|STATE_SWITCHED|STATE_OPERATION_ENABLED|STATE_FAULT)',l)]
                print('####',p,len(hits))
                for h in hits[:25]: print('   ',h[0],h[1])
