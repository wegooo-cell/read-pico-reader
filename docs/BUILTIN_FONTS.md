# 内建阅读字库 / Embedded reader fonts

Pico 与 S31 的内建轮廓与补字合计覆盖 7668 个字符，包括 GB2312 的 6763 个汉字、通用规范汉字表前 6500 字，以及部分姓名字、标点与字母。字体不能覆盖任意书籍中的全部生僻字；可在 TF 卡导入字体。
Both boards cover 7668 characters across embedded outlines and supplements, including all 6763 GB2312 Han, the first 6500 standardized Han, and selected names, punctuation and letters. Rarer text may require an imported card font.

正文先使用所选字体已有的字形，缺字才查内建轮廓和补字。借用字形不改变字体选择，冷缓存排版与实际绘制使用相同字宽。界面和正文解压工作区独立，内存不足时保留下次重试机会。
Reader text prefers the selected face, then borrows embedded outlines or supplements for missing glyphs. Borrowing preserves face selection and matching cold-layout/raster advances. UI and reader decode workspaces are independent; transient allocation failures remain retryable.

Pico 与 S31 使用相同的 24px、16 级覆盖率补字、四点插值和逐字墨量校正；补字字宽与边界按源字体计算。两板保留原有 1263 个轮廓字符及全部 6030 个界面补字的像素与度量。S31 移除不再参与显示的旧锁屏原图，为灰阶字库腾出空间，保留现有应用分区与锁屏样式。大字号补字细节受原始位图分辨率限制。
Both boards use identical 24px,16-level coverage, four-tap interpolation and per-glyph ink calibration, with source-font advances and bounds. Preserve all1263 original outlines and all6030 legacy UI supplement pixels/metrics. S31 drops unused legacy lock artwork to retain its application partition and current lock styles. Large supplemental glyph detail remains limited by the bitmap resolution.

字体来源为 Noto Sans SC Medium，适用 [SIL OFL](../main/assets/OFL-Noto.txt)。字符表来源及 Unicode 许可见 [常用字表](../tools/fixtures/reader-common-6500.txt) 与 [Unicode 许可](../licenses/Unicode-3.0.txt)。
Fonts derive from Noto Sans SC Medium under the linked SIL OFL. Character-list provenance and the Unicode license are linked above.

使用静态 Medium 源字体与 FontTools 4.68.0 可重建资源；源字体 SHA-256 为 `724c63c5666743c774eb72b8edf38d345e4fe21d239af3a5e94ed4bbd61e8c4a`。生成器也支持从可变源固定到字重 500，但输出字节可能随 FontTools 版本变化。
Reproduce assets using static Medium with the SHA-256 above and FontTools 4.68.0. Generators also instantiate variable input at weight 500; bytes may vary across FontTools versions.

```sh
python3 tools/gen_pico_builtin_font.py --src /path/to/medium.ttf
cp boards/pico/src/main/assets/builtin.ttf boards/metalio47/src/main/assets/builtin.ttf
python3 tools/gen_reader_hanzi_aa.py --src /path/to/medium.ttf
cp boards/pico/src/main/assets/ui-hanzi.bin boards/metalio47/src/main/assets/ui-hanzi.bin
```

两板测试视图运行 `pico_builtin_font_host_test.py`、`reader_hanzi_fallback_host_test.py` 和 `ui_hanzi_host_test.py`，验证字符覆盖、原轮廓像素、界面补字、字宽、字体回退及内存失败恢复。
Run the named regressions in both board test views to verify coverage, original outline/UI pixels, advances, fallback and allocation-failure recovery.
