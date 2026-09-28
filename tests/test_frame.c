/*
 * test_frame.c - Codec and frame layer unit tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_codec.h"
#include "dlt645_frame.h"
#include "test_util.h"

static void test_bcd(void)
{
    CHECK_EQ_INT(dlt645_bcd_to_bin(0x00), 0);
    CHECK_EQ_INT(dlt645_bcd_to_bin(0x99), 99);
    CHECK_EQ_INT(dlt645_bcd_to_bin(0x9A), 0xFF);
    CHECK_EQ_INT(dlt645_bin_to_bcd(0), 0x00);
    CHECK_EQ_INT(dlt645_bin_to_bcd(99), 0x99);
    CHECK_EQ_INT(dlt645_bin_to_bcd(100), 0xFF);
}

static void test_addr(void)
{
    dlt645_addr_t a;
    char text[16];
    const uint8_t expect[6] = {0x12, 0x90, 0x78, 0x56, 0x34, 0x12};

    CHECK_EQ_INT(dlt645_addr_parse("123456789012", &a), DLT645_OK);
    /* low byte first: "12" is A0 */
    CHECK_EQ_MEM(a.b, expect, 6);

    dlt645_addr_format(&a, text);
    CHECK(strcmp(text, "123456789012") == 0);

    /* shorthand pads high bytes with 0 */
    CHECK_EQ_INT(dlt645_addr_parse("12", &a), DLT645_OK);
    CHECK_EQ_INT(a.b[0], 0x12);
    CHECK_EQ_INT(a.b[5], 0x00);

    /* wildcard high bytes */
    CHECK_EQ_INT(dlt645_addr_parse("AAAAAA123456", &a), DLT645_OK);
    CHECK_EQ_INT(a.b[0], 0x56);
    CHECK_EQ_INT(a.b[2], 0x12);
    CHECK_EQ_INT(a.b[5], 0xAA);

    {
        dlt645_addr_t local;
        (void)dlt645_addr_parse("123456789012", &local);
        /* low six digits of the local address are 789012 */
        CHECK_EQ_INT(dlt645_addr_parse("AAAAAA789012", &a), DLT645_OK);
        CHECK(dlt645_addr_match(&a, &local) == 1);
        (void)dlt645_addr_parse("AAAAAA999999", &a);
        CHECK(dlt645_addr_match(&a, &local) == 0);
    }

    {
        dlt645_addr_t bc;
        dlt645_addr_broadcast(&bc);
        CHECK(dlt645_addr_is_broadcast(&bc) == 1);
    }
}

static void test_di(void)
{
    uint32_t di = dlt645_di_make(0x00, 0x01, 0x00, 0x00);
    uint8_t wire[4];
    CHECK_EQ_INT(di, 0x00010000u);
    dlt645_di_write(di, wire);
    CHECK_EQ_INT(wire[0], 0x00); /* DI0 first */
    CHECK_EQ_INT(wire[1], 0x00);
    CHECK_EQ_INT(wire[2], 0x01);
    CHECK_EQ_INT(wire[3], 0x00);
    CHECK_EQ_INT(dlt645_di_read(wire), di);
}

static void test_bcd_item(void)
{
    /* 123456.78 kWh -> BCD 78 56 34 12 (low byte first) */
    uint8_t data[4] = {0x78, 0x56, 0x34, 0x12};
    int64_t value = 0;
    int neg = 0;
    char text[32];

    CHECK_EQ_INT(dlt645_bcd_decode(data, 4, 2, &value, &neg), DLT645_OK);
    CHECK_EQ_INT(value, 12345678);
    CHECK_EQ_INT(neg, 0);
    CHECK_EQ_INT(dlt645_bcd_format(data, 4, 2, text, sizeof(text)), DLT645_OK);
    CHECK(strcmp(text, "123456.78") == 0);

    {
        uint8_t enc[4];
        CHECK_EQ_INT(dlt645_bcd_encode(12345678, 2, enc, 4), DLT645_OK);
        CHECK_EQ_MEM(enc, data, 4);
    }

    /* negative: 0012.5 -> digits 000125, sign on MSB */
    {
        uint8_t enc[3];
        int64_t v;
        CHECK_EQ_INT(dlt645_bcd_encode(-125, 1, enc, 3), DLT645_OK);
        CHECK_EQ_INT(enc[0], 0x25);
        CHECK_EQ_INT(enc[1], 0x01);
        CHECK_EQ_INT(enc[2], 0x80);
        CHECK_EQ_INT(dlt645_bcd_decode(enc, 3, 1, &v, &neg), DLT645_OK);
        CHECK_EQ_INT(v, 125);
        CHECK_EQ_INT(neg, 1);
        CHECK_EQ_INT(dlt645_bcd_format(enc, 3, 1, text, sizeof(text)), DLT645_OK);
        CHECK(strcmp(text, "-12.5") == 0);
    }

    /* unsorted/invalid nibble */
    {
        uint8_t bad[1] = {0x1F};
        CHECK_EQ_INT(dlt645_bcd_decode(bad, 1, 0, &value, &neg), DLT645_ERR_FORMAT);
    }
}

