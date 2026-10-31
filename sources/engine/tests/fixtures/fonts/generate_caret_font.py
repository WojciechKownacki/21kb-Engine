"""Generate the MIT-licensed synthetic caret fixture with fontTools 4.67.0."""
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString


def glyph(rectangles):
    pen = TTGlyphPen(None)
    for left, bottom, right, top in rectangles:
        pen.moveTo((left, bottom))
        pen.lineTo((right, bottom))
        pen.lineTo((right, top))
        pen.lineTo((left, top))
        pen.closePath()
    return pen.glyph()


builder = FontBuilder(1000, isTTF=True)
builder.setupGlyphOrder([".notdef", "space", "f", "i", "fi"])
builder.setupCharacterMap({32: "space", 102: "f", 105: "i"})
builder.setupGlyf({
    ".notdef": glyph([(0, 0, 400, 700)]),
    "space": glyph([]),
    "f": glyph([(0, 0, 150, 700)]),
    "i": glyph([(0, 0, 750, 700)]),
    "fi": glyph([(0, 0, 150, 700), (250, 0, 950, 700)]),
})
builder.setupHorizontalMetrics({name: (width, 0) for name, width in
    [(".notdef", 500), ("space", 250), ("f", 200), ("i", 800), ("fi", 1000)]})
builder.setupHorizontalHeader(ascent=800, descent=-200)
builder.setupNameTable({"familyName": "CaretTest", "styleName": "Regular",
    "uniqueFontIdentifier": "CaretTest-Regular-1", "fullName": "CaretTest Regular",
    "psName": "CaretTest-Regular", "version": "Version 1.000",
    "licenseDescription": "Distributed under the repository MIT license."})
builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
builder.setupPost()
builder.setupMaxp()
addOpenTypeFeaturesFromString(builder.font,
    "feature liga { sub f i by fi; } liga; table GDEF { LigatureCaretByPos fi 200; } GDEF;")
builder.font.recalcTimestamp = False
builder.font["head"].created = builder.font["head"].modified = 2082844800
builder.save(Path(__file__).with_name("CaretTest.ttf"))
