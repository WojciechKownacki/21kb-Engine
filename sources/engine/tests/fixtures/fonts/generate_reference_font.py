"""Build explicit glyph references from the pinned OFL font (fontTools 4.67.0)."""
from pathlib import Path
from fontTools.ttLib import TTFont

root = Path(__file__).parent
font = TTFont(root / "NotoSansDevanagari-Regular.ttf", recalcTimestamp=False)
names = ["k_ssa-deva", "iMatra-deva.04", "ka-deva", "iMatra-deva.10",
         "na-deva", "ma-deva", "s-deva", "ta-deva", "eMatra-deva"]
for table in font["cmap"].tables:
    if table.isUnicode():
        table.cmap.update({0xE000 + index: name for index, name in enumerate(names)})
# Reference input names the final glyphs directly, without running contextual
# substitution or positioning through the engine's shaping path again.
for tag in ["GSUB", "GPOS"]:
    if tag in font:
        del font[tag]
for record in font["name"].names:
    if record.nameID in [1, 3, 4, 6, 16]:
        record.string = "ShapingReference".encode(record.getEncoding())
font["head"].created = font["head"].modified = 2082844800
font.save(root / "ShapingReference.ttf")
