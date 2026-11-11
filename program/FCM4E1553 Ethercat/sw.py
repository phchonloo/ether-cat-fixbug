import os
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if 'STATUSWORD_VOLTAGE_ENABLED' in s or 'STATUSWORD_REMOTE' in s:
                print('####',p)
                for i,l in enumerate(s.split('\n')):
                    if 'STATUSWORD_VOLTAGE_ENABLED' in l or 'STATUSWORD_REMOTE' in l:
                        print('  ',i+1,l.strip()[:150])
