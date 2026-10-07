# Pico Reader

[简体中文](README.zh-CN.md) · [日本語](README.ja-JP.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

An independent, open-source reading firmware for the **MindReset Read Pico (RDP-G01-W)** ESP32-S3 e-paper board. It is based on [MindReset's official demo firmware](https://github.com/MindReset/read_pico_firmware), but is **not an official MindReset release**.

The current interface has Home, Bookshelf, Files, and Settings. It reads EPUB and TXT books from a TF card, remembers reading progress, and supports Wi-Fi, hotspot, or USB file transfer. The online flasher installs the same firmware image as the local `flash/` bundle.

## Current release: rc85

Improved text-only page turns in normal and full-screen reading, EPUB image compatibility, named-device Bluetooth scanning and Bluetooth pager compatibility. Choose **Reading settings → Page-turn effect → Default effect** to try the text-turn optimization. Water turns and periodic/manual cleanup remain available. See the [changelog](docs/CHANGELOG.md).

## Online installation

Visit the [HTTPS web flasher](https://wegooo-cell.github.io/read-pico-reader/). Use a desktop Chrome or Edge browser with a USB data cable. Select the Read Pico serial device and follow the prompts. **Check the board model before flashing.** Ordinary installation preserves the device's settings and reading records, as well as TF-card contents. It does not include books or sample reading history.

If automatic entry to download mode fails on a device already running this firmware, open **Settings → Upgrade & restore → BOOT flashing** on the device, wait for the computer to detect its serial port again, then retry the web flasher.

The release manifest is [`flash/manifest.json`](flash/manifest.json); it flashes the bootloader, partition table, application, and the OTA data partition. Existing NVS settings and the internal book partition are preserved. The site is published from an explicit allowlist in [`.github/workflows/pages.yml`](.github/workflows/pages.yml), so local books, backups, and extra font packages are not uploaded. See the [release changelog](docs/CHANGELOG.md).

## Online updates

Open **Settings → Upgrade & restore → System update → Check for updates** after connecting Wi-Fi. From rc84 onward, the update offer displays short release notes with Later and Start update buttons. Older versions can read the notes on the web flasher before updating to rc84.

## Local TF-card updates

Local updates require one complete computer installation of an OTA base build so the bootloader and dual-slot partition table are present. That migration keeps the existing settings, reading records, internal-storage addresses, and TF-card contents. Later, copy the application image to the TF-card root as `Pico-update.bin`, then open **Settings → Upgrade & restore → System update → TF-card update**.

Pico checks the image project, version, size, and headers before writing the inactive firmware slot. It selects the new slot only after full image validation. If the new image resets before its first hardware and UI startup check succeeds, the bootloader returns to the previous slot. Keep power connected and the TF card inserted during installation. An application-only update cannot replace the initial OTA base installation and cannot change the bootloader or partition table.

## Build from source

Use ESP-IDF **v6.1** for ESP32-S3:

```sh
idf.py set-target esp32s3
idf.py build
```

The board-specific flash and PSRAM timing is in `sdkconfig.defaults`. `sdkconfig.ci` is for compile checks only. Follow the [MindReset hardware documentation](https://dot.mindreset.tech/docs/read_0) for the board. A prebuilt firmware image is provided for the RDP-G01-W only.

## Books and fonts

On first mount, the firmware creates `books`, `fonts`, and `pictures` folders on the TF card if absent. No books are preloaded. The firmware embeds a subset of **Noto Sans SC Medium** for the system UI and distributes no additional font package. Users may place their own compatible fonts in `fonts` for reading. The embedded subset remains under the [SIL Open Font License](main/assets/OFL-Noto.txt).

The “Covers and spines” bookshelf mode is labeled **experimental, not a formal release**. Other shelf styles remain available.

TXT/EPUB books opened from any TF-card directory appear on the shelf after progress is saved, including on cached returns without a rescan. Bookshelf management → Remove from shelf hides selected books while keeping their files, progress, and favorites. Reading a removed book puts it back. Removal state persists and is included in configuration backups.

TXT books and EPUB books without a valid embedded cover receive a deterministic grayscale cover shared by the shelf, home, and ticket lock screen. A valid EPUB cover takes priority. Generated covers are cached under `.readpico/covers` on the TF card and rebuilt when the file or title changes.

In Reading settings → Typography, first-line indent can be set to 0, 1, 2, or 3 characters (2 by default). Body text is centered by whole-character columns to balance the side margins, and common Chinese punctuation is kept away from prohibited line starts and ends.

Horizontal swipes turn reading pages in either tap-area mode. With vertical tap areas selected, vertical swipes also turn pages. Continuous punctuation groups stay together at line boundaries. Small JPG/PNG illustrations may share a page with surrounding text; large illustrations and image-only chapters keep their own pages.

**Settings → Reading & device → Automatic lock** offers 1, 5, or 10 minutes of inactivity, or Off (default). Touch and button input restart the timer. Transfers and upgrades pause it. Automatic locking saves the current reading state and uses the existing light-sleep/deep-sleep lock behavior. This choice is included in settings backups.

TF-card JPG/PNG files can be up to **50 MiB**, with source dimensions up to 8,192 pixels per side. They are read as streams rather than loaded entirely into memory; decoded output stays within one screen. Progressive JPEG uses the existing reduced-resolution DC decoder, and interlaced PNG remains unsupported. These TF-file limits do not change the EPUB image-resource limit below.

To back up personal settings, open **Settings → Save & restore → Save to TF card**. The device writes `Pico-settings.backup` to the TF-card root. Put that file back at the root and choose **Restore from TF card** to recover fonts, typography, display and lock settings, profile and status signature, saved Wi-Fi name and password, book progress, reading time, bookmarks, favorites, custom book names, and shelf removals. Book, font, avatar, and wallpaper files remain on the TF card. The backup contains the Wi-Fi password in plaintext, so keep the TF card private. Missing external fonts and images fall back to built-in options. Older backups remain readable and leave the current network configuration unchanged.

EPUB metadata is allocated for the actual book size. ZIP entries and chapters each have an 8,192-item limit; covers, images, and navigation also consume ZIP entries. A book may exceed 32 MB overall, while each XHTML resource remains limited to 4 MB and each decompressed image to 8 MB; available device memory and standard ZIP limits also apply.

EPUB body chapters begin on a new page, keeping the title with the opening text when it fits. This also applies to recognized chapter headings within one XHTML resource. TOC links, introductory information and copyright metadata are excluded from body chapter detection; ordinary subheadings continue on the current page. Detection uses authored navigation, standalone numbered headings and body structure; books without reliable markers may still need individual compatibility fixes.

## Licenses and credit

The fork retains the upstream **Apache-2.0** license and notices. The UI icon set comes from **Lucide** under its **ISC** notice. The modified epdiy driver uses **LGPL-3.0-or-later**, and pypinyin dictionary data uses **MIT**. See [Third-party notices](THIRD_PARTY_NOTICES.md) and component directories for the exact scope; a component license does not change the license of the entire firmware.

Please report firmware bugs in this repository, not in the MindReset upstream issue tracker. Hardware purchasing and repair remain matters for the [official support channels](https://dot.mindreset.tech/docs/contact).

## Local rc76 transfer build

File management groups its launchers as WiFi/hotspot, USB and WeRead transfer. WeRead uses saved internet WiFi for QR login, shelf sync and completed EPUB downloads into the SD books directory, with optional inline illustrations. The UI cancels and joins ongoing work before leaving, locking or losing SD media. Cloud progress is never uploaded. CrossMux and FreeInk SDK MIT notices accompany the native port. This is a local validation build; real account login/download and the reported hotspot reset still require device verification.

### 微读书架多选下载

在微信书架点「多选」，可跨页选择书籍或「全选本页」，再点「下载 N 本」进入批量下载页。每次只下载一本；取消、离页或锁屏会停止队列，已完成书籍保留。封面始终下载，正文插图可选；封面获取失败会提示重试。文件名使用微信书架的书名，仅替换文件系统禁用字符；每批最多选择 1024 本。单本详情返回微信书架，微信书架返回文件管理。

The reader Font Settings sheet holds the body size and the body weight; the weight steps are labelled Regular, Medium and Bold (400, 500 and 700), with Regular as the default. wght-axis fonts use their real variation axis, while static fonts, including the embedded subset, approximate the face with coverage morphology, so the untouched look is unchanged. Weight applies to the body only and never changes advances or pagination.

Shake-to-turn is off by default and enabled in reader Font Settings. A horizontal left/right impulse turns to the previous/next page. Slow tilts, other axes, touch and rebounds are filtered; wait through an 800 ms cooldown and rest before the next gesture. Direction and sensitivity still need device validation. The global refresh test option is removed and ignored in older configurations.
