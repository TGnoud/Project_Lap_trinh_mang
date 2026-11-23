#include "protocol/frame.h"

#undef NDEBUG
#include <assert.h>
#include <string.h>

static void test_splits(void) {
    const char body[] = "{\"x\":\"café\"}";
    unsigned char wire[FRAME_HEADER_SIZE + sizeof(body) - 1];
    assert(frame_encode_header(sizeof(body) - 1, wire));
    memcpy(wire + FRAME_HEADER_SIZE, body, sizeof(body) - 1);

    // Mọi điểm chia, kể cả trong header và giữa hai byte của é.
    for (size_t split = 1; split < sizeof(wire); ++split) {
        FrameReader reader = {0};
        size_t used;
        assert(frame_at_boundary(&reader));
        assert(frame_feed(&reader, NULL, 0, &used) == FRAME_MORE && used == 0);
        assert(frame_feed(&reader, wire, split, &used) == FRAME_MORE && used == split);
        assert(!frame_at_boundary(&reader));
        assert(frame_feed(&reader, wire + split, sizeof(wire) - split, &used) == FRAME_READY);
        assert(used == sizeof(wire) - split);
        assert(reader.body_size == sizeof(body) - 1 && strcmp(reader.body, body) == 0);
        frame_reset(&reader);
        assert(frame_at_boundary(&reader));
    }

    FrameReader reader = {0};
    for (size_t i = 0; i < sizeof(wire); ++i) {
        size_t used;
        const FrameStatus status = frame_feed(&reader, wire + i, 1, &used);
        assert(used == 1 && status == (i + 1 == sizeof(wire) ? FRAME_READY : FRAME_MORE));
    }
    frame_reset(&reader);
}

static void test_joined_frames(void) {
    const unsigned char wire[] = "\0\0\0\7{\"x\":1}\0\0\0\7{\"x\":2}";
    FrameReader reader = {0};
    size_t used;
    assert(frame_feed(&reader, wire, sizeof(wire) - 1, &used) == FRAME_READY);
    assert(used == 11 && strcmp(reader.body, "{\"x\":1}") == 0);
    frame_reset(&reader);
    assert(frame_feed(&reader, wire + 11, 11, &used) == FRAME_READY);
    assert(used == 11 && strcmp(reader.body, "{\"x\":2}") == 0);
    frame_reset(&reader);
}

static void test_lengths(void) {
    const unsigned char zero[] = {0, 0, 0, 0};
    const unsigned char oversized[] = {0, 1, 0, 1};
    static unsigned char wire[FRAME_HEADER_SIZE + FRAME_MAX_SIZE];
    assert(!frame_encode_header(0, wire));
    assert(!frame_encode_header(FRAME_MAX_SIZE + 1, wire));
    assert(frame_encode_header(FRAME_MAX_SIZE, wire));
    assert(memcmp(wire, "\0\1\0\0", FRAME_HEADER_SIZE) == 0);
    memset(wire + FRAME_HEADER_SIZE, ' ', FRAME_MAX_SIZE);
    memcpy(wire + FRAME_HEADER_SIZE, "{}", 2);

    FrameReader reader = {0};
    size_t used;
    assert(frame_feed(&reader, wire, sizeof(wire), &used) == FRAME_READY);
    assert(reader.body_size == FRAME_MAX_SIZE && used == sizeof(wire));
    frame_reset(&reader);
    assert(frame_feed(&reader, zero, sizeof(zero), &used) == FRAME_ERROR && reader.body == NULL);
    frame_reset(&reader);
    assert(frame_feed(&reader, oversized, sizeof(oversized), &used) == FRAME_ERROR && reader.body == NULL);
    frame_reset(&reader);
}

static void check_json(const char *body, size_t size, FrameStatus expected) {
    unsigned char wire[128];
    assert(size <= sizeof(wire) - FRAME_HEADER_SIZE);
    assert(frame_encode_header(size, wire));
    memcpy(wire + FRAME_HEADER_SIZE, body, size);

    FrameReader reader = {0};
    size_t used;
    assert(frame_feed(&reader, wire, FRAME_HEADER_SIZE + size, &used) == expected);
    frame_reset(&reader);
}

static void test_json(void) {
    const char valid[] = "{\"tên\":[\"NaN\",\"quote: \\\"\",\"🙂\",\"\xe0\xa0\x80\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\",null,1.5,-0,-20,0.1e2,2E-3,3e+4]}";
    const char embedded_nul[] = "{}\0{}";
    const char escaped_nul[] = "{\"x\":\"\\\0\"}";
    const char *invalid[] = {
        "{\"x\":}", "{}{}", "[]", "0", "null", "{\"x\":NaN}", "{\"x\":-Infinity}",
        "{\"x\":\"\xc3(\"}", "{\"x\":\"\xe2\x82(\"}", "{\"x\":\"\xc0\x81\"}",
        "{\"x\":\"\xed\xa0\x80\"}", "{\"x\":\"\xf4\x90\x80\x80\"}", "{\"x\":\"\xe2",
        "{\"x\":\"\\é\"}", "{\"x\":é}", "{\"x\":\"\n\"}", "{\"x\":1,}",
        "{\"x\":1.}", "{\"x\":-01}", "{\"x\":00.1}", "{\"x\":-}", "{\"x\":1e+}",
    };
    check_json(valid, sizeof(valid) - 1, FRAME_READY);
    check_json(embedded_nul, sizeof(embedded_nul) - 1, FRAME_ERROR);
    check_json(escaped_nul, sizeof(escaped_nul) - 1, FRAME_ERROR);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        check_json(invalid[i], strlen(invalid[i]), FRAME_ERROR);
    }
}

int main(void) {
    test_splits();
    test_joined_frames();
    test_lengths();
    test_json();
    return 0;
}
