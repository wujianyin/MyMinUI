from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable
from fontTools import merge

def to_ttf(path_in, path_out, max_err=1.0):
    f = TTFont(path_in)
    assert 'CFF ' in f
    order = f.getGlyphOrder()
    gs = f.getGlyphSet()
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(gs)
        gs[name].draw(Cu2QuPen(pen, max_err, reverse_direction=True))
        glyphs[name] = pen.glyph()
    f['loca'] = newTable('loca')
    f['glyf'] = glyf = newTable('glyf')
    glyf.glyphOrder = order
    glyf.glyphs = glyphs
    del f['CFF ']
    for t in ('VORG',):
        if t in f: del f[t]
    glyf.compile(f)
    hmtx = f['hmtx']
    for name, g in glyf.glyphs.items():
        if hasattr(g, 'xMin'):
            hmtx[name] = (hmtx[name][0], g.xMin)
    maxp = f['maxp']
    maxp.tableVersion = 0x00010000
    maxp.maxZones = 1; maxp.maxTwilightPoints = 0; maxp.maxStorage = 0
    maxp.maxFunctionDefs = 0; maxp.maxInstructionDefs = 0; maxp.maxStackElements = 0
    maxp.maxSizeOfInstructions = 0
    maxp.maxComponentElements = max((len(getattr(g, 'components', [])) for g in glyf.glyphs.values()), default=0)
    post = f['post']; post.formatType = 2.0; post.extraNames = []; post.mapping = {}; post.glyphOrder = order
    f.sfntVersion = '\x00\x01\x00\x00'
    f.save(path_out)

to_ttf('/Users/wujianyin/work/gitwork/MyMinUI/skeleton/SYSTEM/res/BPreplayBold-unhinted.otf', 'latin.ttf')
print('latin ok')
to_ttf('noto_subset.otf', 'cjk.ttf')
print('cjk ok')
out = merge.Merger().merge(['latin.ttf', 'cjk.ttf'])
out.save('merged.ttf')
g = TTFont('merged.ttf'); cm = g.getBestCmap()
print('merged glyphs', len(g.getGlyphOrder()), 'cmap', len(cm), 'hhea', g['hhea'].ascent, g['hhea'].descent, g['hhea'].lineGap)
print(all(c in cm for c in (0x41, 0x4e2d, 0x8d85, 0x3001, 0xff1a)))
