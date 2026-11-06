# Tiny AS2 bytecode helpers
import struct
def P(*vals):
    body=b''
    for v in vals:
        if isinstance(v,bool): body+=b'\x05'+bytes([1 if v else 0])
        elif isinstance(v,str): body+=b'\x00'+v.encode('latin1')+b'\0'
        elif isinstance(v,int): body+=b'\x07'+struct.pack('<i',v)
        elif v is None: body+=b'\x03'
        else:
            d=struct.pack('<d',v); body+=b'\x06'+d[4:]+d[:4]
    return b'\x96'+struct.pack('<H',len(body))+body
OP=dict(ADD=b'\x47',SUB=b'\x0b',MUL=b'\x0c',DIV=b'\x0d',MOD=b'\x3f',EQ=b'\x49',LT=b'\x48',GT=b'\x67',NOT=b'\x12',
        GETV=b'\x1c',SETV=b'\x1d',GETM=b'\x4e',SETM=b'\x4f',TOINT=b'\x18',INC=b'\x50',POP=b'\x17',CALLM=b'\x52',CALLF=b'\x3d',DUP=b'\x4c',TOSTR=b'\x4b',TONUM=b'\x4a')
globals().update(OP)
def V(n): return P(n)+GETV
def setv(n,expr): return P(n)+expr+SETV
def mem(obj,prop): return V(obj)+P(prop)+GETM
def setmem(obj,prop,expr): return V(obj)+P(prop)+expr+SETM
def method(objexpr,name,*args): return b''.join(reversed([a if isinstance(a,bytes) else P(a) for a in args]))+P(len(args))+objexpr+P(name)+CALLM
def math(fn,*args): return method(V('Math'),fn,*args)
def call(fn,*args): return b''.join(reversed([a if isinstance(a,bytes) else P(a) for a in args]))+P(len(args))+P(fn)+CALLF
def inc(n): return setv(n,V(n)+P(1)+ADD)
def while_(cond,body):
    jif=b'\x9d'+struct.pack('<Hh',2,len(body)+5)
    back=-(len(cond)+1+len(jif)+len(body)+5)
    return cond+NOT+jif+body+b'\x99'+struct.pack('<Hh',2,back)
def if_(cond,body):
    return cond+NOT+b'\x9d'+struct.pack('<Hh',2,len(body))+body
def for_(var,n,body): return setv(var,P(0))+while_(V(var)+P(n)+LT,body+inc(var))
def out(label,expr): return setv('out',V('out')+P(label+'=')+ADD+expr+ADD+P('\r')+ADD)
def full(expr_name):
    # value with 15 digits plus residual beyond them
    return V(expr_name)+P(' r')+ADD+ (V(expr_name)+call('Number',call('String',V(expr_name)))+SUB) +ADD
