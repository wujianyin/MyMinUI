#!/usr/bin/env python3
"""Build a 60 Hz variant of the SJGAM M22Pro mtd0 (U-Boot package + DTB) image OFFLINE.

Nothing is flashed by this script. It only reads a mtd0 dump and writes a new file.

What it does (differs from the upstream `dd` recipe on purpose):
  * patches the seven LCD timing words *in place* inside the existing hx8399 DTB
    (no dtc round trip, so the DTB keeps exactly the same size and layout);
  * keeps the `sunxi-package` additive checksum valid WITHOUT touching the package header sector:
    a compensating 32-bit word is written into the zero-filled slack after the DTB (still inside
    the checksummed range). Use --fix-header to recompute the header checksum instead;
  * leaves boot0 (offset 0, 28 KiB) and every other byte untouched;
  * prints a full verification report (what changed, checksum, bytes differing).

Usage:
  patch_mtd0_60fps.py <mtd0_dump.bin> <out.bin> [--restore] [--fix-header]
  --restore     write the stock timings back (use on a patched image)
  --fix-header  update the checksum field in the header sector instead of the slack word
"""
import struct
import sys

# stock values (orig.dts) and the values from upstream's edited.dts
STOCK = {"lcd_dclk_freq": 0x5A, "lcd_hbp": 0x20, "lcd_ht": 0x4CE, "lcd_hspw": 0x08,
         "lcd_vbp": 0x07, "lcd_vt": 0x799, "lcd_vspw": 0x06}
FAST = {"lcd_dclk_freq": 0xA2, "lcd_hbp": 0x78, "lcd_ht": 0x546, "lcd_hspw": 0x14,
        "lcd_vbp": 0x05, "lcd_vt": 0x7E0, "lcd_vspw": 0x03}
NODE = "lcd0@5461000"
PKG = 0x10000                # sunxi-package offset inside mtd0
STAMP = 0x5F0A6C39           # add_sum field is replaced by this stamp while summing


def u32be(b, o): return struct.unpack_from(">I", b, o)[0]


def find_dtbs(d):
    out, i = [], 0
    while True:
        i = d.find(b"\xd0\x0d\xfe\xed", i)
        if i < 0: return out
        total = u32be(d, i + 4)
        if 0x1000 < total < 0x40000: out.append((i, total))
        i += 4


def fdt_props(d, base):
    """yield (nodepath, propname, data_offset_in_d, length) for every property of the DTB at `base`"""
    off_struct, off_strings = u32be(d, base + 8), u32be(d, base + 12)
    p = base + off_struct
    path = []
    while True:
        tok = u32be(d, p); p += 4
        if tok == 1:
            end = d.index(b"\0", p)
            name = d[p:end].decode()
            path.append(name)
            p = (end + 1 + 3) & ~3
        elif tok == 2:
            path.pop()
        elif tok == 3:
            ln, nameoff = u32be(d, p), u32be(d, p + 4)
            p += 8
            s = base + off_strings + nameoff
            pname = d[s:d.index(b"\0", s)].decode()
            yield "/".join(path), pname, p, ln
            p = (p + ln + 3) & ~3
        elif tok == 4:
            continue
        elif tok == 9:
            return
        else:
            raise SystemExit("bad FDT token %#x at %#x" % (tok, p - 4))


