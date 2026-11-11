import os
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith('.c'):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if '0x06090030' in s or '0x06010002' in s:
                print('####',p)
                lines=s.split('\n')
                for i,l in enumerate(lines):
                    if '0x060' in l or '0x050' in l or '0x080' in l:
                        print('  ',i+1,l.strip()[:120])
                print()
