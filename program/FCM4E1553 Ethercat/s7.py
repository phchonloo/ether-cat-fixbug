import os
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if '0x0609' in s:
                print('####',p)
                for i,l in enumerate(s.split('\n')):
                    if '0x0609' in l or '0x0601' in l:
                        print('  ',i+1,l.strip()[:140])
