"""Exact function bounds from the exe dump's .pdata (RUNTIME_FUNCTION table). Usage: uv run --with pefile tools/pdata.py <rva-hex>."""
import struct,sys,os
ROOT=r"D:\projects\in_progress\gdaccess"
img=open(os.path.join(ROOT,"build","GrimDawn.unpacked.bin"),"rb").read()
e=struct.unpack_from("<I",img,0x3c)[0]; opt=e+0x18
magic=struct.unpack_from("<H",img,opt)[0]
dd=opt+(0x70 if magic==0x20b else 0x60)+8*3
rva,size=struct.unpack_from("<II",img,dd)
ents=[]
for o in range(rva,rva+size,12):
    b,en,u=struct.unpack_from("<III",img,o)
    if b==0 and en==0: continue
    ents.append((b,en))
ents.sort()
def find(x):
    lo,hi=0,len(ents)-1;r=None
    for b,en in ents:
        if b<=x<en: return (b,en)
    return None
for a in sys.argv[1:]:
    x=int(a,16)
    f=find(x)
    print(f"{x:#x} -> " + (f"fn exe+{f[0]:#x} .. exe+{f[1]:#x} (size {f[1]-f[0]:#x})" if f else "no pdata entry"))