def checksum(d):
    magic, = struct.unpack_from("<I", d, PKG + 16)
    assert magic == 0x89119800, "sunxi-package header not found"
    vlen, = struct.unpack_from("<I", d, PKG + 16 + 20)
    body = d[PKG:PKG + vlen]
    s = 0
    for i in range(0, vlen, 4):
        v = struct.unpack_from("<I", body, i)[0]
        if i == 20: v = STAMP
        s = (s + v) & 0xFFFFFFFF
    return s, vlen


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    restore = "--restore" in sys.argv
    if len(args) != 2: raise SystemExit(__doc__)
    src, dst = args
    d = bytearray(open(src, "rb").read())
    assert len(d) == 0x100000, "expected a 1 MiB mtd0 dump"

    stored, = struct.unpack_from("<I", d, PKG + 20)
    calc, vlen = checksum(d)
    print("package: valid_len=%#x stored add_sum=%#010x calculated=%#010x -> %s" %
          (vlen, stored, calc, "OK" if stored == calc else "MISMATCH (input is not a clean dump!)"))
    if stored != calc: raise SystemExit(1)

    target = None
    for off, total in find_dtbs(d):
        props = [(n, p, o, l) for n, p, o, l in fdt_props(d, off) if NODE in n]
        names = {p: (o, l) for n, p, o, l in props}
        drv = d[names["lcd_driver_name"][0]:names["lcd_driver_name"][0] + 6] if "lcd_driver_name" in names else b""
        print("DTB at %#x size %d lcd driver=%s" % (off, total, drv.decode(errors="replace") or "-"))
        if drv == b"hx8399":
            target = (off, total, names)
    if not target: raise SystemExit("hx8399 DTB not found")
    off, total, names = target
    assert off + total <= PKG + vlen, "DTB must lie inside the checksummed package"

    want = STOCK if restore else FAST
    for k, v in want.items():
        o, l = names[k]
        assert l == 4
        old = u32be(d, o)
        struct.pack_into(">I", d, o, v)
        print("  %-14s %#06x (%d) -> %#06x (%d)" % (k, old, old, v, v))

    new_sum, _ = checksum(d)
    if "--fix-header" in sys.argv:
        struct.pack_into("<I", d, PKG + 20, new_sum)
        print("header add_sum updated -> %#010x (header sector will change)" % new_sum)
    else:
        slack = off + total + 0x330          # inside the zero-filled slack after the DTB, 4-byte aligned
        assert slack % 4 == 0 and slack + 4 <= PKG + vlen
        assert d[slack:slack + 4] == b"\0\0\0\0" or restore, "slack word is not zero"
        cur, = struct.unpack_from("<I", d, slack)
        delta = (stored - new_sum + cur) & 0xFFFFFFFF   # make the total equal the stored add_sum again
        struct.pack_into("<I", d, slack, delta)
        print("compensation word at %#x = %#010x (header untouched, add_sum stays %#010x)" % (slack, delta, stored))
    assert checksum(d)[0] == struct.unpack_from("<I", d, PKG + 20)[0], "package checksum would be wrong"
    print("package checksum verified")

    # timing sanity
    def fps(dclk, ht, vt): return dclk * 1e6 / (ht * vt)
    print("refresh: %.2f Hz  (stock would be %.2f Hz)" % (fps(want["lcd_dclk_freq"], want["lcd_ht"], want["lcd_vt"]),
                                                          fps(STOCK["lcd_dclk_freq"], STOCK["lcd_ht"], STOCK["lcd_vt"])))
    print("DSI lane rate (4 lanes, RGB888): %.0f Mbit/s (stock %.0f)" % (want["lcd_dclk_freq"] * 24 / 4, STOCK["lcd_dclk_freq"] * 24 / 4))

    orig = open(src, "rb").read()
    diff = [i for i in range(len(d)) if d[i] != orig[i]]
    first = min(diff) if diff else None
    print("bytes changed: %d, in range %#x..%#x; boot0 area (0..0x7000) changed: %s; size unchanged: %s" %
          (len(diff), first, max(diff), any(i < 0x7000 for i in diff), len(d) == len(orig)))
    print("4 KiB sectors that must be rewritten (erase size is 4 KiB): %s" % ", ".join(sorted({"%#x" % (i & ~0xFFF) for i in diff})))
    print("package header sector (0x10000) changed: %s" % any(0x10000 <= i < 0x11000 for i in diff))
    open(dst, "wb").write(d)
    print("wrote", dst)


if __name__ == "__main__":
    main()
