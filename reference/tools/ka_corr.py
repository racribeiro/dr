import struct
NEO="192.168.2.1"
def read_pcap(path):
    d=open(path,"rb").read(); end="<" if d[:4]==b"\xd4\xc3\xb2\xa1" else ">"; o=24
    while o+16<=len(d):
        ts,tu,cl,ol=struct.unpack(end+"IIII",d[o:o+16]); o+=16; yield d[o:o+cl]; o+=cl
def ip(raw):
    b=raw
    if len(b)<20 or b[0]>>4!=4: return None
    ihl=(b[0]&0xf)*4
    if b[9]!=17: return None
    return ".".join(map(str,b[12:16])),".".join(map(str,b[16:20])),b[ihl+8:]
import sys as _s
path=_s.argv[1] if len(_s.argv)>1 else "gimbalmove-sanitized.pcap"
rd16=lambda p:p[0]|(p[1]<<8)
last_t5_f45=None; match=0; tot=0; samples=[]
for raw in read_pcap(path):
    r=ip(raw)
    if not r: continue
    s,dd,pl=r
    if dd!=NEO or len(pl)<8: continue
    t=pl[6]; f45=rd16(pl[4:6])
    if t==5: last_t5_f45=f45
    elif t==4 and len(pl)-8>=20 and last_t5_f45 is not None:
        body=pl[8:]; field6=rd16(body[18:20]); tot+=1
        # keepalive body field6 vs most-recent type-5 f45 (expect field6 == last_t5_f45 + 8, the NEXT f45)
        if field6==((last_t5_f45+8)&0xffff) or field6==last_t5_f45: match+=1
        if len(samples)<6: samples.append((hex(last_t5_f45),hex(field6)))
print(f"keepalives checked={tot} body[18:20]==recent type-5 f45(±8): {match} ({100*match//max(tot,1)}%)")
print(" samples (last_t5_f45, ka_body_field6):", samples)
# also: are fields 1,2,3 (c0b8,66a0,84d0) constant across whole session?
f1=set(); f2=set(); f3=set()
for raw in read_pcap(path):
    r=ip(raw)
    if not r: continue
    s,dd,pl=r
    if dd==NEO and len(pl)>=8 and pl[6]==4 and len(pl)-8>=18:
        b=pl[8:]; f1.add(rd16(b[0:2])); f2.add(rd16(b[8:10])); f3.add(rd16(b[16:18]))
print(" field0(0:2) values:",[hex(x) for x in f1]," field2(8:10):",[hex(x) for x in f2]," field4(16:18):",[hex(x) for x in f3])
