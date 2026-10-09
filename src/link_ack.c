#include "link_ack.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static int pending(const neo_rx_t *rx, unsigned index) {
    unsigned slot=(rx->head+index)%NEO_RX_WINDOW;
    return (rx->pending[slot/8] >> (slot%8)) & 1;
}
void neo_rx_reset(neo_rx_t *rx, uint16_t seed) {
    memset(rx,0,sizeof *rx); rx->ack=rx->high=seed;
}
int neo_rx_arrive(neo_rx_t *rx, uint16_t sequence) {
    uint16_t delta=(uint16_t)(sequence-rx->ack);
    if (delta&7) return 0;
    if (!delta || delta>=0x8000) return 1; /* well-formed duplicate/stale packet */
    if ((delta&7) || delta/8>NEO_RX_WINDOW) return 0;
    if (delta>(uint16_t)(rx->high-rx->ack)) rx->high=sequence;
    unsigned slot=(rx->head+delta/8-1)%NEO_RX_WINDOW;
    rx->pending[slot/8] |= (uint8_t)(1u<<(slot%8));
    while (pending(rx,0)) {
        rx->pending[rx->head/8] &= (uint8_t)~(1u<<(rx->head%8));
        rx->head=(rx->head+1)%NEO_RX_WINDOW;
        rx->ack=(uint16_t)(rx->ack+8);
    }
    return 1;
}
int neo_rx_announce(neo_rx_t *rx, uint16_t high) {
    uint16_t delta=(uint16_t)(high-rx->ack);
    if ((delta&7) || delta>=0x8000 || delta/8>NEO_RX_WINDOW) return 0;
    if (delta>(uint16_t)(rx->high-rx->ack)) rx->high=high;
    return 1;
}
size_t neo_rx_encode(const neo_rx_t *rx, uint8_t *out) {
    unsigned count=(uint16_t)(rx->high-rx->ack)/8;
    put16(out,rx->ack); put16(out+2,rx->high);
    put16(out+4,(uint16_t)count); put16(out+6,(uint16_t)(count!=0));
    size_t bytes=(count+3)/4;
    /* Unused entries are 3, as in real Fly extension bytes fc/f0/c0. */
    memset(out+8,0xff,bytes);
    for (unsigned i=0;i<count;++i) {
        unsigned shift=(i%4)*2;
        uint8_t status=pending(rx,i) ? 3 : 0; /* received vs request retransmit */
        out[8+i/4]=(uint8_t)((out[8+i/4] & ~(3u<<shift)) | ((unsigned)status<<shift));
    }
    return 8+bytes;
}
int neo_ack_parse(const uint8_t *p, size_t size, neo_ack_block_t *out) {
    if (!p || !out || size<8) return 0;
    uint16_t tag=le16(p+6), count=le16(p+4);
    if (tag>1) return 0;
    size_t extra=tag ? (count+3u)/4u : 0;
    if ((tag && count>NEO_RX_WINDOW) || extra>size-8) return 0;
    *out=(neo_ack_block_t){le16(p),le16(p+2),tag ? count : 0,p+8,8+extra};
    return 1;
}
static neo_video_message_t *message(neo_video_rx_t *v, uint64_t generation) {
    neo_video_message_t *m=&v->messages[generation&255];
    return m->count && m->generation==generation ? m : NULL;
}
int neo_video_arrive(neo_video_rx_t *v, uint16_t wrapper, const uint8_t *b, size_t size) {
    if (!v || !b || size<12 || (le16(b)&7)) { if(v) ++v->invalid; return 0; }
    unsigned flags=wrapper&7, count=b[9]&127;
    /* Observed packed pair-index occupies the low five bits; upper bits and
     * byte 11 vary independently and must not be interpreted as an LE u16. */
    unsigned index=(b[10]&31u)*2+(b[9]>>7);
    if (!count || count>64 || index>=count || (flags!=0 && flags!=2 && flags!=4) ||
        (!flags && (wrapper&0xfff8)!=le16(b+2))) { ++v->invalid; return 0; }
    if(flags) ++v->retransmits;
    if (!flags) {
        uint16_t delta=(uint16_t)((wrapper&0xfff8)-v->rx.ack);
        if (!delta || delta>=0x8000) return 1;
        if (delta/8>NEO_RX_WINDOW) { ++v->outside_window; return 0; }
    }
    if (!v->initialized) { v->latest=256u+b[8]; v->initialized=1; }
    int distance=(int)((b[8]-(v->latest&255)+128)&255)-128;
    uint64_t generation=distance<0 ? v->latest-(unsigned)-distance : v->latest+(unsigned)distance;
    if (generation>v->latest) v->latest=generation;
    neo_video_message_t *m=&v->messages[b[8]];
    if (m->generation!=generation || m->count!=count)
        *m=(neo_video_message_t){.generation=generation,.count=(uint8_t)count};
    if (!flags) {
        uint16_t base=(uint16_t)((wrapper&0xfff8)-(index+1)*8);
        if (m->known==1 && m->base!=base) { ++v->conflicts; return 0; }
        m->base=base; m->known=1;
    }
    /* A completely lost message can be anchored by its known neighbour once
     * retransmitted metadata supplies the missing message's fragment count. */
    for (unsigned pass=0;pass<128;++pass) {
        int changed=0;
        for (unsigned i=0;i<256;++i) {
            neo_video_message_t *x=&v->messages[i];
            if (!x->count || x->known || v->latest-x->generation>=128) continue;
            neo_video_message_t *prev=message(v,x->generation-1), *next=message(v,x->generation+1);
            int have_prev=prev && prev->known, have_next=next && next->known;
            uint16_t a=have_prev ? (uint16_t)(prev->base+prev->count*8) : 0;
            uint16_t z=have_next ? (uint16_t)(next->base-x->count*8) : 0;
            if ((have_prev || have_next) && !(have_prev && have_next && a!=z)) {
                x->base=have_prev ? a : z; x->known=2; changed=1;
            }
        }
        if (!changed) break;
    }
    /* Retransmission wrapper endpoints cover multiple outstanding messages;
     * body[2:4] is not necessarily the same endpoint. Neither identifies this
     * fragment's original sequence: only the message lineage below does. */
    uint16_t endpoint=!flags ? (uint16_t)(m->base+count*8) : (uint16_t)(wrapper&0xfff8);
    neo_rx_announce(&v->rx,endpoint);
    if (!m->known) { ++v->unanchored; return 1; } /* request range, not fabricated receipt */
    return neo_rx_arrive(&v->rx,(uint16_t)(m->base+(index+1)*8));
}
