# -*- coding: utf-8 -*-
"""
gen_di_table.py - Generate the DL/T 645-2007 data identifier catalogue
from the standard PDF (Appendix A).

Outputs
-------
  tools/dlt645_di_catalog.json   full machine readable catalogue
  src/dlt645_di_table.inc        C table + definitions compiled into the lib

The Appendix A tables describe every data item with four identifier
bytes DI3 DI2 DI1 DI0.  Many rows collapse a range of identifiers (e.g.
rate 1..63 or settlement day 1..12) into a single line.  Such rows are
emitted as a wildcard entry: the range byte is cleared in `mask` so the
lookup matches any value, while the base value is kept in `di`.

Usage:
  python tools/gen_di_table.py <pdf> [out_json] [out_inc]
"""
import sys, os, re, io, json

try:
    import pymupdf
except ImportError:  # pragma: no cover
    import fitz as pymupdf


HEX2 = re.compile(r"^[0-9A-Fa-f]{2}$")
TOKEN = re.compile(r"\b([0-9A-Fa-f]{2})\b")

CATEGORY = {
    0x00: ("DLT645_DI_CAT_ENERGY", "电能量"),
    0x01: ("DLT645_DI_CAT_DEMAND", "最大需量及发生时间"),
    0x02: ("DLT645_DI_CAT_VARIABLE", "变量"),
    0x03: ("DLT645_DI_CAT_EVENT", "事件记录"),
    0x04: ("DLT645_DI_CAT_PARAM", "参变量"),
    0x05: ("DLT645_DI_CAT_FREEZE", "冻结量"),
    0x06: ("DLT645_DI_CAT_LOAD", "负荷记录"),
}


def clean(s):
    if s is None:
        return ""
    s = s.replace("\r", " ").replace("\n", " ")
    s = re.sub(r"\s+", " ", s)
    return s.strip()


def parse_cell(cell):
    """Return (values, wildcard) for a DI byte cell."""
    c = clean(cell)
    if not c:
        return [], False
    if HEX2.match(c):
        return [int(c, 16)], False
    tokens = TOKEN.findall(c)
    if not tokens:
        return [], False
    if "…" in c or "..." in c:
        return [int(tokens[0], 16)], True
    return [int(t, 16) for t in tokens], False


def first_name(cell):
    """Best effort extraction of the primary data item name."""
    s = clean(cell)
    if not s:
        return ""
    cut = s.split("…")[0].strip()
    # The first name ends where the second name begins.  Names usually
    # start with '(' or are separated by a space before '('.
    idx = cut.find("(", 1)
    if idx > 0:
        return cut[:idx].strip()
    # fall back: stop at the last "数据块" marker or keep as is
    return cut.strip()


def parse_len(cell):
    m = re.search(r"\d+", clean(cell))
    return int(m.group(0)) if m else 0


def parse_format(cell, length):
    f = clean(cell)
    dec = 0
    m = re.search(r"X+\.(X+)", f)
    if m:
        dec = len(m.group(1))
    return f, dec


def parse_unit(cell):
    u = clean(cell)
    if not u:
        return ""
    # take the leading ASCII unit token(s) before any CJK text
    m = re.match(r"([A-Za-z%°/· ]+?)(?=[\u4e00-\u9fff]|$)", u)
    if m:
        return m.group(1).strip()
    return u.split(" ")[0]


def rw_flags(read_cell, write_cell):
    flags = 0
    if clean(read_cell):
        flags |= 1  # DLT645_DI_READ
    if clean(write_cell):
        flags |= 2  # DLT645_DI_WRITE
    return flags


def extract_rows(pdf):
    doc = pymupdf.open(pdf)
    rows = []
    for pno in range(len(doc)):
        page = doc[pno]
        try:
            text = page.get_text("text")
        except Exception:
            text = ""
        # Appendix A pages are the only ones carrying "表A.n" headers.
        if "表A." not in text:
            continue
        try:
            tabs = page.find_tables()
        except Exception:
            continue
        for t in tabs.tables:
            for r in t.extract():
                if len(r) < 10:
                    continue
                joined = " ".join(clean(x) for x in r)
                if "数据标识" in joined or "DI 3" in joined:
                    continue
                rows.append(r)
    return rows


