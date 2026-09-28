/*
 * test_di.c - Data identifier catalogue tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_di.h"
#include "test_util.h"

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

    TEST_REPORT("test_di");
}
