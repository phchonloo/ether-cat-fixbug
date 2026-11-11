import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith('.h'):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if 'ABORTIDX_READ_ONLY_ENTRY' in s and 'define' in s:
                print('####',p)
                for line in s.split('\n'):
                    if 'ABORTIDX_' in line and '#define' in line:
                        print('  ',line.strip()[:110])
                raise SystemExit
