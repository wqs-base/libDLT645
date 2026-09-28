/*
 * test_di.c - Data identifier catalogue tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_di.h"
#include "test_util.h"

/* Application supplied catalogue extension. */
static const dlt645_di_info_t g_user_di[] = {
    /* new vendor specific identifier */
    DLT645_DI_DEFINE(0x0A000001u, "厂商自定义电能", "kWh", "XXXXXX.XX",
                     4, 2, DLT645_DI_READ, DLT645_DI_CAT_OTHER),
    /* override a built-in identifier */
    DLT645_DI_DEFINE(0x00010000u, "正向有功总电能(厂家重定义)", "kWh",
                     "XXXXXX.XX", 4, 2, DLT645_DI_READ | DLT645_DI_WRITE,
                     DLT645_DI_CAT_ENERGY),
    /* wildcard entry: DI1 (rate) variable */
    DLT645_DI_ENTRY(0x0B000000u, 0xFFFF00FFu, "厂商自定义总电能", "kWh",
                    "XXXXXX.XX", 4, 2,
                    DLT645_DI_READ | DLT645_DI_WILD_RATE,
                    DLT645_DI_CAT_ENERGY)
};

static void test_user_table(void)
{
    const dlt645_di_info_t *e;
    char name[128];

    /* not visible before registration */
    CHECK(dlt645_di_lookup(0x0A000001u) == NULL);
    CHECK_EQ_INT(dlt645_di_user_count(), 0u);

    dlt645_di_set_user_table(g_user_di, 3);

    CHECK_EQ_INT(dlt645_di_user_count(), 3u);
    CHECK(dlt645_di_user_at(0) != NULL);
    CHECK(dlt645_di_user_at(3) == NULL);
    /* built-in catalogue is unchanged */
    CHECK(dlt645_di_count() > 100u);

    /* new identifier now resolves */
    e = dlt645_di_lookup(0x0A000001u);
    CHECK(e != NULL);
    if (e != NULL) {
        CHECK(strcmp(e->name, "厂商自定义电能") == 0);
        CHECK_EQ_INT(e->category, DLT645_DI_CAT_OTHER);
    }
    dlt645_di_format_name(0x0A000001u, name, sizeof(name));
    CHECK(strcmp(name, "厂商自定义电能") == 0);

    /* built-in identifier overridden by the user entry */
    dlt645_di_format_name(0x00010000u, name, sizeof(name));
    CHECK(strstr(name, "厂家重定义") != NULL);

    /* wildcard user entry + rate suffix */
    e = dlt645_di_lookup(0x0B000500u);
    CHECK(e != NULL);
    if (e != NULL) {
        CHECK((e->flags & DLT645_DI_WILD_RATE) != 0);
    }
    dlt645_di_format_name(0x0B000500u, name, sizeof(name));
    CHECK(strstr(name, "厂商自定义总电能") != NULL);
    CHECK(strstr(name, "费率5") != NULL);

    /* clearing restores the built-in behaviour */
    dlt645_di_clear_user_table();
    CHECK_EQ_INT(dlt645_di_user_count(), 0u);
    CHECK(dlt645_di_lookup(0x0A000001u) == NULL);
    dlt645_di_format_name(0x00010000u, name, sizeof(name));
    CHECK(strstr(name, "厂家重定义") == NULL);
    CHECK(strstr(name, "正向有功") != NULL);
}

int main(void)
{
    const dlt645_di_info_t *e;
    char name[128];

    e = dlt645_di_lookup(0x00010000u); /* (当前)正向有功总电能 */
    CHECK(e != NULL);
    if (e != NULL) {
        CHECK(e->len == 4);
        CHECK_EQ_INT(e->decimals, 2);
        CHECK(strcmp(e->unit, "kWh") == 0);
        CHECK_EQ_INT(e->category, DLT645_DI_CAT_ENERGY);
    }

    /* rate variants resolve through the wildcard entry */
    e = dlt645_di_lookup(0x00010500u); /* 正向有功 费率5 */
    CHECK(e != NULL);
    if (e != NULL) {
        CHECK((e->flags & DLT645_DI_WILD_RATE) != 0);
    }
    dlt645_di_format_name(0x00010500u, name, sizeof(name));
    CHECK(strstr(name, "正向有功") != NULL);
    CHECK(strstr(name, "费率5") != NULL);

    /* settlement day 12 */
    e = dlt645_di_lookup(0x0001000Cu);
    CHECK(e != NULL);
    dlt645_di_format_name(0x0001000Cu, name, sizeof(name));
    CHECK(strstr(name, "结算日") != NULL);

    /* settlement day 5 is not listed verbatim in Appendix A but must
     * still resolve through the graceful fallback */
    dlt645_di_format_name(0x00010005u, name, sizeof(name));
    CHECK(strstr(name, "正向有功") != NULL);
    CHECK(strstr(name, "上5结算日") != NULL);

    /* catalogue is non-trivial */
    CHECK(dlt645_di_count() > 100u);
    CHECK(dlt645_di_at(0) != NULL);
    CHECK(dlt645_di_at(dlt645_di_count()) == NULL);

    /* unknown identifier */
    dlt645_di_format_name(0xDEADBEEFu, name, sizeof(name));
    CHECK(strstr(name, "0xDEADBEEF") != NULL);

    test_user_table();

    TEST_REPORT("test_di");
}
