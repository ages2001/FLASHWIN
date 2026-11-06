import struct,sys,importlib.util
sys.path.insert(0,'/tmp/claude-0/sc/swfgen')
from as2 import *
spec=importlib.util.spec_from_file_location('mk','/tmp/claude-0/sc/swfgen/mk_lib.py');mk=importlib.util.module_from_spec(spec);spec.loader.exec_module(mk)
INITARR=b'\x42';INITOBJ=b'\x43';TYPEOF=b'\x44'
def d(prop): return V('so')+P('data')+GETM+P(prop)+GETM
def setd(prop,expr): return V('so')+P('data')+GETM+P(prop)+expr+SETM
c=setv('so',method(V('SharedObject'),'getLocal','sotest'))+setv('out',P('so=')+V('so')+TYPEOF+ADD+P('\r')+ADD)
w=setd('v',P(1))+setd('n',P(3.25))+setd('s',P('hello world'))+setd('b',P(True))
w+=setd('arr',P(False)+P('x')+P(3)+P(2)+P(1)+P(5)+INITARR)
w+=setd('obj',P('a',1)+P('b','two')+P(2)+INITOBJ)
w+=setv('big',P(0)+INITARR)+for_('k',300,V('big')+V('k')+V('k')+P(1.5)+MUL+P(0.001)+ADD+SETM)+setd('big',V('big'))
w+=setd('runs',P(1))
w+=out('written; flush',method(V('so'),'flush'))
r=setd('runs',d('runs')+P(1)+ADD)
r+=out('v',d('v'))+out('n',d('n'))+out('s',d('s'))+out('b',d('b')+P(' ')+ADD+d('b')+TYPEOF+ADD)
r+=out('arr',d('arr')+P('length')+GETM+P(' ')+ADD+(d('arr')+P(3)+GETM)+ADD+P(' ')+ADD+(d('arr')+P(4)+GETM)+ADD)
r+=out('obj',d('obj')+P('a')+GETM+P(' ')+ADD+(d('obj')+P('b')+GETM)+ADD)
r+=setv('sum',P(0.0))+for_('k',300,setv('sum',V('sum')+(d('big')+V('k')+GETM)+ADD))
r+=out('big',d('big')+P('length')+GETM+P(' sum=')+ADD+V('sum')+ADD)
r+=out('runs',d('runs'))+out('flush',method(V('so'),'flush'))
c+=b''  # if (so.data.v == undefined) write else read
cond=d('v')+P(None)+EQ
c+=cond+b'\x9d'+struct.pack('<Hh',2,len(r)+5)+r+b'\x99'+struct.pack('<Hh',2,len(w))+w
import math
ref=sum(k*1.5+0.001 for k in range(300))
c+=out('expect big sum',P(ref))+b'\x07'
f1=0x80|0x40|0x20|0x08|0x04|0x01;f2=0x10|0x20
ed=struct.pack('<H',5)+mk.rect(0,12400,0,9400)+bytes([f1,f2])+struct.pack('<HH',1,16*20)+bytes((255,255,255,255))+struct.pack('<BHHHh',0,40,40,0,0)+b'out\0'+b'\0'
T=[mk.tag(9,bytes([0,0,64])),mk.font2(1,'Arial'),mk.tag(37,ed),mk.place(5,1,100),mk.doaction(c),mk.tag(1,b''),mk.tag(0,b'')]
body=mk.rect(0,640*20,0,480*20)+struct.pack('<HH',12<<8,1)+b''.join(T)
o=b'FWS'+bytes([8])+struct.pack('<I',8+len(body))+body
open(sys.argv[1],'wb').write(o);print(len(o),ref)
