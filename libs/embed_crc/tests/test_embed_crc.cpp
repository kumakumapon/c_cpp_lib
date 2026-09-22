#include "embed_crc/embed_crc.h"
#include "embed_test/embed_test.hpp"
TEST_CASE(known_vectors_and_streaming) {
    const char *s="123456789";
    const uint32_t expected[]={0x4b,0x29b1,0x4b37,0x31c3,0xcbf43926,0xe3069283};
    for(int i=0;i<6;++i) {
        auto k=(embed_crc_kind)i; CHECK_EQ(embed_crc(k,s,9),expected[i]);
        auto state=embed_crc_update(k,embed_crc_init(k),s,4);
        CHECK_EQ(embed_crc_finish(k,embed_crc_update(k,state,s+4,5)),expected[i]);
    }
    CHECK_EQ(embed_adler32(1,"Wikipedia",9),0x11e60398u);
    CHECK_EQ(embed_fletcher16(0,"abcde",5),0xc8f0u);
    CHECK_EQ(embed_crc(EMBED_CRC32_IEEE,nullptr,0),0u);
}

EMBED_TEST_MAIN()
