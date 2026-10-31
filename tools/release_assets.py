#!/usr/bin/env python3
"""Fetch immutable official Release assets with bounded sizes and SHA-256 checks.
中文：正式二进制来自 Release；站点镜像逐一验证，不写回源码目录。
English: Release binaries are verified into the site staging directory, never back into source.
"""
from pathlib import Path
from urllib.request import Request, urlopen
import hashlib
import json
import re
import shutil
import time

REPO = 'https://github.com/wegooo-cell/read-pico-reader/releases/download/'

def hydrate(source: Path, output: Path) -> None:
    catalog = source / 'release-assets.json'
    if not catalog.is_file():
        return
    release = json.loads(catalog.read_text())
    assert re.fullmatch(r'v\d+\.\d+\.\d+-rc\d+', release['tag'])
    assert release['tag'] == 'v' + json.loads((source / 'manifest.json').read_text())['version']
    _hydrate_files(source, output, release)
    # 中文：旧固定网址继续从原 Release 取字节，不能被当前版本别名覆盖。
    # English: Keep immutable older URLs backed by their original Release, not current aliases.
    seen = set(release['files'])
    for historical in release.get('historical', []):
        assert re.fullmatch(r'v\d+\.\d+\.\d+-rc\d+', historical['tag'])
        expected = 'Pico-update-' + historical['tag'][1:] + '.bin'
        assert set(historical['files']) == {expected} and expected not in seen
        seen.add(expected)
        _hydrate_files(source, output, historical)

def _hydrate_files(source: Path, output: Path, release: dict, maximum: int = 0x400000) -> None:
    fetched = {}
    for name, info in release['files'].items():
        assert re.fullmatch(r'[A-Za-z0-9_.-]+\.bin', name)
        asset = info['asset']
        assert re.fullmatch(r'[A-Za-z0-9_.-]+\.bin', asset)
        assert re.fullmatch(r'[a-f0-9]{64}', info['sha256']) and 0 < info['size'] <= maximum
        target = output / name
        local = source / name
        if local.is_file() and local.stat().st_size == info['size'] and hashlib.sha256(local.read_bytes()).hexdigest() == info['sha256']:
            shutil.copy2(local, target)
            fetched[asset] = target
            continue
        if asset in fetched:
            cached = fetched[asset]
            assert cached.stat().st_size == info['size'] and hashlib.sha256(cached.read_bytes()).hexdigest() == info['sha256'], 'Release alias checksum mismatch'
            shutil.copy2(cached, target)
            continue
        url = REPO + release['tag'] + '/' + asset
        for attempt in range(3):
            try:
                # Size cap also applies to a malicious/unexpected upstream response.
                # 大小上限也约束非预期响应，避免无限读入内存。
                with urlopen(Request(url, headers={'User-Agent':'kiikoread-publisher'}), timeout=30) as response:
                    data = response.read(info['size'] + 1)
                assert len(data) == info['size'] and hashlib.sha256(data).hexdigest() == info['sha256'], 'Release asset checksum mismatch'
                target.write_bytes(data)
                fetched[asset] = target
                break
            except Exception:
                if attempt == 2: raise
                time.sleep(5)
