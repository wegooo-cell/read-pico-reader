#!/usr/bin/env python3
"""
中文：生成翻页基准测试用的固定 EPUB（kiiko-bench.epub）。同一版本的脚本每次生成的书内容一致，
基准测试的章节结构依赖这里的布局，改动时同步修改 BENCH_VERSION 和 turn_bench.py 的场景。

English: build the fixed EPUB used by the turn benchmark. The same script version always produces
the same book; turn_bench.py scenarios depend on this chapter layout, so bump BENCH_VERSION and
update the scenarios together.

章节（从 0 开始编号，与 `perf open <章>` 一致）/ Chapters (0-based, as used by `perf open`):
  0 纯文字甲：约 2 万字，一级字按 Zipf 字频，跑普通/低残影/快速灰阶/水波纹 / dense common Han, effect phases
  1 插图：文字与插图交替，8 张图（JPEG 与 PNG）/ text interleaved with 8 JPEG/PNG images
  2 短章：约 1500 字，用来测跨章 / short chapter for chapter crossing
  3 纯文字乙：换成二级字（同样按 Zipf），测字形缓存冷启动 / less common Han, cold glyph cache
  4 尾声：短章 / short tail

需要 Pillow 生成插图。/ Needs Pillow for the illustrations.
用法 / usage: python3 make_bench_epub.py [-o kiiko-bench.epub]
"""
import argparse
import hashlib
import io
import itertools
import random
import zipfile

from PIL import Image, ImageDraw

BENCH_VERSION = 2
SEED = 20261123
ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def gb2312_level1():
    # GB2312 一级汉字（B0A1–D7F9），按拼音排序的 3755 个常用字。/ GB2312 level-1 Han.
    chars = []
    for row in range(0xB0, 0xD8):
        for col in range(0xA1, 0xFF):
            try:
                chars.append(bytes([row, col]).decode('gb2312'))
            except UnicodeDecodeError:
                pass
    return chars


def gb2312_level2():
    # GB2312 二级汉字（D8A1–F7FE），较少用。/ GB2312 level-2, less common.
    chars = []
    for row in range(0xD8, 0xF8):
        for col in range(0xA1, 0xFF):
            try:
                chars.append(bytes([row, col]).decode('gb2312'))
            except UnicodeDecodeError:
                pass
    return chars


class ZipfPool:
    # 真实中文字频近似 Zipf（前 1000 字约占九成）；指数 1.1 时每页约 70 个前两页没出现过的字，
    # 与真书实测的 30–80 个字形未命中相当。均匀抽字会让每页 200 多个新字，夸大未命中。
    # Real Chinese character frequency is roughly Zipfian; exponent 1.1 gives about 70 characters
    # per page unseen in the previous two pages, matching 30-80 glyph misses measured on real books.
    def __init__(self, chars, seed, exponent=1.1):
        self.chars = chars[:]
        random.Random(seed).shuffle(self.chars)
        self.cum = list(itertools.accumulate(1 / (i + 1) ** exponent for i in range(len(self.chars))))

    def pick(self, rng):
        return rng.choices(self.chars, cum_weights=self.cum)[0]


def paragraph(rng, pool, length):
    out, run = [], 0
    while len(out) < length:
        out.append(pool.pick(rng))
        run += 1
        if run >= rng.randint(7, 16):
            out.append('。' if rng.random() < 0.3 else '，')
            run = 0
    if out[-1] in '，。':
        out.pop()
    out.append('。')
    return ''.join(out)


def paragraphs(rng, pool, total, size=(180, 420)):
    out, n = [], 0
    while n < total:
        p = paragraph(rng, pool, rng.randint(*size))
        out.append(p)
        n += len(p)
    return out


def jpeg_image(rng, w, h):
    # 渐变、圆形加细纹理，接近照片的连续灰阶和文件大小；baseline JPEG。
    # Gradient, discs and fine texture, close to a photo's tones and file size; baseline JPEG.
    img = Image.linear_gradient('L').rotate(rng.choice((0, 90, 180, 270))).resize((w, h), Image.BILINEAR)
    d = ImageDraw.Draw(img)
    for _ in range(16):
        r = rng.randint(w // 20, w // 6)
        cx, cy = rng.randint(0, w), rng.randint(0, h)
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=rng.randint(0, 255))
    tw, th = w // 4, h // 4
    noise = Image.frombytes('L', (tw, th), bytes(rng.randint(0, 255) for _ in range(tw * th)))
    img = Image.blend(img, noise.resize((w, h), Image.BICUBIC), 0.25)
    buf = io.BytesIO()
    img.save(buf, 'JPEG', quality=85, optimize=False, progressive=False, subsampling=0)
    return buf.getvalue()


def png_image(rng, w, h):
    # 线稿：白底黑线和文字块，模拟插画/图表。/ Line art on white, like diagrams.
    img = Image.new('L', (w, h), 255)
    d = ImageDraw.Draw(img)
    for _ in range(40):
        x0, y0 = rng.randint(0, w), rng.randint(0, h)
        x1, y1 = rng.randint(0, w), rng.randint(0, h)
        d.line((x0, y0, x1, y1), fill=rng.choice((0, 64, 128)), width=rng.randint(1, 4))
    for _ in range(6):
        x, y = rng.randint(0, w - 80), rng.randint(0, h - 40)
        d.rectangle((x, y, x + 80, y + 40), outline=0, width=2)
    buf = io.BytesIO()
    img.save(buf, 'PNG', optimize=False)
    return buf.getvalue()


