import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if 'ApplyRamp(' in s or 'CONTROL_HZ' in s:
                print('####',p)
                for i,l in enumerate(s.split('\n')):
                    if re.search(r'(ApplyRamp\(|EthercatMotorControl_Stopping|CONTROL_HZ)',l):
                        print('  ',i+1,l.strip()[:150])
