import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if '605A' in s or 'QuickStop' in s or 'QUICK_STOP' in s:
                print('####',p)
                for i,l in enumerate(s.split('\n')):
                    if re.search(r'(605A|6085|605B|QuickStop|QUICK_STOP|quick_stop)',l):
                        print('  ',i+1,l.strip()[:160])
