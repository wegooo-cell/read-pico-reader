/* Shared ticket persistence and time formatting. / 双机共用票根持久化与时长格式。 */
#define TICKET_DAY_MS UINT64_C(86400000)
#define TICKET_STATS_MAGIC UINT32_C(0x52505432)
#define TICKET_POSITION_MAGIC UINT32_C(0x52505031)

static const char *const ticket_namespace = KIIKO_BOARD_METALIO47 ? "mt_ticket" : "rp_ticket";

typedef struct {
    uint32_t magic, day;
    uint64_t total_seconds, today_seconds;
    uint32_t total_turns, today_turns;
} legacy_ticket_stats_t;

typedef struct {
    uint32_t day, milliseconds, turns;
} ticket_day_t;

typedef struct {
    uint32_t magic, total_turns;
    uint64_t total_ms;
    ticket_day_t days[32];
} ticket_stats_t;

typedef struct {
    uint32_t magic, size, offset;
    uint16_t block;
    uint8_t pct;
    char path[288], chapter[128];
} ticket_position_t;

static ticket_position_t s_position;
static bool s_position_saved;

uint64_t book_ticket_local_time_ms(void) {
    struct timeval now = {0};
    if (gettimeofday(&now, NULL) != 0 || now.tv_sec < 1704067200) {
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        if (!pmu || !pmu->time_synced || pmu->unix_sec < 1704067200) return 0;
        now.tv_sec = pmu->unix_sec;
        now.tv_usec = 0;
    }
    return ((uint64_t)now.tv_sec + 8 * 3600) * 1000 + now.tv_usec / 1000;
}

static uint32_t local_day(void) {
    return (uint32_t)(book_ticket_local_time_ms() / TICKET_DAY_MS);
}

static uint32_t as_ms(uint64_t seconds) {
    return seconds > UINT32_MAX / 1000 ? UINT32_MAX : (uint32_t)seconds * 1000;
}

static uint32_t plus_u32(uint32_t old, uint32_t add) {
    return UINT32_MAX - old < add ? UINT32_MAX : old + add;
}

static esp_err_t stats_read(nvs_handle_t h, ticket_stats_t *out) {
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, "reading_ms", out, &len);
    if (err == ESP_OK)
        return len == sizeof(*out) && out->magic == TICKET_STATS_MAGIC ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;

    *out = (ticket_stats_t){.magic = TICKET_STATS_MAGIC};
    legacy_ticket_stats_t totals = {0};
    len = sizeof(totals);
    err = nvs_get_blob(h, "stats", &totals, &len);
    if (err == ESP_OK && len == sizeof(totals) && totals.magic == UINT32_C(0x52505431)) {
        out->total_ms = totals.total_seconds > UINT64_MAX / 1000 ? UINT64_MAX : totals.total_seconds * 1000;
        out->total_turns = totals.total_turns;
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    } else {
        memset(&totals, 0, sizeof(totals));
    }
    struct {uint32_t magic, day[32], seconds[32];} month;
    len = sizeof(month);
    err = nvs_get_blob(h, "heat32", &month, &len);
    if (err == ESP_OK && len == sizeof(month) && month.magic == UINT32_C(0x52504832)) {
        for (unsigned i = 0; i < 32; ++i)
            if (month.day[i] && month.day[i] % 32 == i)
                out->days[i] = (ticket_day_t){month.day[i], as_ms(month.seconds[i]), 0};
    } else {
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
        struct {uint32_t magic, day[30], seconds[30];} heat;
        len = sizeof(heat);
        err = nvs_get_blob(h, "heatmap", &heat, &len);
        if (err == ESP_OK && len == sizeof(heat) && heat.magic == UINT32_C(0x52504831)) {
            for (unsigned i = 0; i < 30; ++i) if (heat.day[i] && heat.day[i] % 30 == i) {
                ticket_day_t *slot = &out->days[heat.day[i] % 32];
                if (slot->day < heat.day[i]) *slot = (ticket_day_t){heat.day[i], as_ms(heat.seconds[i]), 0};
            }
        } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }
    if (totals.day) {
        ticket_day_t *slot = &out->days[totals.day % 32];
        if (slot->day <= totals.day) {
            const uint32_t ms = as_ms(totals.today_seconds);
            const uint32_t existing = slot->day == totals.day ? slot->milliseconds : 0;
            *slot = (ticket_day_t){totals.day, ms > existing ? ms : existing, totals.today_turns};
        }
    }
    return ESP_OK;
}

static void stats_load(ticket_stats_t *out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open(ticket_namespace, NVS_READONLY, &h) != ESP_OK) return;
    if (stats_read(h, out) != ESP_OK) memset(out, 0, sizeof(*out));
    nvs_close(h);
}

