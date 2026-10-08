#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 在仓库根运行管理功能回归；网页脚本另用 Node 验证。
# Run management regressions from the repository root; validate web interactions separately with Node.
set -euo pipefail
bash tools/run_book_host_tests.sh
python3 tools/test_book_epub.py
mkdir -p build/book-tests/settings-card
cc -std=gnu11 -Wall -Wextra -Werror -Itools/settings_backup_stubs \
    -Icomponents/read_pico_transfer/include \
    tools/settings_backup_host_test.c -o build/book-tests/settings-backup
build/book-tests/settings-backup
cc -std=c11 -Wall -Wextra -Werror -Imain tools/auto_lock_host_test.c -o build/book-tests/auto-lock
build/book-tests/auto-lock
cc -std=gnu11 -Wall -Wextra -Werror -Itools/settings_backup_stubs \
    tools/book_history_backup_host_test.c -o build/book-tests/book-history-backup
build/book-tests/book-history-backup
python3 tools/book_ui_host_test.py
bash tools/run_text_input_host_tests.sh
python3 tools/reader_images_host_test.py
python3 tools/book_layout_draw_host_test.py
python3 tools/ui_font_coverage_host_test.py
python3 tools/test_home_recent.py
python3 tools/test_book_toc.py
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Itools/book_cover_auto_stubs -Imain/book -Imain/font \
    tools/book_cover_auto_host_test.c main/book/book_cover_auto.c -o build/book-tests/auto-cover
build/book-tests/auto-cover
cc -std=c11 -Wall -Wextra -Werror -Imain/ui \
    tools/wallpaper_crop_host_test.c -o build/book-tests/wallpaper-crop
build/book-tests/wallpaper-crop
python3 tools/test_search.py
flags=(-std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -g -fsanitize=address,undefined)
gcc "${flags[@]}" -Itools/file_tree_stubs -Imain/apps tools/file_tree_host_test.c main/apps/file_tree.c -o build/book-tests/file-tree
build/book-tests/file-tree
gcc "${flags[@]}" -Icomponents/read_pico_search/include tools/transfer_host_test.c components/read_pico_search/read_pico_search.c -o build/book-tests/transfer
build/book-tests/transfer
gcc "${flags[@]}" -Imanaged_components/espressif__cjson/cJSON tools/transfer_wifi_host_test.c managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build/book-tests/transfer-wifi
build/book-tests/transfer-wifi
gcc "${flags[@]}" -Itools/transfer_ui_stubs -Icomponents/read_pico_transfer/include -Icomponents/read_pico_search/include -Imain/ui \
    tools/transfer_ui_host_test.c main/ui/ui_text_edit.c main/ui/ui_text_input.c main/ui/ui_keyboard.c main/ui/ui_ime.c components/read_pico_search/read_pico_search.c -o build/book-tests/transfer-ui
python3 tools/test_usb_storage.py
build/book-tests/transfer-ui
bash tools/run_app_loop_host_tests.sh
bash tools/run_display_host_test.sh
bash tools/run_water_turn_host_test.sh
python3 tools/test_lcd_frame_lifecycle.py
python3 tools/test_sd_media_guard.py

python3 tools/test_transfer_netif.py
python3 tools/test_weread_port.py
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itools/weread_ui_stubs -Icomponents/pico_weread/include \
    tools/weread_ui_host_test.c -o build/book-tests/weread-ui
build/book-tests/weread-ui

c++ -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined \
    -Itools/weread_service_stubs -Icomponents/pico_weread/include \
    tools/weread_service_host_test.cpp -o build/book-tests/weread-service
build/book-tests/weread-service

python3 tools/test_pmu_battery.py
python3 tools/ble_turner_host_test.py
for test in ota_release ota_online; do
    cc -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined \
        -Itools/ota_stubs -Imain -Imanaged_components/espressif__cjson/cJSON \
        "tools/${test}_host_test.c" main/ota_release.c managed_components/espressif__cjson/cJSON/cJSON.c \
        -lm -o "build/book-tests/${test}"
    "build/book-tests/${test}"
done
python3 tools/ota_ui_host_test.py

python3 tools/image_page_host_test.py
python3 tools/pmic_gate_host_test.py
python3 tools/settings_scroll_host_test.py
python3 tools/ui_refresh_feedback_host_test.py
python3 tools/clear_shelf_host_test.py
