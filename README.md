# kiikoread

> **Licensing:** New original material explicitly marked with the [Personal Study License](licenses/kiikoread-Personal-Study-1.0.txt) permits personal study and prohibits commercial use without separate written permission. **This policy does not relicense rc93 or earlier code/binaries, remove existing Apache-2.0 rights, or override third-party licenses.** See [LICENSE](LICENSE) for the exact scope.

The public project name is **kiikoread**. The website calls the device an ink-screen reader; the GitHub URL and legacy update/backup filenames remain compatible. Firmware UI branding also uses kiikoread. The bright-white website includes screen previews, features, version selection, web USB flashing, Bin downloads and an [online user manual](flash/manual.html). Stage it with `python3 tools/stage_public_flash.py _site`; both GitHub Pages and Cloudflare Pages can serve `_site`. The PDF can be regenerated from `docs/user-manual.zh-CN.json` with `tools/build_user_manual.py --font /path/to/a/static-CJK.ttf` (Python ReportLab required).
Official firmware binaries are stored in [GitHub Releases](https://github.com/wegooo-cell/read-pico-reader/releases). The verified site staging step retrieves the matching assets; do not publish the raw flash directory. Device OTA checks `https://kiikoread.com/update.json` first, with the existing GitHub Pages feed as a fallback. Stage the primary site with `python3 tools/stage_public_flash.py _site --ota-origin https://kiikoread.com`; keep the default origin for GitHub Pages.


[简体中文](README.zh-CN.md) · [日本語](README.ja-JP.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

An independent reading firmware with publicly available source for the **MindReset RDP-G01-W** ESP32-S3 e-paper board. It is based on [MindReset's official demo firmware](https://github.com/MindReset/read_pico_firmware), but is **not an official MindReset release**. Licenses apply by scope as described above.

The current interface has Home, Bookshelf, Files, and Settings. It reads EPUB and TXT books from a TF card, remembers reading progress, and supports Wi-Fi, hotspot, or USB file transfer. The online flasher installs the same firmware image as the local `flash/` bundle.

## Current release: rc99

Restore system text antialiasing, improve charging display, add one/two-press wake and icon-set lock/cancel artwork, trim reading guides to text spans and clean reader entry. Based on 51e7d8d; keep the original rc98 for rollback. See the [changelog](docs/CHANGELOG.md).

### Settings, storage and lock fixes

Settings paints only visible rows and reuses a small prepared avatar. Entry arrows are drawn icons, so imported fonts without the chevron character cannot hide them. Storage uses the mounted partition's usable capacity and current allocation count; stale FAT32 free-space hints are recounted once on mount. A failed count displays an unavailable message while leaving the card mounted. Reading updates the visible clock locally each minute, without refreshing the body or a hidden immersive status bar.

Wallpaper locks save their final 16-level dithered frame in `.readpico/locks/wallpaper.bin`. The first use prepares it; later locks directly read the validated frame. Changing the source, orientation or algorithm invalidates it; damaged caches regenerate, and write failures preserve normal locking. No extra full-frame memory is retained. Shelf cover pixels remain pending until successfully displayed, including covers prepared during an event and failed display retries.


Reading Font settings include an antialiasing switch (on by default). It changes edge coverage without changing layout or retaining a second glyph cache. Swipe upward from the bottom 120 pixels to open Tools in either tap-turn mode. Settings labels and values stay on one line at 100–200%; long values are truncated. Reader Tools and their secondary panels follow system UI size, with upward expansion, bounded native glyphs and shared paint/hit rectangles; the book body keeps its reading size. Cached shelf covers are painted together on entry in every refresh mode. Cold caches are prepared in bounded work slices, then reveal the whole page together; a failed cover keeps its placeholder. Existing cache format ownership and navigation interruption remain.

## Main refresh and reader controls

Settings → Display → Main refresh mode offers ordinary, fast and water, defaulting to ordinary. Ordinary/water retain grays; fast uses monochrome covers and fine checkerboard acrylic. Four main screens retain navigation icons/labels and locally move the selection marker. Navigation feedback preserves its gray background in all modes.

The main-screen ripple matches the reader's compact launch timing: eight bands, 44 ticks and an 11,090 µs target, retaining all 37 phases per pixel. Its software target is about 488ms instead of the previous 624ms; actual latency also includes rendering and scanning. Main gray sequences, fixed navigation and failure recovery remain intact. Optical quality and ghosting require device testing.

List covers bounce gently and settle before opening. Only the cover movement region updates: fast mode uses monochrome differentials, ordinary/water retain grays, without an extra feedback frame.

Swipe down from the top edge to open the WiFi, Bluetooth, refresh and lock controls; swipe up, tap outside or press Back to close. WiFi connects the last saved network or disconnects it, unless a transfer is busy. Reader key controls offer custom actions and two presets. The default is Previous / Tools / Next; the second is Home / Full screen / Tools. A middle hold defaults to Home and can also be mapped to refresh or other actions. Swiping up from the bottom edge always opens Tools, independently of turn or key settings. All three short presses and the middle hold may use other actions; startup and backup restoration preserve those choices. Centre taps only enter fullscreen; bottom-edge swipes return to standard mode and show tools. Side taps turn pages, retaining the vertical-turn regions. New books open standard with tools visible; page turns preserve the selected mode. These choices persist across reboot and configuration restore.

The firmware checkpoints the book, position and full-screen state before lock, consuming deep-wake resume only once. Interrupted opening routes to an operable recovery page with the book name, Home and confirmed deletion, without automatic retries. Tickets always use the system face and restore the reader face on light wake. Quick controls retain grayscale borders in all main modes. Fast mode retains only BW covers; ordinary/water retain only gray covers and free obsolete formats on a policy change. Home resume titles reuse the active system face at 40px; reader book/chapter titles reuse the reader face at 32/48px. Outlines rasterize directly without additional built-in fonts or simultaneously loading system and reader faces; unavailable faces retain the safe built-in missing-glyph fallback. These changes are included in rc88.


### Local reading and low-power update

Reading settings uses the approved compact two-column sheet: tap Auto full refresh to cycle 5/10/15/30 pages or no forced refresh, and tap Turn effect to switch ordinary/water. Turn mode and Key controls open their own pages; power-key page turning, immersive display and hiding illustrations remain available. Manual refresh remains a key action. Reading ripple uses 8 bands with an 11,090 µs target tick; all 37 gray phases complete, while main-screen ripple retains its independent settings.

The local build ports [official commit a2a63a6](https://github.com/MindReset/read_pico_firmware/commit/a2a63a6a2c606540be72156a6567392d948d65fc): park touch, accelerometer, buzzer and the IO expander, pull floating panel/TF-clock inputs down in light sleep, and prepare CPU retention. CW_INT remains an input during light sleep for key wake and is driven low only for host power-down. Preserve the ten-minute lock, PIN and reading checkpoint; a failed PMU handoff restores peripherals instead of rebooting or waiting forever. Actual current draw requires hardware measurement.

## Keyboard

Rename, profile name, status signature, shelf search and WiFi password fields share one keyboard. T9 inputs Chinese Pinyin only; QWERTY supports Chinese/English and case switching. Digits are entered from the numeric panel only. Continuous Pinyin uses 487 offline common phrases, pageable homophones and continuations; select a candidate before saving. Tap text or caret arrows to insert and delete in the middle. Holding backspace for half a second repeats deletion, removing pending Pinyin before text at the caret; release or move away to stop. Ordinary input/deletion refreshes changed regions only. After six candidate-strip changes, a 700 ms touch-free pause triggers one local grayscale settle; typing, held deletion and layout switches defer or cancel it. Keys darken/inset on press and restore on release. T9 More cycles through readings; homophones remain available across batches. The embedded black face covers all 6,763 GB2312 Han characters without a TF font package. WiFi passwords use ASCII QWERTY, digits and symbols. Both AP and STA web transfer pages can save or clear the persistent status signature (up to 95 UTF-8 bytes). These features are included in the rc86 release.

## Online installation

Select the latest release or the original **rc86 / rc85** build above the connection button. The publication time and TF-image download follow the selection. Ordinary installation keeps the preserve-data default for all versions.

Visit the [HTTPS web flasher](https://wegooo-cell.github.io/read-pico-reader/). Use a desktop Chrome or Edge browser with a USB data cable. Select the ink-screen reader’s serial device and follow the prompts. **Check the board model before flashing.** Ordinary installation preserves the device's settings and reading records, as well as TF-card contents. It does not include books or sample reading history.

If automatic entry to download mode fails on a device already running this firmware, open **Settings → Upgrade & restore → BOOT flashing** on the device, wait for the computer to detect its serial port again, then retry the web flasher.

The release manifest is [`flash/manifest.json`](flash/manifest.json); it flashes the bootloader, partition table, application, and the OTA data partition. Existing NVS settings and the internal book partition are preserved. The site is published from an explicit allowlist in [`.github/workflows/pages.yml`](.github/workflows/pages.yml), so local books, backups, and extra font packages are not uploaded. See the [release changelog](docs/CHANGELOG.md).

## Online updates

Open **Settings → Upgrade & restore → System update → Check for updates** after connecting Wi-Fi. From rc84 onward, the update offer displays short release notes with Later and Start update buttons. Older versions can read the notes on the web flasher before updating to rc84.

The local fix releases off-screen cover caches before online updates and reports insufficient memory safely. Display scans and incremental flash writes are serialized. After at least five seconds and five percentage points of growth, progress appends only newly completed black pixels and updates the percentage, without erasing completed segments or triggering periodic page wipes. Exiting, locking and cancellation still join the worker; failed validation keeps the current boot slot.

## Local TF-card updates

Local updates require one complete computer installation of an OTA base build so the bootloader and dual-slot partition table are present. That migration keeps the existing settings, reading records, internal-storage addresses, and TF-card contents. Later, copy the application image to the TF-card root as `Pico-update.bin`, then open **Settings → Upgrade & restore → System update → TF-card update**.

kiikoread checks the image project, version, size, and headers before writing the inactive firmware slot. It selects the new slot only after full image validation. If the new image resets before its first hardware and UI startup check succeeds, the bootloader returns to the previous slot. Keep power connected and the TF card inserted during installation. An application-only update cannot replace the initial OTA base installation and cannot change the bootloader or partition table.

## Build from source

The repository keeps one shared `main/` and `components/` source tree; `boards/<board>/` supplies the hardware backend, sdkconfig, partition table and dependency lock. Select the device with `--board pico|metalio47` and a matching ESP-IDF **v6.1** SDK:

```sh
python3 tools/build_firmware.py --board pico --python /path/to/idf-python/bin/python
python3 tools/build_firmware.py --board metalio47 --python /path/to/idf-python/bin/python
```

Pico is ESP32-S3 (Xtensa), Metalio is ESP32-S31 (RISC-V); each needs its own SDK/toolchain, config and output directory, and the two images are never interchangeable. On Windows the wrapper is `tools\build_board.ps1 -Board pico|metalio47`. See [docs/BOARDS.md](docs/BOARDS.md) for the full build matrix, partition tables and flash offsets. A prebuilt firmware image is provided for the RDP-G01-W only.

## Books and fonts

TF storage currently supports FAT16/FAT32; exFAT is not enabled. Capacity alone does not establish compatibility. The local development build retries 40 MHz after a 250 ms settle, then falls back to 20/10 MHz, including busy high-speed negotiation. Failed card-detect communication preserves the last snapshot instead of pretending the card was removed; an unreadable detect signal permits a bounded actual mount attempt. Initialization failures never automatically suggest or perform formatting. Real removal still invalidates open-file state; recovery requires closing consumers and explicit remounting or restarting the device.

On first mount, the firmware creates `books`, `fonts`, and `pictures` folders on the TF card if absent. No books are preloaded. The firmware embeds a subset of **Noto Sans SC Medium** for the system UI and distributes no additional font package. Users may place their own compatible fonts in `fonts` for reading. The embedded subset remains under the [SIL Open Font License](main/assets/OFL-Noto.txt).

Shelf styles retain “Dark rail” and “Acrylic shelf”, with nine books per page. Saved or restored pocket/spine choices fall back to acrylic.

The local development build adds **Settings → Lock style → Library collage**. Real covers keep their source aspect ratios, rounded corners and a shared 15° tilt; recent books occupy the middle. The profile name appears at the upper left, with a large book count and smaller unit at the lower right. Text is rasterized from the selected system font, never the independent reading face; light-sleep wake restores the previous font context. The collage always uses 16 grays and stable dithering, regardless of the main refresh mode. SD caches under `.readpico/locks` rebuild when the profile, font or library changes. First-time cover preparation takes longer; low memory uses a safe lock fallback.

Pale collage covers receive a fine rounded gray outline to keep their edges visible on white. **Settings → Personalization → Lock password** adds an optional four-digit PIN, off by default. Enabling requires two matching entries; changing or disabling requires the current PIN. Wake and cold boot require verification before returning to the saved page. Pressed circles preview an input; only a valid release commits it. Cancel returns to the lock face, and Delete removes one digit. The actual lock background uses a lighter blur cached once. Wake and cold boot present the complete pad once, with a rounded lock icon and crisp controls; typing does not trigger backdrop updates. Key feedback updates locally. Text uses the selected system face, with a built-in fallback if it is unavailable. Only a salted digest is stored; ordinary configuration backups neither contain nor replace this credential.

Automatic sleep and manual lock both use the selected lock style. PIN backgrounds are reused only for the matching style and wallpaper identity; changing styles or the wallpaper causes a redraw on cold/deep wake, without changing PIN protection. To select wallpaper mode, confirm an image in the wallpaper preview.

TXT/EPUB books opened from any TF-card directory appear on the shelf after progress is saved, including on cached returns without a rescan. Bookshelf management → Remove from shelf hides selected books while keeping their files, progress, and favorites. Reading a removed book puts it back. Removal state persists and is included in configuration backups.

Bookshelf management follows the file browser: search, a book count, **Shelf order**, Select, and list/grid switching. Tap a book to read; hold a book or tap Select to reveal circular selection marks and Delete books / Clear progress / Remove from shelf / Back. Back leaves selection; paging, sorting and layout changes retain selected book identities. The pager jumps to the first/previous/next/last page. Lists show five books per page and grids six; destructive actions retain confirmation.

TXT books and EPUB books without a valid embedded cover receive a deterministic grayscale cover shared by the shelf, home, and ticket lock screen. A valid EPUB cover takes priority. Generated covers are cached under `.readpico/covers` on the TF card and rebuilt when the file or title changes.

In Reading settings → Typography, first-line indent can be set to 0, 1, 2, or 3 characters (2 by default). Fine adjustment moves body first lines by 1px within -20..+20px (default 0); tap the value to reset. No-indent ignores the offset; restart and backups retain it. Margin slider stops follow distinct visible widths at the current font size and tracking, preventing duplicate stops that appear unresponsive; existing saved margins are retained until changed. Body text is centered by whole-character columns to balance the side margins, and common Chinese punctuation is kept away from prohibited line starts and ends.

Font settings pairs the reading font with weight, and the embedded-font switch with typography controls; the reading-background entry has been removed. Settings now groups Connection & time (WiFi, Bluetooth, Date & time), Display, Personalization and Upgrade & restore. Display places Main refresh mode immediately below System contrast and contains automatic lock and shutdown sleep. Personalization contains shelf style, lock style, lock password and the status signature. System font includes a preview and a tap-to-cycle size from 100% to 200% in 10% steps; larger sizes retain labels and values on one line. Selecting a system font asks whether to also use it for reading. Only system changes no reading font; Sync reading font saves the same path for reading without loading a second face during this page. Failed saves remain retryable.

Shelf covers are processed once and saved in the TF card’s hidden `.readpico/covers` directory. Ordinary/water reuse gray thumbnails; fast also saves the finished monochrome raster, avoiding repeat decoding, scaling and dithering on subsequent loads. Source identity, dimensions, contrast and dither phase validate each raster; damaged or stale caches regenerate safely. The first uncached visit still needs generation. When collage locking is selected, an idle shelf warms one bounded cover at a time in `.readpico/locks`; locks reuse aspect-preserved covers and a validated complete frame without reducing picture quality. Home resume uses the same gray-thumbnail cache and a separate 5,500-byte finished monochrome file, avoiding conflicts with shelf dimensions; modes never retain both gray and monochrome buffers. Cached images are disposable and are not book files. Write failures do not prevent reading.

Horizontal swipes turn reading pages in either tap-area mode. With vertical tap areas selected, vertical swipes also turn pages. Continuous punctuation groups stay together at line boundaries. Small JPG/PNG illustrations may share a page with surrounding text; large illustrations and image-only chapters keep their own pages.

**Settings → Reading & device → Automatic lock** offers 1, 5, or 10 minutes of inactivity, or Off (default). Touch and button input restart the timer. Transfers and upgrades pause it. Automatic locking saves the current reading state and uses the existing light-sleep/deep-sleep lock behavior. This choice is included in settings backups.

TF-card JPG/PNG files can be up to **50 MiB**, with source dimensions up to 8,192 pixels per side. They are read as streams rather than loaded entirely into memory; decoded output stays within one screen. Progressive JPEG uses the existing reduced-resolution DC decoder, and interlaced PNG remains unsupported. These TF-file limits do not change the EPUB image-resource limit below.

To back up personal settings, open **Settings → Save & restore → Save to TF card**. The device writes `Pico-settings.backup` to the TF-card root. Put that file back at the root and choose **Restore from TF card** to recover fonts, typography, display and lock settings, profile and status signature, saved Wi-Fi name and password, book progress, reading time, bookmarks, favorites, custom book names, and shelf removals. Book, font, avatar, and wallpaper files remain on the TF card. The backup contains the Wi-Fi password in plaintext, so keep the TF card private. Missing external fonts and images fall back to built-in options. Older backups remain readable and leave the current network configuration unchanged.

EPUB metadata is allocated for the actual book size. ZIP entries and chapters each have an 8,192-item limit; covers, images, and navigation also consume ZIP entries. A book may exceed 32 MB overall, while each XHTML resource remains limited to 4 MB and each decompressed image to 8 MB; available device memory and standard ZIP limits also apply.

Pico ordinary and illustrated turns refresh only the reading content with the existing GL16 waveform. Status and title stay outside the turn scan; chapter/progress updates use a local footer refresh after 800ms idle, matching ripple and Fast BW. Entry, manual and periodic cleanup remain available.

EPUB GIF illustrations display their first frame on white; JPEGDEC supplies a reduced-size fallback when ROM JPEG decoding fails. Supported same-chapter footnote image markers open a nearby card; tap again to close it without changing the reading position.

EPUB body chapters begin on a new page, keeping the title with the opening text when it fits. This also applies to recognized chapter headings within one XHTML resource. TOC links, introductory information and copyright metadata are excluded from body chapter detection; ordinary subheadings continue on the current page. Detection uses authored navigation, standalone numbered headings and body structure; books without reliable markers may still need individual compatibility fixes.

## Boot diagnostics and panel repair

The current source integrates PR #32–#34; release attachments remain tied to their published versions. Boot-phase timing is recorded in the existing RTC trail and shown in Overview's last-run area. Boot marks stop after the first frame. Vertical reading guides sit beside the glyph cells with a shared dash phase, chapter numbers align with ideographs, and dash-like symbols follow vertical rotation.

For persistent black or white stripes after a power cut during a refresh, tap **Settings → Upgrade and recovery → Panel repair**. It performs several full-screen black/white drives and restores the current page including navigation. It runs only on an explicit tap and preserves normal reading rules, waveform tables and VCOM calibration. A failed final output reports failure and allows retry.

## Licenses and credit

The [license policy](LICENSE) introduces a **personal-study, noncommercial license** only for explicitly marked new original material first published after its adoption. The custom license is source-available, not an OSI-approved open-source license. Existing Apache-2.0 grants, historical releases and contributors' rights remain intact; the [original Apache-2.0 text](licenses/Apache-2.0.txt) is preserved. Lucide icons retain **ISC**, the modified epdiy driver retains **LGPL-3.0-or-later**, and pypinyin dictionary data retains **MIT**. See [Third-party notices](THIRD_PARTY_NOTICES.md) and component directories. The new policy does not make all existing firmware noncommercial.

Please report firmware bugs in this repository, not in the MindReset upstream issue tracker. Hardware purchasing and repair remain matters for the [official support channels](https://dot.mindreset.tech/docs/contact).

## File transfer

File management shows **WiFi transfer**, **Hotspot transfer** and **USB transfer** directly. WiFi connects to a saved network or opens network setup; hotspot mode starts the device access point; USB exposes the TF card to the computer. AP/STA browser transfer keeps directory browsing, arbitrary-file uploads and status-signature editing.

Shake-to-turn is off by default and enabled in reader Font Settings. A horizontal left/right impulse turns to the previous/next page. Slow tilts, other axes, touch and rebounds are filtered; wait through an 800 ms cooldown and rest before the next gesture. Direction and sensitivity still need device validation. The global refresh test option is removed and ignored in older configurations.

### Local test: list shelf and lock feedback

Settings → Personalization → Shelf style adds a four-book list with uniform 120×156px covers (aspect fill, centered cropping), author, progress and favorite badges, and at most two title lines. Existing paging, management and favorite ordering remain available. The choice persists across restart and backup restoration. Normal/ripple retain gray covers; fast mode retains only BW covers.

Dark two-pixel frames and separators remain visible in fast/ripple modes. PIN entry paints the complete screen once, without a second backdrop stage. PIN input updates only the inner circular feedback and password dots with local BW differences, without rerendering artwork or glyphs. Delete removes one digit; Cancel keeps the device locked. Collage covers gain soft shadows and thinner white typography outlines, with cache invalidation. Wallpaper, ticket and collage images use physical 16-level grayscale with ordered dithering of 256-level source brightness, rather than native 256-level hardware.

Reader Font Settings → Weight uses a minus/plus capsule and an active-face preview; each step adjusts 1/16-pixel coverage from −2 to +2 pixels (65 levels), preserving legacy settings. Native weight is unchanged; other levels preserve advances and aspect ratio for static imported and embedded book fonts. Reader weight does not alter system pages and is included in configuration backups.

## Latest interface updates (unreleased, both boards)

Fresh/reset profiles enter language, Wi-Fi and font setup; existing configured profiles skip it on upgrade. Chinese/English applies across the system UI and remains selectable in Settings. Book content, filenames and user input retain their original text. Setup imports TTF/OTF fonts through USB storage or QR-based Wi-Fi transfer, rescans the card and offers a paged font picker. It applies the selected font to both system and reading. A connected Wi-Fi network starts background time synchronization; failed saves keep setup open.

Files adds recursive Chinese/pinyin/initials/English search, list/grid views, persistent multiselection, destination-folder moves and confirmed deletion. Moves never overwrite existing targets and failed entries stay selected for retry. Shelf management supports name, last-read and first-added ordering in either direction; pre-existing books register on their first upgraded scan. Wi-Fi remembers eight profiles and marks saved networks; enabling Bluetooth attempts to reconnect the last successfully used paired device.

New books open in standard mode with time/signature/battery at the top, chapter/progress above the visible bottom tools, with Back/title/Favorite below the status row. A centre tap only enters fullscreen; a bottom-edge swipe returns to standard mode and shows tools. Back/title/Favorite and bottom tools appear together and hide together; all tools shortcuts first leave fullscreen. The saved immersive style remains independent. Fullscreen with immersion off shows time/status signature/battery instead of the book title, with the chapter/count/percentage footer; immersion on fills the page with text and keeps only a bottom 3px progress line. Turns retain the selected mode, and mode changes preserve the reading anchor. Metadata updates locally after idle. Drivers, power/sleep and reader waveforms follow PR #59; manual and periodic cleanup remain available. Both boards retain separate hardware backends.