static void test_time(void)
{
    dlt645_time_t t = {24, 3, 15, 13, 45, 30};
    uint8_t out[6];
    dlt645_time_t back;

    CHECK_EQ_INT(dlt645_time_to_broadcast(&t, out), DLT645_OK);
    /* ss mm hh DD MM YY */
    CHECK_EQ_INT(out[0], 0x30);
    CHECK_EQ_INT(out[1], 0x45);
    CHECK_EQ_INT(out[2], 0x13);
    CHECK_EQ_INT(out[3], 0x15);
    CHECK_EQ_INT(out[4], 0x03);
    CHECK_EQ_INT(out[5], 0x24);

    CHECK_EQ_INT(dlt645_time_from_broadcast(out, &back), DLT645_OK);
    CHECK_EQ_INT(back.year, 24);
    CHECK_EQ_INT(back.month, 3);
    CHECK_EQ_INT(back.day, 15);
    CHECK_EQ_INT(back.hour, 13);
    CHECK_EQ_INT(back.minute, 45);
    CHECK_EQ_INT(back.second, 30);
}

/* Read request for 正向有功总电能 from meter 123456789012. */
static const uint8_t k_read_req[] = {
    0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68,
    0x11, 0x04, 0x33, 0x33, 0x34, 0x33,
    0x68, 0x16
};

static void test_encode_read(void)
{
    dlt645_addr_t addr;
    uint32_t di = dlt645_di_make(0x00, 0x01, 0x00, 0x00);
    uint8_t data[4];
    uint8_t out[32];
    size_t out_len = 0;

    (void)dlt645_addr_parse("123456789012", &addr);
    dlt645_di_write(di, data);

    CHECK_EQ_INT(dlt645_frame_encode_simple(&addr, DLT645_FUNC_READ_DATA,
                                            data, 4, out, sizeof(out),
                                            &out_len),
                 DLT645_OK);
    CHECK_EQ_INT(out_len, sizeof(k_read_req));
    CHECK_EQ_MEM(out, k_read_req, sizeof(k_read_req));
}

static void test_decode_read_response(void)
{
    /* 123456.78 kWh response for 正向有功总电能 */
    static const uint8_t resp[] = {
        0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68,
        0x91, 0x08, 0x33, 0x33, 0x34, 0x33, 0xAB, 0x89, 0x67, 0x45,
        0xCC, 0x16
    };
    dlt645_frame_t f;
    uint8_t plain[16];
    size_t plain_len = 0;
    char text[32];

    CHECK_EQ_INT(dlt645_frame_decode(resp, sizeof(resp), &f), DLT645_OK);
    CHECK_EQ_INT(f.ctrl, 0x91);
    CHECK_EQ_INT(f.len, 8);

    CHECK_EQ_INT(dlt645_frame_data(&f, plain, sizeof(plain), &plain_len),
                 DLT645_OK);
    CHECK_EQ_INT(plain_len, 8);
    CHECK_EQ_INT(dlt645_di_read(plain), 0x00010000u);
    CHECK_EQ_INT(dlt645_bcd_format(&plain[4], 4, 2, text, sizeof(text)),
                 DLT645_OK);
    CHECK(strcmp(text, "123456.78") == 0);

    /* control code helpers */
    CHECK_EQ_INT(dlt645_ctrl_func(f.ctrl), DLT645_FUNC_READ_DATA);
    CHECK(dlt645_ctrl_is_response(f.ctrl) == 1);
    CHECK(dlt645_ctrl_is_error(f.ctrl) == 0);
    CHECK(dlt645_ctrl_has_followup(0xB1) == 1);
}

static void test_bad_checksum(void)
{
    uint8_t bad[sizeof(k_read_req)];
    dlt645_frame_t f;
    memcpy(bad, k_read_req, sizeof(bad));
    bad[13] ^= 0xFF;
    CHECK_EQ_INT(dlt645_frame_decode(bad, sizeof(bad), &f), DLT645_ERR_CHECKSUM);
}

static void test_rx_parser(void)
{
    dlt645_rx_parser_t p;
    size_t i;
    /* Include a 0xFE preamble and random noise before the frame. */
    const uint8_t stream[] = {0xFE, 0xFE, 0xFE, 0xFE, 0x00};

    dlt645_rx_init(&p);
    for (i = 0; i < sizeof(stream); i++) {
        CHECK_EQ_INT(dlt645_rx_feed(&p, stream[i]), DLT645_OK);
    }
    for (i = 0; i < sizeof(k_read_req); i++) {
        CHECK_EQ_INT(dlt645_rx_feed(&p, k_read_req[i]), DLT645_OK);
    }
    CHECK(dlt645_rx_frame_ready(&p) == 1);
    CHECK_EQ_INT(dlt645_rx_frame(&p)->ctrl, 0x11);
    CHECK_EQ_INT(dlt645_rx_frame(&p)->len, 4);

    /* Corrupt a byte: parser must report and recover. */
    {
        uint8_t corrupt[sizeof(k_read_req)];
        memcpy(corrupt, k_read_req, sizeof(corrupt));
        corrupt[10] ^= 0xFF;
        dlt645_rx_reset(&p);
        for (i = 0; i < sizeof(corrupt); i++) {
            (void)dlt645_rx_feed(&p, corrupt[i]);
        }
        /* The last byte 0x16 was fed while in CS state, resync to idle. */
        CHECK(dlt645_rx_frame_ready(&p) == 0);
    }
}

int main(void)
{
    test_bcd();
    test_addr();
    test_di();
    test_bcd_item();
    test_time();
    test_encode_read();
    test_decode_read_response();
    test_bad_checksum();
    test_rx_parser();
    TEST_REPORT("test_frame");
}
