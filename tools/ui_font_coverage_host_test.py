"""Check real reader-setting labels against the shipped UI font cmap.

SPDX-License-Identifier: Apache-2.0
中文：说明里缺一个字就会整行回退到阅读字体；直接检查固件内建字库。
English: One missing character sends the whole label to the reader face; inspect
the real embedded font instead of a mocked has-text result. Uses only stdlib.
"""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
font = (root / "main/assets/builtin.ttf").read_bytes()


def u16(offset):
    return struct.unpack_from(">H", font, offset)[0]


def u32(offset):
    return struct.unpack_from(">I", font, offset)[0]


tables = {}
for i in range(u16(4)):
    record = 12 + i * 16
    tables[font[record:record + 4]] = u32(record + 8)
base = tables[b"cmap"]
subtables = []
for i in range(u16(base + 2)):
    record = base + 4 + i * 8
    platform, encoding = u16(record), u16(record + 2)
    if platform == 0 or (platform == 3 and encoding in (1, 10)):
        offset = base + u32(record + 4)
        if u16(offset) in (4, 12):
            subtables.append(offset)
assert subtables, "No Unicode cmap in the embedded font"


def has_glyph(codepoint):
    for offset in subtables:
        if u16(offset) == 12:
            for i in range(u32(offset + 12)):
                group = offset + 16 + i * 12
                first, last = u32(group), u32(group + 4)
                if first <= codepoint <= last:
                    return bool(u32(group + 8) + codepoint - first)
        elif codepoint <= 0xffff:
            count = u16(offset + 6) // 2
            ends = offset + 14
            starts = ends + 2 * count + 2
            deltas = starts + 2 * count
            ranges = deltas + 2 * count
            for i in range(count):
                first, last = u16(starts + i * 2), u16(ends + i * 2)
                if first <= codepoint <= last:
                    delta, distance = u16(deltas + i * 2), u16(ranges + i * 2)
                    if not distance:
                        return bool((codepoint + delta) & 0xffff)
                    glyph = u16(ranges + i * 2 + distance + 2 * (codepoint - first))
                    return bool(glyph and ((glyph + delta) & 0xffff))
    return False


source = (root / "main/apps/app_book.c").read_text(encoding="utf-8")
first = source.index("static void draw_font_settings(")
last = source.index("static void draw_font_picker(", first)
# Covers the font sheet, the reading sheet, their explanations, and the
# horizontal/vertical page diagrams. Dynamic book text and user font names
# intentionally use other paths.
section = source[first:last]
labels = re.findall(r'"([^"\n]*)"', section)
missing = {}
for label in labels:
    absent = "".join(dict.fromkeys(ch for ch in label if not has_glyph(ord(ch))))
    if absent:
        missing[label] = absent
assert not missing, f"Reader-setting labels fall back to the reading font: {missing}"
print(f"PASS: {len(labels)} real reader-setting labels have complete system UI glyphs")
