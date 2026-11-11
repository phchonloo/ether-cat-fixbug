import os,re
pats=[r'0x0?E66', r'15530111', r'0x1018', r'VendorId', r'ProductCode', r'RevisionNo']
for root,dirs,files in os.walk('.'):
    for fn in files:
        if not fn.lower().endswith(('.c','.h','.xml','.bin','.hex')): continue
        p=os.path.join(root,fn)
        try: s=open(p,'rb').read().decode('utf-8','replace')
        except: continue
        hits=[]
        for pat in pats:
            for m in re.finditer(pat,s):
                line=s[:m.start()].count('\n')+1
                hits.append((line,pat))
                break
        if hits:
            print(p, hits[:6])
