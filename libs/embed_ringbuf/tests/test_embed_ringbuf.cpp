#include "embed_ringbuf/embed_ringbuf.h"
#include "embed_test/embed_test.hpp"
#include <initializer_list>
TEST_CASE(wrap_full_and_invalid) {
    uint8_t storage[8], out=0; embed_ringbuf_t rb{};
    for (size_t capacity: {size_t(8),size_t(7)}) {
        embed_ringbuf_init(&rb,storage,capacity);
        for(unsigned round=0;round<3;++round) {
            for(size_t i=0;i<capacity-1;++i) CHECK(embed_ringbuf_put(&rb,(uint8_t)i));
            CHECK(!embed_ringbuf_put(&rb,42)); CHECK_EQ(embed_ringbuf_free_space(&rb),0u);
            for(size_t i=0;i<capacity-1;++i) { CHECK(embed_ringbuf_get(&rb,&out)); CHECK_EQ(out,i); }
            CHECK(!embed_ringbuf_get(&rb,&out));
        }
    }
    embed_ringbuf_init(&rb,storage,1); CHECK(!embed_ringbuf_put(&rb,1));
}

EMBED_TEST_MAIN()
