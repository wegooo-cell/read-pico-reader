#!/usr/bin/env python3
"""
中文：Pico 翻页基准测试。通过 USB 测试命令驱动计时版固件翻页，收集 `turn: TURN` 行并按场景汇总。
只打印一张汇总表；原始日志和 JSON 结果写进输出目录，供以后对比。

English: Pico page-turn benchmark. Drives a timing build through the USB probe, collects
`turn: TURN` lines and summarizes them per scenario. Prints one compact table; the raw log and
JSON go to the output directory for later comparison.

子命令 / subcommands:
  flash   只写 ota_0（先核对分区表与启动槽，首次备份原应用）/ write ota_0 only, after checks and a one-time backup
  restore 把备份的原应用写回 ota_0 / write the backed-up application back to ota_0
  run     跑一轮基准 / run the benchmark
  compare 对比两次结果 / compare two results

需要 ESP-IDF 的 Python（含 pyserial、esptool）。/ Run with the ESP-IDF Python (pyserial, esptool).
固件需用 --pico-turn-timing 构建；TF 卡上放 make_bench_epub.py 生成的 kiiko-bench.epub。
Firmware must be built with --pico-turn-timing; put kiiko-bench.epub on the TF card.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import statistics
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_PACKAGE = os.path.join(ROOT, 'build/turn-timing/packages/pico')
DEFAULT_BACKUP = os.path.join(ROOT, 'build/device-backup')
DEFAULT_OUT = os.path.join(ROOT, 'build/bench/results')
DEFAULT_BOOK = '/sdcard/kiiko-bench.epub'
BENCH_VERSION = 2

TURN_RE = re.compile(r'turn: TURN (.*)')
FIELD_RE = re.compile(r'(\w+)=(\S+)')
CHAPTER_RE = re.compile(r'book: turn chapter=(\d+) page=(\d+)/(\d+)')


def die(msg):
    print(f'error: {msg}', file=sys.stderr)
    sys.exit(1)


def find_port(port):
    if port:
        return port
    ports = sorted(glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*'))
    if len(ports) != 1:
        die(f'specify --port; found {ports or "no USB serial ports"}')
    return ports[0]


# ---- 刷机 / Flashing ----

def esptool(port, *args, quiet=True):
    cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '-p', port, '-b', '921600', *args]
    # 设备刚睡眠或刚复位时第一次连接偶尔失败，重试一次。/ Retry once: the first connect can fail right after sleep or reset.
    for attempt in range(2):
        r = subprocess.run(cmd, capture_output=quiet, text=True)
        if not r.returncode:
            return
        time.sleep(2)
    if r.returncode:
        die('esptool failed: ' + ' '.join(args) + ('\n' + r.stdout[-2000:] + r.stderr[-2000:] if quiet else ''))


def parse_partitions(data):
    parts = {}
    for i in range(0, len(data), 32):
        magic, ptype, subtype, offset, size = struct.unpack_from('<HBBII', data, i)
        if magic != 0x50AA:
            break
        label = data[i + 12:i + 28].split(b'\0')[0].decode()
        parts[label] = (ptype, subtype, offset, size)
    return parts


def boot_slot(otadata):
    # 两个 32 字节记录，序号大且 CRC 有效者生效；全空表示 ota_0。
    # Two 32-byte records; the valid one with the higher sequence wins; blank means ota_0.
    import binascii
    best = 0
    for base in (0, 0x1000):
        seq = struct.unpack_from('<I', otadata, base)[0]
        crc = struct.unpack_from('<I', otadata, base + 28)[0]
        if seq in (0, 0xFFFFFFFF):
            continue
        if binascii.crc32(struct.pack('<I', seq), 0xFFFFFFFF) != crc:
            continue
        best = max(best, seq)
    return 0 if best == 0 else (best - 1) % 2


def device_layout(port, work):
    os.makedirs(work, exist_ok=True)
    table = os.path.join(work, 'device-partitions.bin')
    esptool(port, 'read-flash', '0x8000', '0xc00', table)
    parts = parse_partitions(open(table, 'rb').read())
    if 'ota_0' not in parts or 'otadata' not in parts:
        die(f'unexpected partition table: {parts}')
    ota = os.path.join(work, 'device-otadata.bin')
    esptool(port, 'read-flash', hex(parts['otadata'][2]), hex(parts['otadata'][3]), ota)
    return table, parts, boot_slot(open(ota, 'rb').read())


def cmd_flash(args):
    port = find_port(args.port)
    firmware = os.path.join(args.package, 'firmware.bin')
    manifest = json.load(open(os.path.join(args.package, 'manifest.json')))
    if not manifest.get('local_turn_timing'):
        die('package is not a --pico-turn-timing build')
    if manifest.get('board') != 'pico':
        die('package is not for the pico board')
    table, parts, slot = device_layout(port, args.backup)
    if open(table, 'rb').read().rstrip(b'\xff') != open(os.path.join(args.package, 'partitions.bin'), 'rb').read().rstrip(b'\xff'):
        die('device partition table differs from the package; refusing to write ota_0 only')
    if slot != 0:
        die('device boots ota_1; writing ota_0 would not take effect. Install a full package instead.')
    offset, size = parts['ota_0'][2], parts['ota_0'][3]
    if os.path.getsize(firmware) > size:
        die('firmware larger than ota_0')
    backup = os.path.join(args.backup, 'dev_ota0.bin')
    if not os.path.exists(backup):
        print(f'backing up ota_0 to {backup}')
        esptool(port, 'read-flash', hex(offset), hex(size), backup)
    else:
        print(f'keeping existing backup {backup}')
    print(f'writing {firmware} to {hex(offset)}')
    esptool(port, 'write-flash', hex(offset), firmware)
    print('done; device rebooted')


def cmd_restore(args):
    port = find_port(args.port)
    backup = os.path.join(args.backup, 'dev_ota0.bin')
    if not os.path.exists(backup):
        die(f'no backup at {backup}')
    _, parts, slot = device_layout(port, args.backup)
    if slot != 0:
        die('device boots ota_1; not touching ota_0')
    offset = parts['ota_0'][2]
    # 备份是整个分区，尾部 0xff 不必写。/ The backup spans the whole partition; skip trailing 0xff.
    data = open(backup, 'rb').read().rstrip(b'\xff')
    trimmed = os.path.join(args.backup, 'dev_ota0.trimmed.bin')
    with open(trimmed, 'wb') as f:
        f.write(data + b'\xff' * (-len(data) % 4096))
    print(f'restoring {backup} to {hex(offset)}')
    esptool(port, 'write-flash', hex(offset), trimmed)
    print('done; device rebooted')


# ---- 串口会话 / Serial session ----

class Device:
    def __init__(self, port, log_path):
        import serial
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()  # 打开串口会让设备复位。/ Opening the port resets the device.
        self.log = open(log_path, 'w', encoding='utf-8', errors='replace')
        self.buf = b''
        self.boot = {}

    def lines(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            data = self.ser.read(4096)
            if not data:
                continue
            self.buf += data
            while b'\n' in self.buf:
                raw, self.buf = self.buf.split(b'\n', 1)
                line = raw.decode('utf-8', 'replace').rstrip('\r')
                self.log.write(line + '\n')
                yield line

    def wait(self, pattern, timeout, what=None):
        rx = re.compile(pattern)
        for line in self.lines(timeout):
            m = rx.search(line)
            if m:
                return m
        die(f'timeout waiting for {what or pattern}')

    def drain(self, seconds):
        for _ in self.lines(seconds):
            pass

    def send(self, cmd):
        self.log.write(f'>>> {cmd}\n')
        self.ser.write((cmd + '\n').encode())
        self.ser.flush()

    def close(self):
        self.ser.close()
        self.log.close()


class Bench:
    def __init__(self, dev, book, expect_size=None):
        self.dev, self.book, self.expect_size = dev, book, expect_size
        self.turns, self.warnings = [], []
        self.chapter = None

    def info(self):
        self.dev.send('perf info')
        m = self.dev.wait(r'latency: INFO page=(-?\d+)', 5, 'INFO')
        r = self.dev.wait(r'latency: READER fx=(\d+) lg=(\d+) px=(\d+)', 5, 'READER info')
        return int(m.group(1)), {'fx': int(r.group(1)), 'lg': int(r.group(2)), 'px': int(r.group(3))}

    def set_fx(self, fx):
        self.dev.send(f'perf turnfx {fx}')
        self.dev.wait(rf'latency: TURNFX {fx}\b', 5, 'TURNFX')

    def set_lg(self, lg):
        self.dev.send(f'perf lowghost {lg}')
        self.dev.wait(rf'latency: LOWGHOST {lg}\b', 5, 'LOWGHOST')

    def home(self):
        page, _ = self.info()
        if page == 0:
            return
        self.dev.send('perf page 0')
        self.dev.wait(r'latency: SWITCH from=\d+ to=0', 15, 'switch to Home')
        self.dev.drain(1.0)

    def open_at(self, chapter):
        self.home()
        self.dev.send(f'perf open {chapter} {self.book}')
        m = self.dev.wait(r'latency: OPEN chapter=\d+ error=(\S+)(?: size=(\d+))?', 5, 'OPEN')
        if m.group(1) != 'ESP_OK':
            die(f'cannot open {self.book}: {m.group(1)} (is kiiko-bench.epub on the TF card?)')
        if self.expect_size and m.group(2) and int(m.group(2)) != self.expect_size:
            die(f'bench book on the card is {m.group(2)} bytes, local build is {self.expect_size}; '
                'copy the freshly generated kiiko-bench.epub to the TF card')
        o = self.dev.wait(r'book: opened kind=\d+ chapters=(\d+) pages=(\d+) px=(\d+)', 20, 'book opened')
        if int(o.group(1)) != 5:
            die(f'bench book has {o.group(1)} chapters, expected 5; regenerate it with make_bench_epub.py')
        self.chapter = chapter
        self.dev.drain(3.0)

    def turn(self, key, label, settle=1.5):
        self.dev.send(f'perf key {key}')
        chapter = None
        for line in self.dev.lines(15):
            c = CHAPTER_RE.search(line)
            if c:
                chapter = int(c.group(1))
            t = TURN_RE.search(line)
            if t:
                rec = {k: v for k, v in FIELD_RE.findall(t.group(1))}
                for k, v in list(rec.items()):
                    if k in ('dir', 'hit', 'img', 'fx', 'lg', 'pf', 'glyphs', 'miss', 'scans', 'frames', 'skip'):
                        rec[k] = int(v)
                    elif k == 'page':
                        rec['page'], rec['pages'] = map(int, v.split('/'))
                    elif k != 'path':
                        rec[k] = float(v)
                crossed = chapter is not None and self.chapter is not None and chapter != self.chapter
                rec['chapter'] = chapter if chapter is not None else self.chapter
                rec['label'] = 'chapter_fwd' if crossed and key == 3 else 'chapter_back' if crossed else label
                if crossed and not label.startswith('walk'):
                    self.warnings.append(f'{label} crossed into chapter {chapter}')
                self.chapter = rec['chapter']
                self.turns.append(rec)
                self.dev.drain(settle)
                return rec
        die(f'no TURN line after key {key} ({label}); is this a --pico-turn-timing build in the reader?')

    def run(self):
        # 场景依赖 make_bench_epub.py 的章节布局。/ Scenarios depend on make_bench_epub.py's layout.
        self.set_fx(0)
        self.set_lg(0)
        self.open_at(0)
        self.turn(3, 'first_after_open')
        for _ in range(8):
            self.turn(3, 'text_fwd')
        for _ in range(4):
            self.turn(1, 'text_back')
        for k in (3, 1, 3, 1):
            self.turn(k, 'alternate')
        self.set_lg(1)
        for _ in range(6):
            self.turn(3, 'lowghost')
        self.set_lg(0)
        self.set_fx(3)
        for _ in range(6):
            self.turn(3, 'fastgray')
        self.set_fx(1)
        for _ in range(6):
            self.turn(3, 'ripple')
        self.set_fx(0)

        # 插图章：一直往前到跨入短章，再穿过短章进入纯文字乙。
        # Image chapter: forward into the short chapter, then through it into the second text chapter.
        self.open_at(1)
        prev_img = 0
        for _ in range(40):
            rec = self.turn(3, 'walk_images')
            if rec['label'] == 'walk_images':
                rec['label'] = 'image' if rec['img'] else 'after_image' if prev_img else 'image_ch_text'
            prev_img = rec['img']
            if rec['chapter'] == 2:
                break
        for _ in range(40):
            rec = self.turn(3, 'walk_short')
            if rec['chapter'] == 3:
                break
        self.turn(1, 'walk_back')            # 跨回短章 / back across into the short chapter
        self.turn(3, 'walk_fwd')             # 再跨进纯文字乙 / forward into the second text chapter
        for _ in range(6):
            self.turn(3, 'cold_text')
        self.home()


# ---- 汇总 / Summary ----

LABEL_ORDER = ['first_after_open', 'text_fwd', 'text_back', 'alternate', 'lowghost', 'fastgray', 'ripple',
               'image', 'after_image', 'image_ch_text', 'walk_short', 'cold_text', 'chapter_fwd', 'chapter_back']


def summarize(turns):
    groups = {}
    for t in turns:
        groups.setdefault(t['label'], []).append(t)
    out = {}
    for label, ts in groups.items():
        med = lambda k: round(statistics.median(t[k] for t in ts), 1)
        out[label] = {
            'n': len(ts),
            'hit_rate': round(sum(t['hit'] == 1 for t in ts) / len(ts), 2),
            'fn': round(statistics.median(t['turn'] - t['save'] - t['load'] for t in ts), 1),
            'img': round(statistics.median(t['prep'] + t['pre'] for t in ts), 1),
            'turn': med('turn'), 'load': med('load'), 'join': med('join'), 'draw': med('draw'), 'push': med('push'),
            'total': med('total'), 'frames': med('frames'), 'raster': med('raster'), 'miss': med('miss'),
        }
    saves = [t['save'] for t in turns if t['save'] > 0]
    out['_save'] = {'n': len(saves), 'median': round(statistics.median(saves), 1) if saves else 0}
    return out


def print_summary(summary):
    print(f'{"scenario":16}{"n":>3}{"hit":>5}{"fn":>7}{"img":>6}{"draw":>7}{"push":>7}{"join":>6}{"total":>7}{"frm":>5}{"miss":>5}')
    for label in LABEL_ORDER + sorted(k for k in summary if k not in LABEL_ORDER and not k.startswith('_')):
        s = summary.get(label)
        if not s:
            continue
        print(f'{label:16}{s["n"]:>3}{s["hit_rate"]:>5.2f}{s["fn"]:>7.1f}{s["img"]:>6.0f}{s["draw"]:>7.1f}'
              f'{s["push"]:>7.1f}{s["join"]:>6.1f}{s["total"]:>7.1f}{s["frames"]:>5.0f}{s["miss"]:>5.0f}')
    s = summary['_save']
    print(f'progress saves: {s["n"]} x median {s["median"]} ms (inside "total", excluded from "fn")')
    if summary.get('_idle_save', {}).get('n'):
        s = summary['_idle_save']
        print(f'idle progress saves: {s["n"]} x median {s["median"]} ms (outside any turn)')


def cmd_run(args):
    port = find_port(args.port)
    os.makedirs(args.out, exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    name = f'{stamp}-{args.label}' if args.label else stamp
    log_path = os.path.join(args.out, name + '.log')
    dev = Device(port, log_path)
    meta = {'bench_version': BENCH_VERSION, 'date': stamp, 'label': args.label, 'book': args.book}
    if os.path.exists(args.local_book):
        meta['book_sha256'] = hashlib.sha256(open(args.local_book, 'rb').read()).hexdigest()
    try:
        for line in dev.lines(25):
            for key, rx in (('app_version', r'App version:\s+(\S+)'), ('compile_time', r'Compile time:\s+(.+)$'),
                            ('elf_sha256', r'ELF file SHA256:\s+(\S+)')):
                m = re.search(rx, line)
                if m:
                    meta[key] = m.group(1).strip()
            if 'LOCAL_USB_PROBE ready' in line:
                break
        else:
            die('no LOCAL_USB_PROBE ready line; flash a --pico-turn-timing build first')
        dev.drain(3.0)
        bench = Bench(dev, args.book, os.path.getsize(args.local_book) if os.path.exists(args.local_book) else None)
        _, original = bench.info()
        meta['original_settings'] = original
        meta['px'] = original['px']
        try:
            bench.run()
        finally:
            # 无论成败都恢复用户的翻页效果和低残影设置。/ Always restore the user's effect and low-ghost.
            try:
                bench.set_fx(original['fx'])
                bench.set_lg(original['lg'])
            except SystemExit:
                print(f'warning: could not restore fx={original["fx"]} lg={original["lg"]}', file=sys.stderr)
    finally:
        dev.close()
    summary = summarize(bench.turns)
    # 空闲时写入的进度不在任何一次翻页里，单独从日志里统计。/ Idle saves happen outside turns; count them from the log.
    idle = [float(m.group(1)) for m in re.finditer(r'turn: SAVE idle=([\d.]+)', open(log_path, errors='replace').read())]
    summary['_idle_save'] = {'n': len(idle), 'median': round(statistics.median(idle), 1) if idle else 0}
    result = {'meta': meta, 'summary': summary, 'warnings': bench.warnings, 'turns': bench.turns}
    json_path = os.path.join(args.out, name + '.json')
    json.dump(result, open(json_path, 'w'), ensure_ascii=False, indent=1)
    print(f'{meta.get("app_version")} elf={meta.get("elf_sha256", "?")[:12]} px={meta["px"]} turns={len(bench.turns)}')
    print_summary(summary)
    for w in bench.warnings:
        print('warning:', w)
    print(f'result {json_path}')


def cmd_compare(args):
    a, b = json.load(open(args.a)), json.load(open(args.b))
    for r, tag in ((a, 'A'), (b, 'B')):
        m = r['meta']
        print(f'{tag}: {m.get("date")} {m.get("label") or ""} {m.get("app_version")} elf={m.get("elf_sha256", "?")[:12]} px={m.get("px")}')
    if a['meta'].get('px') != b['meta'].get('px'):
        print('warning: font size differs; layout-dependent numbers are not comparable')
    print(f'{"scenario":16}{"total A":>9}{"B":>8}{"delta":>8}{"fn A":>8}{"B":>7}{"push A":>8}{"B":>7}{"hit A":>7}{"B":>6}')
    sa, sb = a['summary'], b['summary']
    for label in LABEL_ORDER:
        if label not in sa or label not in sb:
            continue
        x, y = sa[label], sb[label]
        print(f'{label:16}{x["total"]:>9.1f}{y["total"]:>8.1f}{y["total"] - x["total"]:>+8.1f}'
              f'{x["fn"]:>8.1f}{y["fn"]:>7.1f}{x["push"]:>8.1f}{y["push"]:>7.1f}'
              f'{x["hit_rate"]:>7.2f}{y["hit_rate"]:>6.2f}')
    print(f'saves: A {sa["_save"]} B {sb["_save"]}')


def main():
    ap = argparse.ArgumentParser(description='Pico page-turn benchmark')
    sub = ap.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('flash', help='write a --pico-turn-timing package to ota_0')
    f.add_argument('--package', default=DEFAULT_PACKAGE)
    r = sub.add_parser('restore', help='write the backed-up application back to ota_0')
    for p in (f, r):
        p.add_argument('--port')
        p.add_argument('--backup', default=DEFAULT_BACKUP)
    run = sub.add_parser('run', help='run the benchmark')
    run.add_argument('--port')
    run.add_argument('--book', default=DEFAULT_BOOK, help='bench EPUB path on the device')
    run.add_argument('--local-book', default=os.path.join(ROOT, 'build/bench/kiiko-bench.epub'),
                     help='locally generated copy, used to check the card holds the same version')
    run.add_argument('--out', default=DEFAULT_OUT)
    run.add_argument('--label', default='', help='short tag for the result file, e.g. a branch name')
    c = sub.add_parser('compare', help='compare two result JSON files (A = before, B = after)')
    c.add_argument('a')
    c.add_argument('b')
    args = ap.parse_args()
    {'flash': cmd_flash, 'restore': cmd_restore, 'run': cmd_run, 'compare': cmd_compare}[args.cmd](args)


if __name__ == '__main__':
    main()
