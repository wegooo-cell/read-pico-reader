// 页面联网任务仍在使用连接，传输组件同时跟踪收发、网页操作与对时。
// Outbound jobs keep the connection in use; the transfer service also tracks HTTP and clock work.
#include "book/book_weread_cover.h"
#include "book/book_xteink_sync.h"
#include "online_font.h"

static void poll_wifi_idle(void) {
    const book_weread_cover_status_t covers = book_weread_cover_status();
    online_font_status_t font;
    online_font_status(&font);
    read_pico_transfer_idle_poll(pico_online_busy() || xteink_sync_busy() || font.busy ||
                                 covers.running || covers.saving || (covers.active && covers.multiple));
}
