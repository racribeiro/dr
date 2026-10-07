import struct
from collections import Counter, defaultdict
NEO="192.168.2.1"
def read_pcap(path):
    d=open(path,"rb").read(); end="<" if d[:4]==b"\xd4\xc3\xb2\xa1" else ">"; o=24
    while o+16<=len(d):
        ts,tu,cl,ol=struct.unpack(end+"IIII",d[o:o+16]); o+=16; yield ts+tu/1e6,d[o:o+cl]; o+=cl
def ip(raw):
    b=raw
    if len(b)<20 or b[0]>>4!=4: return None
    ihl=(b[0]&0xf)*4
    if b[9]!=17: return None
    return ".".join(map(str,b[12:16])),".".join(map(str,b[16:20])),b[ihl+8:]
import sys as _s
path=_s.argv[1] if len(_s.argv)>1 else "gimbalmove-sanitized.pcap"
rd16=lambda p:p[0]|(p[1]<<8)
ups=[]
for ts,raw in read_pcap(path):
    r=ip(raw)
    if not r: continue
    s,dd,pl=r
    if dd==NEO and len(pl)>=8:
        ups.append((ts,pl[6],rd16(pl[4:6]),len(pl)-8,pl[8:]))  # ts,type,f45,bodylen,body

print("=== A. field45 rule ===")
# f45 by type
by=defaultdict(list)
for ts,t,f,bl,b in ups: by[t].append(f)
for t in sorted(by):
    vals=by[t]
    print(f" type {t}: n={len(vals)} f45 set-size={len(set(vals))} min={min(vals):#06x} max={max(vals):#06x} all_zero={all(v==0 for v in vals)}")
# step of f45 on consecutive type-5
t5=[f for ts,t,f,bl,b in ups if t==5]
steps=Counter((t5[i+1]-t5[i])&0xffff for i in range(len(t5)-1))
print(" consecutive type-5 f45 deltas (top):", steps.most_common(5))
# does a NON-type5 frame between two type-5 change the step? check global order
seq=[(t,f) for ts,t,f,bl,b in ups]
print(" first 12 (type,f45):", [(t,hex(f)) for t,f in seq[:12]])

print("\n=== B. keepalive (type-4) body ===")
ka=[(bl,b) for ts,t,f,bl,b in ups if t==4]
lens=Counter(bl for bl,b in ka)
print(" body lengths:", dict(lens))
uniq=[]
seen=set()
for bl,b in ka:
    h=b.hex()
    if h not in seen: seen.add(h); uniq.append((bl,h))
print(f" distinct bodies: {len(uniq)} (showing up to 8)")
for bl,h in uniq[:8]: print(f"   len={bl}: {h}")
