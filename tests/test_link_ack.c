#include "link_ack.h"
#include <assert.h>
#include <string.h>

static void put(uint8_t *b, uint16_t v) { b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); }
static void codec(void) {
    neo_rx_t rx; neo_rx_reset(&rx,0xfff0);
    uint8_t b[NEO_ACK_MAX_SIZE+1]; neo_ack_block_t block;
    assert(neo_rx_encode(&rx,b)==8);
    const uint8_t empty[]={0xf0,0xff,0xf0,0xff,0,0,0,0};
    assert(!memcmp(b,empty,8));
    assert(neo_rx_announce(&rx,0x20)); /* six missing positions across wrap */
    assert(neo_rx_encode(&rx,b)==10 && b[8]==0 && b[9]==0xf0);
    assert(neo_rx_arrive(&rx,0x08)); /* position 3 received, 1/2 still missing */
    assert(neo_rx_encode(&rx,b)==10 && b[8]==0x30 && b[9]==0xf0);
    assert(neo_ack_parse(b,10,&block) && block.base==0xfff0 && block.high==0x20 && block.count==6 && block.size==10);
    assert(!neo_ack_parse(b,9,&block));
    assert(!neo_ack_parse(NULL,10,&block));
    assert(!neo_ack_parse(b,10,NULL));
    b[6]=2; assert(!neo_ack_parse(b,10,&block)); b[6]=1;
    put(b+4,1025); assert(!neo_ack_parse(b,sizeof b,&block));
    assert(!neo_rx_arrive(&rx,0xfff9));
    assert(!neo_rx_announce(&rx,0xfff9));
    assert(!neo_rx_arrive(&rx,(uint16_t)(rx.ack+1025*8)));
    assert(neo_rx_arrive(&rx,0xfff8) && rx.ack==0xfff8);
    assert(neo_rx_arrive(&rx,0x00) && rx.ack==0x08);
    assert(neo_rx_arrive(&rx,0x00) && rx.ack==0x08); /* stale/duplicate */
    for(unsigned i=2;i<=4;++i) assert(neo_rx_arrive(&rx,(uint16_t)(i*8)));
    assert(rx.ack==0x20 && neo_rx_encode(&rx,b)==8);
    /* Full bounded window, no overflow, including packed padding and ring wrap. */
    neo_rx_reset(&rx,0); assert(neo_rx_arrive(&rx,8192));
    assert(neo_rx_encode(&rx,b)==NEO_ACK_MAX_SIZE && b[NEO_ACK_MAX_SIZE-1]==0xc0);
    assert(neo_ack_parse(b,NEO_ACK_MAX_SIZE,&block) && block.count==1024);
    for(unsigned i=1;i<1024;++i) assert(neo_rx_arrive(&rx,(uint16_t)(i*8)));
    assert(rx.ack==8192 && neo_rx_encode(&rx,b)==8);
}
static int fragment(neo_video_rx_t *v, uint8_t id, unsigned count, unsigned index,
                    uint16_t wrapper, uint16_t body_high) {
    uint8_t b[12]={0}; put(b,v->rx.ack); put(b+2,body_high);
    b[8]=id; b[9]=(uint8_t)(count|((index&1)<<7));
    b[10]=(uint8_t)(0xe0|(index/2)); b[11]=0xa5; /* opaque bits must be ignored */
    return neo_video_arrive(v,wrapper,b,sizeof b);
}
static void retransmission(void) {
    neo_video_rx_t v={0}; neo_rx_reset(&v.rx,0);
    /* Two three-fragment messages. Last fragment anchors each message; both
     * retransmissions use the same newest wrapper endpoint, not original seq. */
    assert(fragment(&v,7,3,2,24,24));
    assert(fragment(&v,8,3,2,48,48));
    assert(v.rx.ack==0 && v.rx.high==48);
    uint8_t b[NEO_ACK_MAX_SIZE]; assert(neo_rx_encode(&v.rx,b)==10);
    assert(b[8]==0x30 && b[9]==0xfc);
    assert(fragment(&v,7,3,0,50,24)); assert(v.rx.ack==8);
    assert(fragment(&v,8,3,0,50,24)); assert(v.rx.ack==8);
    assert(fragment(&v,7,3,1,50,24)); assert(v.rx.ack==32);
    assert(fragment(&v,8,3,1,52,24)); assert(v.rx.ack==48);
    assert(neo_rx_encode(&v.rx,b)==8 && v.retransmits==4);
    /* Entire message lost: infer its base from adjacent anchored message. */
    memset(&v,0,sizeof v); neo_rx_reset(&v.rx,0);
    assert(fragment(&v,11,2,1,40,40));
    assert(fragment(&v,10,3,0,42,16)); assert(v.rx.ack==8);
    assert(fragment(&v,10,3,2,42,16)); assert(v.rx.ack==8);
    assert(fragment(&v,10,3,1,44,16)); assert(v.rx.ack==24);
    assert(fragment(&v,11,2,0,42,16)); assert(v.rx.ack==40);
    /* No anchor: request announced range, but never invent receipt. */
    memset(&v,0,sizeof v); neo_rx_reset(&v.rx,0);
    assert(fragment(&v,40,3,1,26,0));
    assert(v.rx.ack==0 && v.rx.high==24 && v.unanchored==1);
    assert(!fragment(&v,40,3,3,26,0)); /* index outside count */
    assert(!fragment(&v,40,3,0,25,0)); /* unsupported flag */
    assert(!fragment(&v,40,3,0,8,16)); /* normal prefix mismatch */
    assert(!fragment(&v,41,3,0,8192+8,8192+8));
    assert(v.rx.ack==0);
}
static void many_messages(void) {
    neo_video_rx_t v={0}; neo_rx_reset(&v.rx,0xffe0);
    uint16_t base=v.rx.ack;
    /* Exercises message ID wrap, sequence wrap, cached normal anchors and
     * retransmission flags 2/4 with deliberately differing body endpoints. */
    for(unsigned m=0;m<2000;++m) {
        uint16_t high=(uint16_t)(base+8*8);
        assert(fragment(&v,(uint8_t)m,8,7,high,high));
        for(unsigned j=0;j<7;++j) {
            unsigned index=(j*3)%7;
            assert(fragment(&v,(uint8_t)m,8,index,(uint16_t)(high+(j%2 ? 4 : 2)),base));
        }
        assert(v.rx.ack==high); base=high;
    }
    assert(!v.invalid && !v.outside_window && !v.conflicts && !v.unanchored);
}
int main(void) { codec(); retransmission(); many_messages(); return 0; }
