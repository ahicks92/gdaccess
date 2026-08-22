"""Unpack a Grim Dawn .arc (v3, LZ4 parts) offline -- e.g. resources/Text_EN.arc for every localized string (tagUse=Interact).
Usage: uv run --with lz4 tools/arc_unpack.py  (edit p / the print below). Left by an RE agent, 2026-08-22."""
import struct, zlib, lz4.block
p=r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\resources\Text_EN.arc"
d=open(p,'rb').read()
magic,ver,numE,numP,recSize,strSize,recOff=struct.unpack_from("<IIIIIII",d,0)
parts=[struct.unpack_from("<III",d,recOff+12*i) for i in range(numP)]
strs=d[recOff+recSize:recOff+recSize+strSize]
ent=recOff+recSize+strSize
for i in range(numE):
    typ,off,csz,usz,adler,ft1,ft2,np,pi,slen,so=struct.unpack_from("<11I",d,ent+44*i)
    nm=strs[so:strs.index(b'\0',so)].decode(errors='replace')
    blob=b""
    for j in range(pi,pi+np):
        o,c,u=parts[j]
        chunk=d[o:o+c]
        blob += chunk if c==u else lz4.block.decompress(chunk, uncompressed_size=u)
    print("===",nm,len(blob))
    txt=blob.decode('utf-8-sig',errors='replace')
    for line in txt.splitlines():
        if line.split('=')[0] in ("tagUse","tagPickup","tagForceMove","tagMove","tagEvade","tagToggleUI","tagAltarWindow","tagSelectAllPets","tagMoveForward","tagSkillReclamationMode","tagPushToTalk"):
            print("   ", line)
