"""Execute delivered MAIN ST; no TwinCAT runtime or hardware."""
import unittest
from st_subset import Parser, execute, fields, source, uncomment, zero, value

TREE=Parser(source('PLC/POUs/MAIN.TcPOU','ST')).block()
def globals_env(name):
    text=uncomment(source('PLC/GVLs/'+name+'.TcGVL','Declaration')).split('VAR_GLOBAL')[1].split('END_VAR')[0]
    result={}
    for part in text.split(';'):
        if not part.strip():continue
        left,right=part.split(':',1)
        name=left.split(' AT ')[0].strip()
        typ,sep,default=right.partition(':=')
        result[name]=value(Parser(default).expression(),{}) if sep else zero(typ.strip())
    return result
class Bench:
    def __init__(self):
        decl=source('PLC/POUs/MAIN.TcPOU','Declaration').split('VAR',1)[1].split('END_VAR')[0]
        self.e=fields(decl)
        self.e.update({n:globals_env(n) for n in ('GVL_HM','GVL_FCM_IO','GVL_STEP_IO')})
        self.e['_TaskInfo']={1:{'CycleTime':20000,'CycleTimeExceeded':False}}
        self.g=self.e['GVL_HM'];self.io=self.e['GVL_FCM_IO']
        self.io.update(iStatusWord=0x40,iModeDisplay=6,iEthercatState=8,iActualPosition=123)
    def scan(self,n=1,follow=False):
        for _ in range(n):
            if follow:self.io['iStatusWord']={0:0x40,6:0x21,7:0x23,15:0x427}.get(self.io['qControlWord'],self.io['iStatusWord'])
            execute(TREE,self.e)
            assert self.e['GVL_STEP_IO']['qControlWord']==0
            assert self.io['qMode']==6 and self.io['qTargetVelocity']==0
            assert self.io['qTargetPosition']==self.io['iActualPosition']
    def enable(self):
        self.scan();self.g['bEnable']=True;self.scan(30,True)
        assert self.g['bReady'],self.g
    def start(self):
        self.g['bStart']=True;self.scan();self.io['iStatusWord']=0x27;self.scan()
        assert self.g['bBusy'] and self.io['qControlWord']==31
class Tests(unittest.TestCase):
    def test_disabled(self):
        b=Bench();b.scan(5);self.assertEqual(b.io['qControlWord'],0)
    def test_enable_and_start(self):
        b=Bench();b.enable();b.start()
    def test_already_high_enable_needs_edge(self):
        b=Bench();b.g['bEnable']=True;b.scan(100,True);self.assertFalse(b.g['bReady'])
        self.assertEqual(b.io['qControlWord'],0)
        b.g['bEnable']=False;b.scan();b.g['bEnable']=True;b.scan(30,True);self.assertTrue(b.g['bReady'])
    def test_early_start_not_queued(self):
        b=Bench();b.g['bStart']=True;b.enable();b.scan(20)
        self.assertTrue(b.g['bReady']);self.assertEqual(b.io['qControlWord'],15)
    def test_done_one_per_enable(self):
        b=Bench();b.enable();b.start();b.io['iStatusWord']=0x1427;b.scan(3)
        self.assertTrue(b.g['bDone']);b.g['bStart']=True;b.scan()
        self.assertTrue(b.g['bDone']);self.assertEqual(b.io['qControlWord'],15)
    def test_cancel_waits_for_stop(self):
        b=Bench();b.enable();b.start();b.g['bCancel']=True;b.scan(3)
        self.assertEqual(b.io['qControlWord'],271);self.assertTrue(b.g['bBusy'])
        b.io['iStatusWord']=0x427;b.scan(2);self.assertTrue(b.g['bCanceled'])
        self.assertFalse(b.g['bDone'])
    def test_disable_immediate(self):
        b=Bench();b.enable();b.start();b.g['bEnable']=False;b.scan()
        self.assertEqual(b.io['qControlWord'],0)
    def test_wc_and_bus_latched(self):
        for key,val in [('iWcState',True),('iEthercatState',4)]:
            b=Bench();b.enable();b.start();b.io[key]=val;b.scan()
            self.assertEqual(b.g['nError'],1);self.assertEqual(b.io['qControlWord'],0)
            b.io.update(iWcState=False,iEthercatState=8);b.scan(40,True)
            self.assertEqual(b.io['qControlWord'],0)
    def test_wrong_period(self):
        b=Bench();b.enable();b.e['_TaskInfo'][1]['CycleTime']=10000;b.scan()
        self.assertEqual(b.g['nError'],2);self.assertEqual(b.io['qControlWord'],0)
    def test_three_consecutive_overruns(self):
        b=Bench();b.enable();b.e['_TaskInfo'][1]['CycleTimeExceeded']=True;b.scan(2)
        self.assertEqual(b.g['nError'],0);b.scan();self.assertEqual(b.g['nError'],2)
    def test_drive_fault(self):
        b=Bench();b.enable();b.io['iStatusWord']=8;b.scan()
        self.assertEqual(b.g['nError'],3);self.assertEqual(b.io['qControlWord'],0)
    def test_homing_error(self):
        b=Bench();b.enable();b.start();b.io['iStatusWord']=0x2427;b.scan()
        self.assertEqual(b.g['nError'],6)
    def test_mode_loss_same_scan(self):
        b=Bench();b.enable();b.io['iModeDisplay']=8;b.g['bStart']=True;b.scan()
        self.assertEqual(b.g['nError'],7);self.assertEqual(b.io['qControlWord'],0)
    def test_enable_and_hm_timeout(self):
        b=Bench();b.scan();b.g['bEnable']=True;b.scan();b.e['nCounter']=1499;b.scan()
        self.assertEqual(b.g['nError'],4)
        b=Bench();b.enable();b.start();b.e['nCounter']=15499;b.scan()
        self.assertEqual(b.g['nError'],5);self.assertEqual(b.io['qControlWord'],0)
    def test_reset_never_enables(self):
        b=Bench();b.scan();b.io['iStatusWord']=8;b.g['bReset']=True;b.scan()
        self.assertEqual(b.io['qControlWord'],128);b.scan(50)
        self.assertEqual(b.io['qControlWord'],0);self.assertFalse(b.g['bReady'])
    def test_forced_start_not_queued(self):
        b=Bench();b.scan();b.g['bEnable']=True
        for _ in range(35):
            b.g['bStart']=True;b.scan(follow=True)
        self.assertTrue(b.g['bReady']);self.assertEqual(b.io['qControlWord'],15)
    def test_held_reset_is_bounded(self):
        b=Bench();b.scan();b.io['iStatusWord']=8
        for _ in range(55):
            b.g['bReset']=True;b.scan()
        self.assertEqual(b.io['qControlWord'],0);self.assertFalse(b.g['bReady'])
if __name__=='__main__':unittest.main(verbosity=2)
