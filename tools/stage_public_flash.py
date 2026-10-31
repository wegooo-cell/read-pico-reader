#!/usr/bin/env python3
"""Stage only the files intended for the public HTTPS flashing site."""

from __future__ import annotations

from html.parser import HTMLParser
from pathlib import Path
import shutil
import json
import re
import sys
from urllib.parse import urlparse

from verify_flash_bundle import check, check_archived_releases
from release_assets import hydrate
from metalio_release_assets import stage_metalio


ROOT = Path(__file__).resolve().parents[1]
FLASH = ROOT / "flash"


class LocalLinks(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, _tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for key, value in attrs:
            if key in {"href", "src", "manifest"} and value:
                self.links.append(value)


def stage(output: Path, ota_origin: str | None = None) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name in ("index.html", "notices.html", "manual.html", "site.css", "site.js", "installer-bridge.js", "flasher-integrity.js", "metalio-installer.js", "metalio-flash-core.js", "metalio-connection.js", "metalio-flash-reader.js", "metalio-transport.js", "metalio-native-stub.js", "_headers",
                 "manifest.json", "toc-preview.png", "firmware.bin",
                 "bootloader.bin", "partitions.bin", "ota_data_initial.bin", "Pico-update.bin", "update.json"):
        if (FLASH / name).is_file(): shutil.copy2(FLASH / name, output / name)
    # 官网仅发布展示资源与用户说明；内部构建及调试记录不进入网站。
    # Publish website assets and the user manual, excluding private build and debug files.
    shutil.copytree(FLASH / "assets", output / "assets", dirs_exist_ok=True)
    # 中文：只复制仓库中的公开说明书（正文、检索索引、图解、PDF）。
    # English: Copy the curated public manual, never its local rendering/debug workspace.
    shutil.copytree(FLASH / "manual", output / "manual", dirs_exist_ok=True)
    if (FLASH / "new-features").is_dir():
        shutil.copytree(FLASH / "new-features", output / "new-features", dirs_exist_ok=True)
    # 中文：只发布构建好的模拟器；宿主源码、个人书库和工具链留在站点外。
    # English: Publish built simulator assets only, excluding sources, personal files and toolchains.
    if (FLASH / "simulator").is_dir():
        shutil.copytree(FLASH / "simulator", output / "simulator", dirs_exist_ok=True)
    version = json.loads((FLASH / "manifest.json").read_text())["version"]
    versioned = FLASH / f"Pico-update-{version}.bin"
    if versioned.is_file(): shutil.copy2(versioned, output / versioned.name)
    hydrate(FLASH, output)
    stage_metalio(FLASH, output)
    # 保留已发布版本的固定下载地址，避免旧清单缓存或正在进行的更新突然遇到 404。
    # Keep immutable published URLs so cached manifests and in-flight updates do not suddenly get a 404.
    for upgrade in FLASH.glob("Pico-update-*.bin"):
        name = re.fullmatch(r"Pico-update-(\d+\.\d+\.\d+(?:-rc\d+)?)\.bin", upgrade.name)
        if not name: continue
        with upgrade.open("rb") as source:
            header = source.read(112)
        assert header[48:80].split(b"\0")[0].decode("ascii") == name[1], "historical upgrade version mismatch"
        assert header[80:112].split(b"\0")[0] == b"Read_Pico", "historical upgrade project mismatch"
        shutil.copy2(upgrade, output / upgrade.name)
    # 按验证后的正式目录发布历史清单与基础包，不递归上传本地测试文件。
    # Publish only verified archived manifests/base parts, excluding local test files.
    # 最新基础包来自 Release；兼容性检查使用已经校验过的站点镜像。
    # The latest base comes from Release; compare archives with the verified staged image.
    archives = check_archived_releases(FLASH, output / "partitions.bin", output)
    previous = json.loads((FLASH / 'manifest.json').read_text()).get('previous_version')
    if previous:
        assert any(item['version'] == previous for item in archives), 'previous official release is missing'
    if (FLASH / "releases.json").is_file():
        shutil.copy2(FLASH / "releases.json", output / "releases.json")
        shutil.copy2(FLASH / "versions.js", output / "versions.js")
        for release in archives:
            destination = output / Path(release["manifest"]).parent
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(FLASH / release["manifest"], destination / "manifest.json")
            for name in ("bootloader.bin", "partitions.bin", "ota_data_initial.bin"):
                shutil.copy2(FLASH / Path(release["manifest"]).parent / name, destination / name)
    shutil.copytree(FLASH / "vendor/web", output / "vendor/web", dirs_exist_ok=True)
    shutil.copy2(FLASH / "vendor/LICENSE", output / "vendor/LICENSE")
    shutil.copytree(FLASH / "vendor/metalio", output / "vendor/metalio", dirs_exist_ok=True)
    licenses = {
        ROOT / "licenses/Apache-2.0.txt": "Apache-2.0.txt",
        ROOT / "licenses/kiikoread-Personal-Study-1.0.txt": "kiikoread-Personal-Study-1.0.txt",
        ROOT / "licenses/LUCIDE-ISC.txt": "LUCIDE-ISC.txt",
        ROOT / "components/jpegdec/LICENSE": "JPEGDEC-APACHE-2.0.txt",
        ROOT / "licenses/LGPL-3.0.txt": "LGPL-3.0.txt",
        ROOT / "licenses/GPL-3.0.txt": "GPL-3.0.txt",
        ROOT / "components/read_pico_search/LICENSE.pypinyin": "PYPINYIN-MIT.txt",
        ROOT / "main/assets/OFL-Noto.txt": "OFL-Noto.txt",
        ROOT / "licenses/CrossMux-MIT.txt": "CROSSMUX-MIT.txt",
        ROOT / "licenses/AnimateIcons-MIT.txt": "AnimateIcons-MIT.txt",
        ROOT / "licenses/FreeInk-SDK-MIT.txt": "FREEINK-SDK-MIT.txt",
        ROOT / "licenses/Metalio-MIT.txt": "METALIO-MIT.txt",
        ROOT / "licenses/WebP-BSD-3-Clause.txt": "WebP-BSD-3-Clause.txt",
        ROOT / "licenses/lowzag-MIT.txt": "lowzag-MIT.txt",
    }
    (output / "licenses").mkdir(exist_ok=True)
    for source, name in licenses.items():
        shutil.copy2(source, output / "licenses" / name)
    shutil.copy2(ROOT / "LICENSE", output / "LICENSE")
    (output / ".nojekyll").touch()

    for page in ("index.html", "notices.html", "manual.html", "manual/index.html", "metalio/index.html"):
        links = LocalLinks()
        links.feed((output / page).read_text(encoding="utf-8"))
        for link in links.links:
            parsed = urlparse(link)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            target = (output / Path(page).parent / parsed.path).resolve()
            if target.is_dir(): target = target / 'index.html'
            assert target.is_relative_to(output.resolve()) and target.is_file(), (
                f"broken local link in {page}: {link}")
    feed = json.loads((output / 'update.json').read_text())
    # 用户指定GitHub Pages为默认OTA源；只接受固件已允许的两个HTTPS来源。
    # The user selected GitHub Pages for OTA; retain the firmware's exact HTTPS allowlist.
    origin = ota_origin or feed['url'].rsplit('/', 1)[0]
    assert origin in ('https://kiikoread.com', 'https://wegooo-cell.github.io/read-pico-reader')
    feed['url'] = origin + '/' + f'Pico-update-{version}.bin'
    (output / 'update.json').write_text(json.dumps(feed, ensure_ascii=False, indent=2) + '\n')
    check(output)
    files = list(output.rglob("*"))
    count = sum(path.is_file() for path in files)
    size = sum(path.stat().st_size for path in files if path.is_file())
    print(f"Public site: {count} files, {size} bytes")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('output', nargs='?', type=Path, default=ROOT / '_site')
    parser.add_argument('--ota-origin', choices=['https://kiikoread.com', 'https://wegooo-cell.github.io/read-pico-reader'])
    args = parser.parse_args()
    stage(args.output, args.ota_origin)
