import os,re
for root,_,files in os.walk('.'):
    for fn in files:
        if fn.lower().endswith(('.c','.h')):
            p=os.path.join(root,fn)
            s=open(p,'rb').read().decode('utf-8','replace')
            if 'StopRate' in s or 'ECAT_MOTOR_MAX_RAMP_RPM_PER_SEC' in s or 'ECAT_MOTOR_DECEL_RPM_PER_SEC' in s:
                print('####',p)
                for i,l in enumerate(s.split('\n')):
                    if re.search(r'(StopRate|MAX_RAMP_RPM_PER_SEC|DECEL_RPM_PER_SEC)',l):
                        print('  ',i+1,l.strip()[:150])
