import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if '0x6060' in s or '0x6075' in s:
                lines=s.split('\n')
                hits=[(i+1,l.strip()[:130]) for i,l in enumerate(lines) if '0x6060' in l or '0x6075' in l]
                print('####',p,len(hits))
                for h in hits[:8]: print('   ',h[0],h[1])
