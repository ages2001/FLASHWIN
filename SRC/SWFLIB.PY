import struct,zlib,sys
def tag(code,data):
    if len(data)<0x3f: return struct.pack('<H',(code<<6)|len(data))+data
    return struct.pack('<HI',(code<<6)|0x3f,len(data))+data
def rect(x0,x1,y0,y1):
    vals=[x0,x1,y0,y1];nb=max(v.bit_length() for v in vals)+1
    bits='{:05b}'.format(nb)+''.join(format(v&((1<<nb)-1),'0%db'%nb) for v in vals)
    bits+='0'*((8-len(bits)%8)%8)
    return bytes(int(bits[i:i+8],2) for i in range(0,len(bits),8))
def s(x): return x.encode('latin1')+b'\0'
def font2(fid,name,bold=0,ital=0):
    fl=0x80*0|(0x04)|(2 if ital else 0)|(1 if bold else 0)
    n=name.encode('latin1')
    return tag(75,struct.pack('<HBBB',fid,fl,1,len(n))+n+struct.pack('<HH',0,2))
def edit(cid,fid,y,var,text,html=True,h=20*20,col=(255,204,0)):
    f1=0x80|0x40|0x20|0x08|0x04|0x01  # HasText ReadOnly HasTextColor HasFont
    f2=0x10|(0x02 if html else 0)|0x20 # NoSelect HTML HasLayout
    d=struct.pack('<H',cid)+rect(0,9000,0,1400)+bytes([f1,f2])+struct.pack('<HH',fid,h)+bytes(col+(255,))+struct.pack('<BHHHh',0,40,40,0,40)+s(var)+s(text)
    return tag(37,d)
def place(cid,depth,y):
    # PlaceObject2 with matrix translate
    mat='0'+'1'  # no scale no rotate, translate
    nb=16
    tx,ty=200,y
    bits='0'+'0'+'{:05b}'.format(nb)+format(tx&0xffff,'016b')+format(ty&0xffff,'016b')
    bits+='0'*((8-len(bits)%8)%8)
    m=bytes(int(bits[i:i+8],2) for i in range(0,len(bits),8))
    return tag(26,bytes([0x06])+struct.pack('<HH',depth,cid)+m)
def push_str(*ss):
    body=b''.join(b'\0'+s(x) for x in ss)
    return b'\x96'+struct.pack('<H',len(body))+body
def doaction(code): return tag(12,code+b'\0')
def setvar(name,val): return push_str(name,val)+b'\x1d'
