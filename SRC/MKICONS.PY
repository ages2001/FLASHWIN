"""Build NT 3.1-readable 4-bit icons from the authentic Flash Player logo
(flashlogo.png) WITHOUT altering the 'f'.  SETUP adds a green install arrow;
UNINST adds a white minus.  Everything else is the user's artwork, downsampled."""
from pathlib import Path
import struct
from PIL import Image
root=Path(__file__).resolve().parent
palette=[(0,0,0),(74,0,0),(0,140,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
         (128,128,128),(255,0,0),(40,200,40),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]
MAROON,DGREEN,GREEN,WHITE=1,2,10,15
src=Image.open(root/'flashlogo.png').convert('RGB')

def icon(removing):
 frames=[]
 for size in (16,32):
  im=src.resize((size,size),Image.LANCZOS)
  px=[[None]*size for _ in range(size)]
  for y in range(size):
   for x in range(size):
    r,g,b=im.getpixel((x,y))
    px[y][x]=WHITE if (r+g+b)>=330 else MAROON   # 2-tone: keep the real 'f'
  # badge bottom-right (clear of the f)
  for y in range(size):
   for x in range(size):
    a=(x+.5)*32/size; b=(y+.5)*32/size
    if removing:
        if 21.0<=a<28.0 and 24.0<=b<26.0: px[y][x]=WHITE
    else:
        shaft=(22.0<=a<25.0 and 19.5<=b<24.0)
        head =(19.5<=a<27.5 and 23.0<=b<26.8 and abs(a-23.5)<=(26.8-b)+0.6)
        tray =(19.0<=a<28.0 and 27.0<=b<28.4)
        if shaft or head: px[y][x]=GREEN
        elif tray: px[y][x]=DGREEN
  stride=((size*4+31)//32)*4;andstride=((size+31)//32)*4;xor=bytearray(stride*size);mask=bytearray(andstride*size)
  for y in range(size):
   for x in range(size):
    v=px[y][x];row=size-1-y
    if v is None:mask[row*andstride+x//8]|=1<<(7-x%8)
    else:xor[row*stride+x//2]|=v<<(4 if x%2==0 else 0)
  dib=struct.pack('<IiiHHIIiiII',40,size,size*2,1,4,0,len(xor)+len(mask),0,0,16,0)
  dib+=b''.join(bytes((bb,g,r,0)) for r,g,bb in palette)+xor+mask;frames.append((size,dib))
 offset=6+16*len(frames);table=b'';data=b''
 for size,dib in frames:table+=struct.pack('<BBBBHHII',size,size,16,0,1,4,len(dib),offset);data+=dib;offset+=len(dib)
 return struct.pack('<HHH',0,1,len(frames))+table+data
for name,removing in [('SETUP.ICO',False),('UNINST.ICO',True)]: (root/name).write_bytes(icon(removing))
(root/'icons.rc').write_text('101 ICON "'+str(root/'SETUP.ICO')+'"\n102 ICON "'+str(root/'UNINST.ICO')+'"\n')
print("icons generated from flashlogo.png")
