import sys
sys.argv=['x']
exec(open('otf2ttf_merge.py').read().split("to_ttf('/Users")[0])   # reuse to_ttf()
to_ttf('noto_subset_r.otf','cjk_r.ttf')
from fontTools.ttLib import TTFont
from fontTools import merge
out = merge.Merger().merge(['FreeSans.orig.ttf','cjk_r.ttf'])
out.save('merged_free.ttf')
g=TTFont('merged_free.ttf'); o=TTFont('FreeSans.orig.ttf')
g['hhea'].ascent=o['hhea'].ascent; g['hhea'].descent=o['hhea'].descent; g['hhea'].lineGap=o['hhea'].lineGap
for k in ('sTypoAscender','sTypoDescender','sTypoLineGap','usWinAscent','usWinDescent'):
    setattr(g['OS/2'],k,getattr(o['OS/2'],k))
g['OS/2'].fsSelection=o['OS/2'].fsSelection
g.save('FreeSans.cjk.ttf')
cm=g.getBestCmap()
print('glyphs',len(g.getGlyphOrder()),'cmap',len(cm),all(c in cm for c in (0x41,0x4e2d,0x8d85,0x3001)))
