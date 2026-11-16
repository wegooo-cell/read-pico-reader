/* Shared reader clock. / 双机共用正文计时。 */
#define READER_TICKET_DAY_MS UINT64_C(86400000)
#define READER_TICKET_IDLE_MS 300000

typedef struct {
    uint32_t day, milliseconds, turns;
} reader_ticket_pending_t;

static reader_ticket_pending_t s_ticket_pending[4];
static int64_t s_ticket_sample_ms;
static int64_t s_ticket_flush_ms;
static bool s_ticket_visible;

static void flush_ticket_stats(void) {
    s_ticket_flush_ms = s_ticket_sample_ms;
    for (size_t i = 0; i < sizeof(s_ticket_pending) / sizeof(*s_ticket_pending); ++i) {
        reader_ticket_pending_t *entry = &s_ticket_pending[i];
        if ((entry->milliseconds || entry->turns) &&
            book_ticket_record(entry->day, entry->milliseconds, entry->turns) == ESP_OK)
            *entry = (reader_ticket_pending_t){0};
    }
}

static void ticket_add(uint32_t day, uint32_t milliseconds, uint32_t turns) {
    if (!milliseconds && !turns) return;
    // Slot zero keeps totals if the wall clock is unavailable or corrected repeatedly.
    reader_ticket_pending_t *entry = &s_ticket_pending[0];
    if (day) {
        for (size_t i = 1; i < sizeof(s_ticket_pending) / sizeof(*s_ticket_pending); ++i) {
            if (s_ticket_pending[i].day == day) {
                entry = &s_ticket_pending[i];
                break;
            }
            if (!s_ticket_pending[i].day && entry == &s_ticket_pending[0])
                entry = &s_ticket_pending[i];
        }
        if (entry != &s_ticket_pending[0]) entry->day = day;
    }
    entry->milliseconds += milliseconds;
    entry->turns += turns;
}

static void ticket_count_turn(void) {
    ticket_add((uint32_t)(book_ticket_local_time_ms() / READER_TICKET_DAY_MS), 0, 1);
    ++s_session_turns;
}

void app_book_ticket_sample(int64_t now_ms, bool exposed) {
    const bool visible = exposed && app_book_reader_body_visible();
    const int64_t previous = s_ticket_sample_ms;
    s_ticket_sample_ms = now_ms;
    if (s_ticket_visible && previous > 0 && now_ms > previous && now_ms - previous <= 5000) {
        const int64_t idle_limit = s_stats_activity_ms + READER_TICKET_IDLE_MS;
        const int64_t end = now_ms < idle_limit ? now_ms : idle_limit;
        if (end > previous) {
            const uint32_t elapsed = (uint32_t)(end - previous);
            const uint64_t local_ms = book_ticket_local_time_ms();
            const uint64_t local_end = local_ms ? local_ms - (uint64_t)(now_ms - end) : 0;
            const uint32_t day = (uint32_t)(local_end / READER_TICKET_DAY_MS);
            const uint32_t since_midnight = (uint32_t)(local_end % READER_TICKET_DAY_MS);
            if (day && elapsed > since_midnight)
                ticket_add(day - 1, elapsed - since_midnight, 0);
            ticket_add(day, day && elapsed > since_midnight ? since_midnight : elapsed, 0);
            if (UINT32_MAX - s_session_read_ms < elapsed) s_session_read_ms = UINT32_MAX;
            else s_session_read_ms += elapsed;
        }
    }
    if (visible && !s_ticket_visible) {
        s_stats_activity_ms = now_ms;
        if (!s_ticket_flush_ms) s_ticket_flush_ms = now_ms;
    }
    s_ticket_visible = visible;
    if (s_ticket_flush_ms && now_ms - s_ticket_flush_ms >= 30000)
        flush_ticket_stats();
}
