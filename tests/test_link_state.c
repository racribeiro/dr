#include "support.h"
#include "link_golden.h"

/* Protocol prefixes only. No captured GPS, credentials or device identities.
 * New Fly capture 113325: third telemetry word-pair advances the uplink ACK;
 * video wrapper sequence (low flag bits removed) advances the first KA pair.
 * Gaps/reordering below are synthetic adversarial cases, NOT capture claims. */
static void put(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static dji_neo_result_t incoming(dji_neo_t *n, uint8_t type, uint16_t sequence,
                                 uint16_t peer_ack, uint64_t ms) {
    uint8_t p[34] = {34,0x80,0xb0,0x4f,0,0,0,0};
    p[6]=type; put(p+4, type==1 ? 0 : sequence);
    put(p+8, TEST_SEED); put(p+10, (uint16_t)(sequence & 0xfff8));
    put(p+16, TEST_SEED); put(p+18, TEST_SEED);
    put(p+24, peer_ack); put(p+26, peer_ack);
    if (type==2) {
        p[16]=(uint8_t)(((uint16_t)(sequence&0xfff8)-TEST_SEED)/8);
        p[17]=1; p[18]=0x60; p[19]=0xa5; /* one fragment, upper metadata bits are opaque */
    }
    p[32]=0x55; p[33]=0xff; /* Must NEVER be copied into keepalive bytes 24..25. */
    test_checksum(p);
    return dji_neo_on_datagram(n,p,sizeof p,ms);
}
static void send_query(dji_neo_t *n) {
    uint8_t f[13]; assert(dji_neo_build_heartbeat(f,sizeof f,0)==13);
    assert(dji_neo_send_query(n,f,sizeof f)==DJI_NEO_OK);
}
static void expect_ka(dji_neo_t *n, test_io_t *io, uint64_t ms,
                       uint16_t video, uint16_t sideband, uint16_t peer, uint16_t sent) {
    assert(dji_neo_poll(n,ms)==DJI_NEO_OK);
    test_wire(io->last,io->size); assert(io->last[6]==4 && io->size>=34);
    const uint8_t *b=io->last+8;
    unsigned extra=test_le16(b+6)==1 ? (test_le16(b+4)+3u)/4u : 0;
    assert(test_le16(b)==video);
    b+=extra;
    /* channel 0's extension shifts the two later channel blocks */
    assert(test_le16(b+8)==sideband && test_le16(b+10)==sideband);
    assert(test_le16(b+16)==peer && test_le16(b+18)==sent);
    assert(!test_le16(b+24));
}
static void captured_keepalive(void) {
    test_io_t io={0}; dji_neo_t *n=test_client(&io,1); test_connect(n);
    for(unsigned i=1;i<=29;++i) send_query(n);
    assert(incoming(n,1,0,TEST_SEED+72,2)==DJI_NEO_OK);
    for(unsigned i=1;i<=53;++i)
        assert(incoming(n,2,(uint16_t)(TEST_SEED+8*i),0,2)==DJI_NEO_OK);
    assert(dji_neo_poll(n,21)==DJI_NEO_OK);
    assert(io.size==sizeof golden_link_keepalive);
    assert(!memcmp(io.last,golden_link_keepalive,sizeof golden_link_keepalive));
    dji_neo_destroy(n);
}
static void link_state(void) {
    dji_neo_config_t bad={.udp_send=test_send,.body_id=0x717d};
    assert(!dji_neo_create(&bad));
    test_io_t io={0}; dji_neo_t *n=test_client(&io,1); test_connect(n);
    send_query(n); assert(test_le16(io.last+4)==TEST_FIRST_F45 && io.last[16]==1);
    send_query(n); send_query(n);
    uint16_t sent=TEST_SEED+24;
    assert(incoming(n,1,0,sent,2)==DJI_NEO_OK);
    send_query(n); assert(test_le16(io.last+8)==sent);
    sent+=8;
    assert(incoming(n,1,0,TEST_FIRST_F45,3)==DJI_NEO_OK); /* stale */
    assert(incoming(n,1,0,sent+8,4)==DJI_NEO_OK); /* future */
    assert(incoming(n,1,0,sent+1,5)==DJI_NEO_OK); /* unaligned */
    uint8_t bare[34]={34,0x80,0xb0,0x4f,0,0,1,0,0x55};
    put(bare+24,sent); test_checksum(bare);
    assert(dji_neo_on_datagram(n,bare,sizeof bare,5)==DJI_NEO_OK); /* DUML-ish body isn't an ACK prefix */
    assert(incoming(n,2,TEST_SEED+8,0,6)==DJI_NEO_OK);
    assert(incoming(n,2,TEST_SEED+24,0,7)==DJI_NEO_OK); /* hole */
    assert(incoming(n,3,TEST_SEED+8,0,8)==DJI_NEO_OK);
    expect_ka(n,&io,21,TEST_SEED+8,TEST_SEED+8,TEST_SEED+24,sent);
    assert(io.size==sizeof golden_gap_keepalive && !memcmp(io.last,golden_gap_keepalive,io.size));
    assert(incoming(n,2,TEST_SEED+16+2,0,22)==DJI_NEO_OK); /* retransmit flag */
    assert(incoming(n,2,TEST_SEED+16,0,23)==DJI_NEO_OK); /* duplicate */
    assert(incoming(n,1,0,sent,24)==DJI_NEO_OK);
    expect_ka(n,&io,41,TEST_SEED+24,TEST_SEED+8,sent,sent);
    /* Wrong session, bad XOR and backwards time must not advance state. */
    uint8_t wrong[]={8,0x80,0xb1,0x4f,0,0,1,0}; test_checksum(wrong);
    assert(dji_neo_on_datagram(n,wrong,sizeof wrong,42)==DJI_NEO_EINVAL);
    wrong[2]=0xb0; wrong[7]^=2;
    assert(dji_neo_on_datagram(n,wrong,sizeof wrong,42)==DJI_NEO_EINVAL);
    assert(incoming(n,2,TEST_SEED+32,0,40)==DJI_NEO_EINVAL);
    expect_ka(n,&io,61,TEST_SEED+24,TEST_SEED+8,sent,sent);
    assert(incoming(n,2,(uint16_t)(TEST_SEED+24+1025*8),0,62)==DJI_NEO_OK);
    expect_ka(n,&io,81,TEST_SEED+24,TEST_SEED+8,sent,sent); /* beyond reorder window */
    /* Receive sequence/counter wraps, including the bitmap ring. */
    for (unsigned i=4;i<=8195;++i)
        assert(incoming(n,2,(uint16_t)(TEST_SEED+8*i),0,82)==DJI_NEO_OK);
    expect_ka(n,&io,101,(uint16_t)(TEST_SEED+8*8195),TEST_SEED+8,sent,sent);
    assert(dji_neo_set_session_armed(n,0)==DJI_NEO_OK);
    assert(dji_neo_reset_session(n,0x4fb0,0x717d)==DJI_NEO_EINVAL);
    assert(dji_neo_reset_session(n,0x4fb0,0x787d)==DJI_NEO_OK);
    assert(incoming(n,2,TEST_SEED+64,0,102)==DJI_NEO_OK); /* disarmed receive can't seed link state */
    assert(dji_neo_set_session_armed(n,1)==DJI_NEO_OK);
    assert(dji_neo_poll(n,103)==DJI_NEO_OK); test_accept(n,104);
    expect_ka(n,&io,124,0x7d78,0x7d78,0x7d78,0x7d78);
    send_query(n); assert(test_le16(io.last+4)==0x7d80 && io.last[16]==1);
    dji_neo_destroy(n);
}

typedef struct { test_io_t io; unsigned views; uint8_t token; uint64_t now,last; } view_io_t;
static int view_send(void *user,const uint8_t *p,size_t size) {
    view_io_t *v=user;
    if (p[6]==5 && p[29]==0x18 && p[30]==0x47) {
        assert(p[35]==v->token && p[37]==(v->views<2));
        if (v->views) assert(v->now-v->last>=50);
        v->last=v->now; ++v->views;
    }
    return test_send(&v->io,p,size);
}
static void liveview(void) {
    view_io_t v={.token=0x2f};
    dji_neo_config_t cfg={.udp_send=view_send,.udp_user=&v,.session_id=0x4fb0,.body_id=0x707d};
    dji_neo_t *n=dji_neo_create(&cfg); assert(n);
    assert(dji_neo_set_liveview_profile(NULL,0,50)==DJI_NEO_EINVAL);
    assert(dji_neo_set_liveview_profile(n,0x2f,19)==DJI_NEO_EINVAL);
    assert(dji_neo_set_liveview_profile(n,0x2f,1001)==DJI_NEO_EINVAL);
    assert(dji_neo_set_liveview_profile(n,v.token,50)==DJI_NEO_OK);
    assert(dji_neo_set_session_armed(n,1)==DJI_NEO_OK);
    assert(dji_neo_poll(n,0)==DJI_NEO_OK);
    assert(dji_neo_set_liveview_profile(n,0x30,50)==DJI_NEO_ESTATE);
    test_accept(n,1);
    for(v.now=5;v.now<=2000;v.now+=5) {
        if(v.now%100==0) test_idle_telemetry(n,v.now);
        assert(dji_neo_poll(n,v.now)==DJI_NEO_OK);
    }
    assert(v.views==30 && !v.io.signs);
    unsigned sends=v.io.sends;
    v.token=0x30; assert(dji_neo_set_liveview_profile(n,v.token,50)==DJI_NEO_OK);
    assert(v.io.sends==sends); /* changing profile never sends implicitly */
    assert(dji_neo_restart_liveview(n)==DJI_NEO_OK); v.views=0;
    for(v.now=2005;v.now<=3600;v.now+=5) {
        if(v.now%100==0) test_idle_telemetry(n,v.now);
        assert(dji_neo_poll(n,v.now)==DJI_NEO_OK);
    }
    assert(v.views==30 && !v.io.signs);
    assert(dji_neo_set_command_armed(n,1)==DJI_NEO_EAUTH);
    dji_neo_destroy(n);
}
static void variable_peer_ack(void) {
    test_io_t io={0}; dji_neo_t *n=test_client(&io,1); test_connect(n);
    send_query(n); send_query(n);
    /* First channel has one extension byte, shifting the third ACK block. */
    uint8_t p[33]={33,0x80,0xb0,0x4f,0,0,1,0};
    put(p+8,TEST_SEED); put(p+10,TEST_SEED+32); put(p+12,4); put(p+14,1);
    put(p+17,TEST_SEED); put(p+19,TEST_SEED);
    put(p+25,TEST_SEED+16); put(p+27,TEST_SEED+16); test_checksum(p);
    assert(dji_neo_on_datagram(n,p,sizeof p,2)==DJI_NEO_OK);
    send_query(n); assert(test_le16(io.last+8)==TEST_SEED+16);
    dji_neo_destroy(n);
}
static void video_liveness(void) {
    test_io_t io={0}; dji_neo_t *n=test_client(&io,1); test_connect(n);
    test_arm_all(n,&io);
    assert(incoming(n,2,TEST_SEED+8,0,1000)==DJI_NEO_OK);
    assert(dji_neo_poll(n,1000)==DJI_NEO_OK);
    assert(incoming(n,2,TEST_SEED+16,0,2001)==DJI_NEO_OK);
    assert(dji_neo_poll(n,2001)==DJI_NEO_OK);
    assert(dji_neo_link_state(n)==DJI_NEO_LINK_CONNECTED);
    uint8_t f[21]; assert(dji_neo_build_gimbal_rate(f,sizeof f,0,-36)==21);
    assert(dji_neo_send_actuation(n,f,sizeof f)==DJI_NEO_ESTATE); /* stale OSD revokes gates */
    assert(io.signs==0); /* video + wire ACK never call signer */
    uint8_t malformed[9]={9,0x80,0xb0,0x4f,0,0,2,0}; test_checksum(malformed);
    assert(dji_neo_on_datagram(n,malformed,sizeof malformed,3900)==DJI_NEO_OK);
    assert(dji_neo_poll(n,4001)==DJI_NEO_OK);
    assert(dji_neo_link_state(n)==DJI_NEO_LINK_LOST); /* malformed video cannot keep link alive */
    dji_neo_destroy(n);
}
static void public_retransmissions(void) {
    test_io_t io={0}; dji_neo_t *n=test_client(&io,1); test_connect(n);
    dji_neo_signer_t noop={.sign=test_sign,.user=&io}; dji_neo_set_signer(n,&noop);
    uint8_t p[20]={20,0x80,0xb0,0x4f,0,0,2,0};
    put(p+8,TEST_SEED); p[17]=0x82; p[18]=0xe0; p[19]=0xa5;
    for(unsigned id=1;id<=2;++id) {
        p[16]=(uint8_t)id; put(p+4,(uint16_t)(TEST_SEED+id*16)); put(p+10,test_le16(p+4));
        test_checksum(p); assert(dji_neo_on_datagram(n,p,sizeof p,2)==DJI_NEO_OK);
    }
    assert(dji_neo_poll(n,21)==DJI_NEO_OK); test_wire(io.last,io.size);
    assert(io.size==35 && test_le16(io.last+8)==TEST_SEED && io.last[16]==0xcc);
    /* Distinct original sequences, shared flagged wrapper endpoint, differing
     * body endpoint. Only metadata/lineage identifies the original packet. */
    p[16]=1; p[17]=2; put(p+4,TEST_SEED+32+2); put(p+10,TEST_SEED+16);
    test_checksum(p); assert(dji_neo_on_datagram(n,p,sizeof p,22)==DJI_NEO_OK);
    io.fail_send=1; assert(dji_neo_poll(n,41)==DJI_NEO_EIO);
    assert(dji_neo_poll(n,41)==DJI_NEO_OK); test_wire(io.last,io.size);
    assert(io.size==35 && test_le16(io.last+8)==TEST_SEED+16 && io.last[16]==0xfc);
    p[16]=2; put(p+4,TEST_SEED+32+4); put(p+10,TEST_SEED);
    test_checksum(p); assert(dji_neo_on_datagram(n,p,sizeof p,42)==DJI_NEO_OK);
    assert(dji_neo_poll(n,61)==DJI_NEO_OK); test_wire(io.last,io.size);
    assert(io.size==34 && test_le16(io.last+8)==TEST_SEED+32 && test_le16(io.last+10)==TEST_SEED+32);
    assert(!io.signs); dji_neo_destroy(n);
}
int main(void) {
    captured_keepalive(); link_state(); liveview(); variable_peer_ack(); video_liveness(); public_retransmissions(); return 0;
}
