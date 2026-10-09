/* Offline clear-IP PCAP review. Never opens a socket or transmits to a drone.
 * Prints only counts/timing, not identities, GPS, credentials or raw packets. */
#include "dji_neo/commands.h"
#include "link_ack.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static unsigned be16(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
typedef struct { uint64_t hash; uint16_t sequence; uint8_t count; } original_t;
static uint64_t payload_hash(const uint8_t *p,size_t size) {
    /* Private, in-memory duplicate matching only; not a cryptographic check. */
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<size;++i) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
typedef struct {
    uint16_t sid,port,peer,last_up,video;
    int have_peer,have_up,have_video;
    neo_video_rx_t *tracker;
    original_t *originals;
} session_t;
typedef struct { uint64_t osd,video,bytes,unexpected_send; } callbacks_t;
static int no_send(void *u,const uint8_t *p,size_t n) {
    (void)p; (void)n; ++((callbacks_t *)u)->unexpected_send; return -1;
}
static void telemetry(void *u,const dji_neo_telemetry_t *t) { (void)t; ++((callbacks_t *)u)->osd; }
static void video(void *u,const dji_neo_video_packet_t *v) {
    callbacks_t *c=u; ++c->video; c->bytes+=v->size;
}
static int error(const char *message) { fprintf(stderr,"capture review: %s\n",message); return 1; }
int main(int argc,char **argv) {
    if(argc!=2) return error("usage: review_capture FILE.pcap (classic LE microsecond PCAP, DLT_RAW=101)");
    FILE *f=fopen(argv[1],"rb"); if(!f) return error("cannot open input");
    uint8_t global[24],record[16],raw[65535];
    if(fread(global,1,24,f)!=24 || le32(global)!=0xa1b2c3d4 || le32(global+20)!=101 ||
       !le32(global+16)) {
        fclose(f); return error("unsupported/invalid global header");
    }
    callbacks_t c={0}; dji_neo_config_t cfg={0};
    cfg.udp_send=no_send; cfg.udp_user=&c; cfg.callback_user=&c;
    cfg.on_telemetry=telemetry; cfg.on_video=video;
    dji_neo_t *n=dji_neo_create(&cfg); if(!n) { fclose(f); return error("allocation failed"); }
    session_t sessions[64]={0}; unsigned ns=0;
    uint64_t records=0,up[6]={0},down[6]={0},valid=0,bad=0,views=0,view_bad=0;
    uint64_t wrappers_bad=0,advisory=0,truncated=0,fragments=0,receive_bad=0;
    uint64_t peer_eligible=0,peer_matches=0,ka_eligible=0,ka_matches=0,video_eligible=0,video_matches=0;
    uint64_t tracked=0,untracked=0,ack_shapes=0,ack_exact=0,ack_other=0;
    uint64_t retrans_known=0,retrans_exact=0,retrans_unknown=0,retrans_unanchored=0;
    uint64_t first=0,last=0,ms=0,video_first=0,video_last=0,max_video_gap=0,video_bytes=0;
    uint8_t tokens[256]={0}; int failure=0;
    for(;;) {
        size_t got=fread(record,1,16,f);
        if(!got) { if(ferror(f)) failure=1; break; }
        if(got!=16) { failure=1; break; }
        uint32_t size=le32(record+8),original=le32(record+12),us=le32(record+4);
        if(!size || !original || size>le32(global+16) || size>sizeof raw || size>original || us>=1000000 ||
           fread(raw,1,size,f)!=size) { failure=1; break; }
        ++records; if(size<original) ++truncated;
        uint64_t ts=(uint64_t)le32(record)*1000000+us;
        if(records==1) first=ts;
        last=ts; if(ts>=first && (ts-first)/1000>ms) ms=(ts-first)/1000;
        if(size<28 || raw[0]>>4!=4 || raw[9]!=17) continue;
        size_t ih=(raw[0]&15)*4;
        if(ih<20 || ih+8>size) continue;
        if(be16(raw+6)&0x3fff) { ++fragments; continue; }
        unsigned src=be16(raw+ih),dst=be16(raw+ih+2),un=be16(raw+ih+4);
        static const uint8_t drone[]={192,168,2,1};
        int uplink=dst==9003 && !memcmp(raw+16,drone,4);
        int downlink=src==9003 && !memcmp(raw+12,drone,4);
        if(!uplink && !downlink) continue;
        if(un<16 || ih+un>size) { ++wrappers_bad; continue; }
        const uint8_t *p=raw+ih+8; size_t len=un-8;
        uint8_t x=0; for(unsigned i=0;i<7;++i) x^=p[i];
        if(!(le16(p)&0x8000) || x!=p[7]) { ++wrappers_bad; continue; }
        if((le16(p)&0x7fff)!=len) ++advisory;
        uint8_t type=p[6]; if(type<6) ++(uplink?up:down)[type];
        const uint8_t *b=p+8; size_t bn=len-8;
        uint16_t sid=le16(p+2),port=(uint16_t)(uplink?src:dst);
        unsigned si=0; for(;si<ns;++si) if(sessions[si].sid==sid && sessions[si].port==port) break;
        if(si==ns) {
            if(ns==64) { failure=1; break; }
            sessions[ns].sid=sid; sessions[ns].port=port; ++ns;
        }
        session_t *s=&sessions[si];
        if(uplink && type==0 && bn>=2) {
            if(!s->tracker) s->tracker=calloc(1,sizeof *s->tracker);
            if(!s->originals) s->originals=calloc(256*64,sizeof *s->originals);
            if(!s->tracker || !s->originals) { failure=1; break; }
            memset(s->tracker,0,sizeof *s->tracker); neo_rx_reset(&s->tracker->rx,le16(b));
            memset(s->originals,0,256*64*sizeof *s->originals);
        }
        if(uplink && type==5) {
            if(bn<25 || !dji_neo_duml_valid(b+12,bn-12)) { ++bad; continue; }
            ++valid;
            if(s->have_peer) { ++peer_eligible; peer_matches+=le16(b)==s->peer; }
            s->last_up=le16(p+4); s->have_up=1;
            const uint8_t *d=b+12; size_t dn=bn-12;
            if(dn==23 && d[9]==0x18 && d[10]==0x47) {
                uint8_t built[23]; ++views; tokens[d[15]]=1;
                int count=dji_neo_build_liveview_ex(built,sizeof built,le16(d+6),le16(d+13),d[15],d[17]);
                if(count!=23 || memcmp(built,d,23)) ++view_bad;
            }
        }
        if(uplink && type==4 && bn>=26) {
            neo_ack_block_t first_block,second_block;
            if(s->have_up && neo_ack_parse(b,bn,&first_block) &&
               neo_ack_parse(b+first_block.size,bn-first_block.size,&second_block) &&
               bn-first_block.size-second_block.size>=4) {
                ++ka_eligible;
                ka_matches+=le16(b+first_block.size+second_block.size+2)==s->last_up;
            }
            if(s->have_video) { ++video_eligible; video_matches+=le16(b)==s->video && le16(b+2)==s->video; }
            neo_ack_block_t block;
            if(neo_ack_parse(b,bn,&block) && (uint16_t)(block.high-block.base)==block.count*8u &&
               !(block.base&7) && !(block.high&7)) {
                neo_rx_t state; neo_rx_reset(&state,block.base); state.high=block.high;
                int supported=1;
                for(unsigned i=0;i<block.count;++i) {
                    unsigned status=(block.status[i/4]>>((i%4)*2))&3;
                    if(status==3) state.pending[i/8]|=(uint8_t)(1u<<(i%8));
                    else if(status!=0) supported=0;
                }
                if(supported) {
                    uint8_t encoded[NEO_ACK_MAX_SIZE]; size_t en=neo_rx_encode(&state,encoded);
                    ++ack_shapes; ack_exact+=en==block.size && !memcmp(encoded,b,en);
                } else ++ack_other;
            } else ++ack_other;
        }
        if(downlink && type==1 && bn>=24) { s->peer=le16(b+16); s->have_peer=1; }
        if(downlink && type==2) {
            if(!video_first) video_first=ts;
            if(video_last && ts>=video_last && ts-video_last>max_video_gap) max_video_gap=ts-video_last;
            video_last=ts; video_bytes+=bn;
            s->video=(uint16_t)(le16(p+4)&0xfff8); s->have_video=1;
            if(s->tracker) {
                if(neo_video_arrive(s->tracker,le16(p+4),b,bn)) ++tracked;
                else ++untracked;
                if(bn>=12) {
                    unsigned count=b[9]&127,index=(b[10]&31)*2+(b[9]>>7),flags=le16(p+4)&7;
                    if(count && count<=64 && index<count) {
                        original_t *o=&s->originals[b[8]*64u+index];
                        uint64_t hash=payload_hash(b+12,bn-12);
                        if(!flags) *o=(original_t){hash,le16(p+4),(uint8_t)count};
                        else if(flags==2 || flags==4) {
                            if(o->count==count && o->hash==hash) {
                                neo_video_message_t *m=&s->tracker->messages[b[8]];
                                if(m->known && m->count==count) {
                                    ++retrans_known;
                                    retrans_exact+=(uint16_t)(m->base+(index+1)*8)==o->sequence;
                                } else ++retrans_unanchored;
                            } else ++retrans_unknown;
                        }
                    }
                }
            }
        }
        if(downlink && (type==1 || type==2 || type==3))
            receive_bad+=dji_neo_on_datagram(n,p,len,ms)!=DJI_NEO_OK;
    }
    fclose(f); dji_neo_destroy(n);
    unsigned outstanding=0; uint64_t invalid_meta=0,outside=0,conflicts=0,unanchored=0;
    for(unsigned i=0;i<ns;++i) {
        if(sessions[i].tracker) {
            neo_video_rx_t *v=sessions[i].tracker;
            outstanding+=(uint16_t)(v->rx.high-v->rx.ack)/8;
            invalid_meta+=v->invalid; outside+=v->outside_window;
            conflicts+=v->conflicts; unanchored+=v->unanchored;
        }
        free(sessions[i].tracker);
        free(sessions[i].originals);
    }
    if(failure) return error("incomplete/corrupt record stream or session capacity exceeded");
    printf("records=%"PRIu64" duration=%.3fs sessions=%u truncated=%"PRIu64" fragments=%"PRIu64"\n",
        records,(last-first)/1e6,ns,truncated,fragments);
    printf("uplink CONNECT=%"PRIu64" KEEPALIVE=%"PRIu64" TYPE5=%"PRIu64"; valid DUML=%"PRIu64" invalid=%"PRIu64"\n",up[0],up[4],up[5],valid,bad);
    printf("SDK callbacks: OSD=%"PRIu64" video=%"PRIu64" bytes=%"PRIu64"; receive errors=%"PRIu64" unexpected sends=%"PRIu64"\n",c.osd,c.video,c.bytes,receive_bad,c.unexpected_send);
    printf("video span=%.3fs max gap=%.3fs; liveview byte-exact=%"PRIu64"/%"PRIu64" opaque tokens:",video_last?(video_last-video_first)/1e6:0,max_video_gap/1e6,views-view_bad,views);
    for(unsigned i=0;i<256;++i) if(tokens[i]) printf(" %02x",i);
    printf("\nObserved latest-state fits (not required to be 100%%): RC ACK=%"PRIu64"/%"PRIu64"; KA sent=%"PRIu64"/%"PRIu64"; KA video=%"PRIu64"/%"PRIu64"\n",peer_matches,peer_eligible,ka_matches,ka_eligible,video_matches,video_eligible);
    printf("wrapper failures=%"PRIu64" advisory-length mismatches=%"PRIu64"\n",wrappers_bad,advisory);
    printf("ACK first-block byte-exact=%"PRIu64"/%"PRIu64" other shapes=%"PRIu64"; video tracked=%"PRIu64" untracked=%"PRIu64" outstanding at EOF=%u\n",ack_exact,ack_shapes,ack_other,tracked,untracked,outstanding);
    printf("video tracker diagnostics: metadata=%"PRIu64" window=%"PRIu64" lineage=%"PRIu64" unanchored=%"PRIu64"\n",invalid_meta,outside,conflicts,unanchored);
    printf("Recovered original sequence matches=%"PRIu64"/%"PRIu64"; original absent=%"PRIu64"; original present but unanchored=%"PRIu64"\n",retrans_exact,retrans_known,retrans_unknown,retrans_unanchored);
    if(outstanding || untracked || unanchored)
        printf("LIMIT: unresolved receive history; this replay does not demonstrate sustained ACK recovery.\n");
    return wrappers_bad || bad || view_bad || receive_bad || c.unexpected_send || c.video!=down[2] || c.bytes!=video_bytes || !valid || ack_exact!=ack_shapes || retrans_exact!=retrans_known;
}