esp_err_t book_ticket_record(uint32_t day, uint32_t milliseconds, uint32_t turns) {
    if (!milliseconds && !turns) return ESP_OK;
    nvs_handle_t h;
    esp_err_t err = nvs_open(ticket_namespace, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    ticket_stats_t stats;
    err = stats_read(h, &stats);
    if (err == ESP_OK) {
        stats.total_ms = UINT64_MAX - stats.total_ms < milliseconds ? UINT64_MAX : stats.total_ms + milliseconds;
        stats.total_turns = plus_u32(stats.total_turns, turns);
        if (day) {
            ticket_day_t *slot = &stats.days[day % 32];
            if (slot->day < day) *slot = (ticket_day_t){.day = day};
            if (slot->day == day) {
                slot->milliseconds = plus_u32(slot->milliseconds, milliseconds);
                slot->turns = plus_u32(slot->turns, turns);
            }
        }
        // 单条记录提交总计和每日数据，保留读取旧格式的能力。
        // Commit totals and daily data in one record, retaining legacy reads.
        err = nvs_set_blob(h, "reading_ms", &stats, sizeof(stats));
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static ticket_day_t stats_day(const ticket_stats_t *stats, uint32_t day) {
    const ticket_day_t slot = stats->days[day % 32];
    return slot.day == day ? slot : (ticket_day_t){0};
}

bool book_ticket_recent_days(uint32_t seconds[30]) {
    if (!seconds) return false;
    const uint32_t today = local_day();
    ticket_stats_t stats;
    if (today) stats_load(&stats);
    for (unsigned i = 0; i < 30; ++i)
        seconds[i] = today ? stats_day(&stats, today - (29 - i)).milliseconds / 1000 : 0;
    return today != 0;
}

bool book_ticket_month_days(int year, int month, uint32_t seconds[31]) {
    if (!seconds) return false;
    memset(seconds, 0, 31 * sizeof(*seconds));
    const uint32_t today = local_day();
    if (!today || year < 2024 || year > 9999 || month < 1 || month > 12) return false;
    const time_t timestamp = (time_t)today * 86400;
    struct tm date;
    if (!gmtime_r(&timestamp, &date) || date.tm_year + 1900 != year || date.tm_mon + 1 != month)
        return false;
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned count = days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 || year % 400 == 0));
    const uint32_t first = today - (date.tm_mday - 1);
    ticket_stats_t stats;
    stats_load(&stats);
    for (unsigned i = 0; i < count; ++i)
        if (first + i <= today) seconds[i] = stats_day(&stats, first + i).milliseconds / 1000;
    return true;
}

void book_ticket_format_time(char *out, size_t cap, uint64_t milliseconds) {
    const unsigned long long seconds = milliseconds / 1000;
    if (seconds >= 3600)
        snprintf(out, cap, "%llu 小时 %02llu 分 %02llu 秒", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else if (seconds >= 60)
        snprintf(out, cap, "%llu 分 %02llu 秒", seconds / 60, seconds % 60);
    else
        snprintf(out, cap, "%llu 秒", seconds);
}

void book_ticket_note_position(const char *path, const book_progress_t *progress,
                               const char *chapter, bool saved) {
    if (!path || !path[0] || strlen(path) >= sizeof(s_position.path) || !progress || !chapter) return;
    const bool same = s_position.magic == TICKET_POSITION_MAGIC &&
        s_position.size == progress->file_size && s_position.offset == progress->byte_off &&
        s_position.block == progress->chapter && s_position.pct == progress->pct &&
        !strcmp(s_position.path, path) && !strcmp(s_position.chapter, chapter);
    if (!same) {
        s_position = (ticket_position_t){.magic = TICKET_POSITION_MAGIC, .size = progress->file_size,
            .offset = progress->byte_off, .block = progress->chapter, .pct = progress->pct};
        strcpy(s_position.path, path);
        snprintf(s_position.chapter, sizeof(s_position.chapter), "%s", chapter);
        s_position_saved = false;
    }
    if (!saved || s_position_saved) return;
    nvs_handle_t h;
    if (nvs_open(ticket_namespace, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "position", &s_position, sizeof(s_position)) == ESP_OK)
        s_position_saved = nvs_commit(h) == ESP_OK;
    nvs_close(h);
}

static bool position_for_ticket(const char *path, uint32_t size, bool from_reader,
                                bool saved, book_progress_t *progress, char chapter[128]) {
    chapter[0] = 0;
    if (from_reader && s_position.magic == TICKET_POSITION_MAGIC &&
        s_position.size == size && !strcmp(s_position.path, path)) {
        progress->pct = s_position.pct;
        strcpy(chapter, s_position.chapter);
        return true;
    }
    if (!saved) return false;
    ticket_position_t position;
    size_t len = sizeof(position);
    nvs_handle_t h;
    if (nvs_open(ticket_namespace, NVS_READONLY, &h) != ESP_OK) return true;
    const esp_err_t err = nvs_get_blob(h, "position", &position, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(position) && position.magic == TICKET_POSITION_MAGIC &&
        position.size == size && position.block == progress->chapter && position.offset == progress->byte_off &&
        !strncmp(position.path, path, sizeof(position.path)) &&
        memchr(position.chapter, 0, sizeof(position.chapter)))
        strcpy(chapter, position.chapter);
    return true;
}