def expand(rows):
    entries = []
    seen = set()
    for r in rows:
        d3, w3 = parse_cell(r[0])
        d2, w2 = parse_cell(r[1])
        d1, w1 = parse_cell(r[2])
        d0, w0 = parse_cell(r[3])
        if not (d3 and d2 and d1 and d0):
            continue
        fmt, dec = parse_format(r[4], parse_len(r[5]))
        length = parse_len(r[5])
        unit = parse_unit(r[6])
        flags = rw_flags(r[7], r[8])
        name = first_name(r[9])

        grids = [(d3, w3, 24), (d2, w2, 16), (d1, w1, 8), (d0, w0, 0)]
        # cartesian product over non-wildcard alternatives
        combos = [({}, 0xFFFFFFFF)]
        for values, wild, shift in grids:
            new = []
            for base, mask in combos:
                if wild:
                    nb = dict(base)
                    nb[shift] = values[0]
                    new.append((nb, mask & ~(0xFF << shift)))
                else:
                    for v in values:
                        nb = dict(base)
                        nb[shift] = v
                        new.append((nb, mask))
            combos = new

        for base, mask in combos:
            di = 0
            for shift, v in base.items():
                di |= (v & 0xFF) << shift
            key = (di, mask)
            if key in seen:
                continue
            seen.add(key)
            cat = (di >> 24) & 0xFF
            entries.append({
                "di": di,
                "mask": mask,
                "name": name,
                "unit": unit,
                "format": fmt,
                "len": length,
                "decimals": dec,
                "flags": flags | (0x10 if (mask & 0xFF00) == 0 else 0)
                         | (0x20 if (mask & 0xFF) == 0 else 0),
                "category": cat,
                "wild_rate": 1 if (mask & 0xFF00) == 0 else 0,
                "wild_day": 1 if (mask & 0xFF) == 0 else 0,
            })
    entries.sort(key=lambda e: (e["di"], -bin(e["mask"]).count("1")))
    return entries


def c_string(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def write_json(entries, path):
    with io.open(path, "w", encoding="utf-8") as f:
        json.dump(entries, f, ensure_ascii=False, indent=1)


def write_inc(entries, path):
    lines = []
    lines.append("/* Auto-generated by tools/gen_di_table.py - do not edit. */\n")
    lines.append("/* Source: DL/T 645-2007 Appendix A (data identifier coding). */\n\n")
    lines.append("static const dlt645_di_info_t dlt645_di_table[] = {\n")
    for e in entries:
        cat = CATEGORY.get(e["category"], ("DLT645_DI_CAT_OTHER", ""))[0]
        lines.append(
            "    { 0x%08Xu, 0x%08Xu, %s, %s, %s, %u, %u, 0x%02Xu, %s },\n"
            % (e["di"], e["mask"], c_string(e["name"]), c_string(e["unit"]),
               c_string(e["format"]), e["len"], e["decimals"], e["flags"], cat))
    lines.append("};\n\n")
    lines.append("static const size_t dlt645_di_table_size = "
                 "sizeof(dlt645_di_table) / sizeof(dlt645_di_table[0]);\n")
    with io.open(path, "w", encoding="utf-8") as f:
        f.write("".join(lines))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    pdf = sys.argv[1]
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_json = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        base, "tools", "dlt645_di_catalog.json")
    out_inc = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
        base, "src", "dlt645_di_table.inc")

    rows = extract_rows(pdf)
    entries = expand(rows)
    write_json(entries, out_json)
    write_inc(entries, out_inc)
    print("rows=%d entries=%d" % (len(rows), len(entries)))
    print("wrote", out_json)
    print("wrote", out_inc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
