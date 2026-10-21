import codecs
from fontTools import subset, merge
from fontTools.ttLib import TTFont

SRC_LATIN = '/Users/wujianyin/work/gitwork/MyMinUI/skeleton/SYSTEM/res/BPreplayBold-unhinted.otf'
SRC_CJK = 'NotoSansCJKsc-Regular.otf'

chars = set()
# GB2312 (hanzi + symbols), GBK first 2 bytes range limited to GB2312 area
for hi in range(0xA1, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.add(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError:
            pass
for lo, hi in [(0x2000,0x206F),(0x2190,0x21FF),(0x25A0,0x25FF),(0x3000,0x303F),(0x3040,0x30FF),(0xFF00,0xFFEF),(0x2460,0x24FF),(0x2160,0x217F)]:
    for c in range(lo, hi+1): chars.add(chr(c))
print('requested chars', len(chars))

opts = subset.Options()
opts.layout_features = []          # no GSUB/GPOS needed
opts.name_IDs = ['*']
opts.notdef_outline = True
opts.hinting = False
opts.drop_tables += ['DSIG','GDEF','GPOS','GSUB','BASE','vhea','vmtx','VORG']
opts.glyph_names = False
f = TTFont(SRC_CJK)
s = subset.Subsetter(opts)
s.populate(unicodes=[ord(c) for c in chars])
s.subset(f)
f.save('noto_subset_r.otf')
print('subset glyphs', len(f.getGlyphOrder()))

