/*
 * dlt645_di.h - Data identifier catalogue (Appendix A) and helpers.
 *
 * The catalogue is generated from the standard by
 * tools/gen_di_table.py.  Identifiers that denote a range (rates,
 * settlement days, data blocks) are stored as wildcard entries whose
 * `mask` clears the variable byte.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_DI_H
#define DLT645_DI_H

#include "dlt645_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DLT645_DI_CAT_OTHER = 0,
    DLT645_DI_CAT_ENERGY,   /* 电能量                     */
    DLT645_DI_CAT_DEMAND,   /* 最大需量及发生时间         */
    DLT645_DI_CAT_VARIABLE, /* 变量                       */
    DLT645_DI_CAT_EVENT,    /* 事件记录                   */
    DLT645_DI_CAT_PARAM,    /* 参变量                     */
    DLT645_DI_CAT_FREEZE,   /* 冻结量                     */
    DLT645_DI_CAT_LOAD      /* 负荷记录                   */
} dlt645_di_category_t;

/* flags */
#define DLT645_DI_READ       0x01u /* readable            */
#define DLT645_DI_WRITE      0x02u /* writable            */
#define DLT645_DI_WILD_RATE  0x10u /* DI1 (rate) variable */
#define DLT645_DI_WILD_DAY   0x20u /* DI0 (day) variable  */

typedef struct {
    uint32_t    di;       /* base identifier (DI3<<24|DI2<<16|DI1<<8|DI0) */
    uint32_t    mask;     /* 1 bits must equal `di`                      */
    const char *name;     /* UTF-8 data item name                        */
    const char *unit;     /* UTF-8 unit, empty when none                 */
    const char *format;   /* data format as printed in Appendix A        */
    uint8_t     len;      /* data length in bytes, 0 when variable       */
    uint8_t     decimals; /* implied decimals of a simple numeric format */
    uint8_t     flags;    /* DLT645_DI_* flags                           */
    uint8_t     category; /* dlt645_di_category_t                        */
} dlt645_di_info_t;

/* Look up the best matching catalogue entry (NULL when unknown). */
const dlt645_di_info_t *dlt645_di_lookup(uint32_t di);

/*
 * Like dlt645_di_lookup() but also resolves rate / settlement day
 * variants that the standard only lists by example.  The returned copy
 * has DLT645_DI_WILD_RATE / DLT645_DI_WILD_DAY set when the requested
 * identifier varies that byte.  Returns 1 when an entry was found.
 */
int dlt645_di_lookup_ex(uint32_t di, dlt645_di_info_t *out);

/* The number of entries in the generated catalogue. */
size_t dlt645_di_count(void);

/* Access an entry by index (0..count-1). */
const dlt645_di_info_t *dlt645_di_at(size_t index);

/* ------------------------------------------------------------------ */
/* User supplied catalogue extension                                   */
/* ------------------------------------------------------------------ */

/*
 * Register an application owned array of catalogue entries.  The table
 * is not copied, so it must stay alive (a static array is ideal).  User
 * entries are consulted together with the built-in catalogue and win on
 * equal specificity, which lets them both add vendor specific
 * identifiers and override built-in ones (name / length / unit ...).
 *
 * Passing NULL or count == 0 clears the extension.  Call this during
 * start-up; the library holds no lock and is not thread safe.
 */
void dlt645_di_set_user_table(const dlt645_di_info_t *table, size_t count);
void dlt645_di_clear_user_table(void);
size_t dlt645_di_user_count(void);
const dlt645_di_info_t *dlt645_di_user_at(size_t index);

/*
 * Convenience initialisers for user entries.  DLT645_DI_DEFINE is an
 * exact (fully specified) identifier; DLT645_DI_ENTRY allows a wildcard
 * `mask` (0 bits = "any value" for that byte).
 */
#define DLT645_DI_ENTRY(di_, mask_, name_, unit_, fmt_, len_, dec_, flags_, cat_) \
    { (di_), (mask_), (name_), (unit_), (fmt_), (uint8_t)(len_), \
      (uint8_t)(dec_), (uint8_t)(flags_), (uint8_t)(cat_) }

#define DLT645_DI_DEFINE(di_, name_, unit_, fmt_, len_, dec_, flags_, cat_) \
    DLT645_DI_ENTRY(di_, 0xFFFFFFFFu, name_, unit_, fmt_, len_, dec_, \
                    flags_, cat_)

/* UTF-8 category label. */
const char *dlt645_di_category_name(dlt645_di_category_t category);

/*
 * Format a human readable name for `di` into `out`.  Rate / settlement
 * day variants get a short suffix.  Always NUL terminated when cap > 0.
 */
void dlt645_di_format_name(uint32_t di, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_DI_H */