def xhtml(title, body):
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!DOCTYPE html>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" '
            'xml:lang="zh-CN" lang="zh-CN">\n'
            f'<head><meta charset="utf-8"/><title>{title}</title></head>\n'
            f'<body>\n<h1>{title}</h1>\n{body}\n</body>\n</html>\n')


def build():
    rng = random.Random(SEED)
    level1, level2 = gb2312_level1(), gb2312_level2()
    common = ZipfPool(level1, SEED + 1)
    rare = ZipfPool(level2, SEED + 2)

    chapters, images = [], []
    ps = lambda items: '\n'.join(f'<p>{p}</p>' for p in items)

    chapters.append(('第一章　纯文字甲', ps(paragraphs(rng, common, 20000))))

    body = []
    for i in range(8):
        body.extend(f'<p>{p}</p>' for p in paragraphs(rng, common, 260, (120, 260)))
        if i % 2 == 0:
            name, data, mime = f'img{i}.jpg', jpeg_image(rng, 900, 1200), 'image/jpeg'
        else:
            name, data, mime = f'img{i}.png', png_image(rng, 900, 630), 'image/png'
        images.append((name, data, mime))
        body.append(f'<p><img src="images/{name}" alt="图{i + 1}"/></p>')
    body.extend(f'<p>{p}</p>' for p in paragraphs(rng, common, 260, (120, 260)))
    chapters.append(('第二章　插图', '\n'.join(body)))

    chapters.append(('第三章　短章', ps(paragraphs(rng, common, 1500))))
    chapters.append(('第四章　纯文字乙', ps(paragraphs(rng, rare, 8000))))
    chapters.append(('第五章　尾声', ps(paragraphs(rng, common, 800))))

    title = f'kiiko 翻页基准 v{BENCH_VERSION}'
    files = {}
    files['META-INF/container.xml'] = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
        '<rootfiles><rootfile full-path="OEBPS/content.opf" '
        'media-type="application/oebps-package+xml"/></rootfiles>\n</container>\n')
    manifest, spine, navs, ncx = [], [], [], []
    for i, (name, body) in enumerate(chapters):
        fn = f'ch{i:02d}.xhtml'
        files[f'OEBPS/{fn}'] = xhtml(name, body)
        manifest.append(f'<item id="ch{i}" href="{fn}" media-type="application/xhtml+xml"/>')
        spine.append(f'<itemref idref="ch{i}"/>')
        navs.append(f'<li><a href="{fn}">{name}</a></li>')
        ncx.append(f'<navPoint id="n{i}" playOrder="{i + 1}"><navLabel><text>{name}</text></navLabel>'
                   f'<content src="{fn}"/></navPoint>')
    for name, data, mime in images:
        files[f'OEBPS/images/{name}'] = data
        manifest.append(f'<item id="{name.replace(".", "_")}" href="images/{name}" media-type="{mime}"/>')
    files['OEBPS/nav.xhtml'] = (
        '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">\n'
        f'<head><title>{title}</title></head><body><nav epub:type="toc"><ol>\n'
        + '\n'.join(navs) + '\n</ol></nav></body></html>\n')
    files['OEBPS/toc.ncx'] = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">\n'
        f'<head><meta name="dtb:uid" content="kiiko-bench-v{BENCH_VERSION}"/></head>\n'
        f'<docTitle><text>{title}</text></docTitle><navMap>\n' + '\n'.join(ncx) + '\n</navMap></ncx>\n')
    manifest.append('<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')
    manifest.append('<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>')
    files['OEBPS/content.opf'] = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid" xml:lang="zh-CN">\n'
        '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        f'<dc:identifier id="uid">kiiko-bench-v{BENCH_VERSION}</dc:identifier>\n'
        f'<dc:title>{title}</dc:title><dc:language>zh-CN</dc:language><dc:creator>kiikoread</dc:creator>\n'
        '<meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>\n</metadata>\n'
        '<manifest>\n' + '\n'.join(manifest) + '\n</manifest>\n'
        '<spine toc="ncx">\n' + '\n'.join(spine) + '\n</spine>\n</package>\n')
    return files


def write_epub(path, files):
    with zipfile.ZipFile(path, 'w') as z:
        info = zipfile.ZipInfo('mimetype', ZIP_TIME)
        info.compress_type = zipfile.ZIP_STORED
        z.writestr(info, 'application/epub+zip')
        for name in sorted(files):
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            data = files[name]
            z.writestr(info, data.encode('utf-8') if isinstance(data, str) else data, compresslevel=9)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument('-o', '--output', default='kiiko-bench.epub')
    args = ap.parse_args()
    write_epub(args.output, build())
    digest = hashlib.sha256(open(args.output, 'rb').read()).hexdigest()
    print(f'{args.output} v{BENCH_VERSION} sha256={digest}')


if __name__ == '__main__':
    main()
