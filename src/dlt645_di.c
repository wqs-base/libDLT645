/*
 * dlt645_di.c - Data identifier catalogue lookup (Appendix A).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_di.h"
#include "dlt645_codec.h"

#include <stdio.h>
#include <string.h>

#include "dlt645_di_table.inc"

/* Number of set bits in a mask (specificity). */
static int mask_bits(uint32_t m)
{
    int n = 0;
    while (m != 0u) {
        n += (int)(m & 1u);
        m >>= 1;
    }
    return n;
}

static const dlt645_di_info_t *best_match(uint32_t di)
{
    const dlt645_di_info_t *best = NULL;
    int best_bits = -1;
    size_t i;

    for (i = 0; i < dlt645_di_table_size; i++) {
        const dlt645_di_info_t *e = &dlt645_di_table[i];
        if ((di & e->mask) != (e->di & e->mask)) {
            continue;
        }
        {
            int bits = mask_bits(e->mask);
            if (bits > best_bits) {
                best_bits = bits;
                best = e;
            }
        }
    }
    return best;
}

/*
 * The standard lists only some rate / settlement day variants.  When an
 * exact lookup fails, progressively ignore the rate (DI1) and settlement
 * day (DI0) bytes so that e.g. "上5结算日" still resolves to its
 * category entry.
 */
static const dlt645_di_info_t *search(uint32_t di)
{
    static const uint32_t clear[4] = {
        0x00000000u, /* as-is      */
        0x000000FFu, /* ignore day */
        0x0000FF00u, /* ignore rate*/
        0x0000FFFFu  /* ignore both*/
    };
    int i;
    for (i = 0; i < 4; i++) {
        const dlt645_di_info_t *e = best_match(di & ~clear[i]);
        if (e != NULL) {
            return e;
        }
    }
    return NULL;
}

const dlt645_di_info_t *dlt645_di_lookup(uint32_t di)
{
    return search(di);
}

int dlt645_di_lookup_ex(uint32_t di, dlt645_di_info_t *out)
{
    const dlt645_di_info_t *e = search(di);
    if (e == NULL) {
        return 0;
    }
    *out = *e;
    if ((e->mask & 0x000000FFu) == 0u || (di & 0xFFu) != (e->di & 0xFFu)) {
        out->flags |= DLT645_DI_WILD_DAY;
    }
    if ((e->mask & 0x0000FF00u) == 0u ||
        ((di >> 8) & 0xFFu) != ((e->di >> 8) & 0xFFu)) {
        out->flags |= DLT645_DI_WILD_RATE;
    }
    return 1;
}

size_t dlt645_di_count(void)
{
    return dlt645_di_table_size;
}

const dlt645_di_info_t *dlt645_di_at(size_t index)
{
    return (index < dlt645_di_table_size) ? &dlt645_di_table[index] : NULL;
}

const char *dlt645_di_category_name(dlt645_di_category_t category)
{
    switch (category) {
    case DLT645_DI_CAT_ENERGY:   return "电能量";
    case DLT645_DI_CAT_DEMAND:   return "最大需量及发生时间";
    case DLT645_DI_CAT_VARIABLE: return "变量";
    case DLT645_DI_CAT_EVENT:    return "事件记录";
    case DLT645_DI_CAT_PARAM:    return "参变量";
    case DLT645_DI_CAT_FREEZE:   return "冻结量";
    case DLT645_DI_CAT_LOAD:     return "负荷记录";
    default:                     return "未知";
    }
}

void dlt645_di_format_name(uint32_t di, char *out, size_t cap)
{
    dlt645_di_info_t info;
    size_t n;
    int rate;
    int day;

    if (out == NULL || cap == 0u) {
        return;
    }
    if (!dlt645_di_lookup_ex(di, &info)) {
        (void)snprintf(out, cap, "未知数据标识 0x%08X", (unsigned)di);
        return;
    }

    n = (size_t)snprintf(out, cap, "%s", info.name);
    if (n >= cap) {
        return;
    }

    rate = (int)((di >> 8) & 0xFFu);
    day = (int)(di & 0xFFu);

    if ((info.flags & DLT645_DI_WILD_RATE) != 0u &&
        rate != (int)((info.di >> 8) & 0xFFu)) {
        n += (size_t)snprintf(out + n, cap - n,
                              (rate == 0xFF) ? "(数据块)" : "(费率%d)", rate);
        if (n >= cap) {
            return;
        }
    }
    if ((info.flags & DLT645_DI_WILD_DAY) != 0u &&
        day != (int)(info.di & 0xFFu)) {
        if (day == 0xFF) {
            (void)snprintf(out + n, cap - n, "(数据块)");
        } else if (day == 0) {
            (void)snprintf(out + n, cap - n, "(当前)");
        } else {
            (void)snprintf(out + n, cap - n, "(上%d结算日)", day);
        }
    }
}
