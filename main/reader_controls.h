#pragma once
// 中文：阅读手势的稳定编号与可移植设置；运行中的自动翻页不持久化。
// English: Stable gesture IDs and portable preferences; a running auto-turn never persists.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RC_NONE, RC_PREVIOUS, RC_NEXT, RC_TOOLS, RC_FULLSCREEN, RC_CONTENTS,
    RC_BOOKMARK, RC_AUTOTURN, RC_EFFECT, RC_REFRESH, RC_GUIDE,
    RC_BOOKMARKS, RC_PROGRESS, RC_TEXT, RC_LAYOUT, RC_DISPLAY,
    RC_TURN_SETTINGS, RC_STATS, RC_LIGHT, RC_BRIGHTNESS, RC_WARMTH, RC_TOOLS_TOGGLE,
    RC_ACTION_COUNT
} reader_control_action_t;
typedef enum {
    RC_TAP_CENTER, RC_DOUBLE_CENTER, RC_HOLD, RC_BOTTOM_LEFT,
    RC_BOTTOM_CENTER, RC_BOTTOM_RIGHT, RC_TAP_PREVIOUS, RC_TAP_NEXT,
    RC_SLOT_COUNT
} reader_control_slot_t;
typedef enum {RC_SIDE_OFF, RC_SIDE_BRIGHTNESS, RC_SIDE_WARMTH, RC_SIDE_FONT, RC_SIDE_LINE, RC_SIDE_COUNT} reader_control_side_t;
enum {READER_AUTO_MIN_SECONDS = 1, READER_AUTO_MAX_SECONDS = 10};
typedef struct {
    uint8_t version, actions[RC_SLOT_COUNT], sides[2], seconds_le[2], checksum[4];
} reader_controls_config_t;
static inline unsigned reader_controls_seconds(const reader_controls_config_t *c) {
    return (unsigned)c->seconds_le[0] | ((unsigned)c->seconds_le[1]<<8);
}
static inline uint32_t reader_controls_checksum(const reader_controls_config_t *c) {
    const uint8_t *p=(const uint8_t*)c;uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(reader_controls_config_t,checksum);++i)h=(h^p[i])*16777619u;
    return h;
}
static inline void reader_controls_seal(reader_controls_config_t *c) {
    uint32_t h=reader_controls_checksum(c);for(unsigned i=0;i<4;++i)c->checksum[i]=(uint8_t)(h>>(8*i));
}
static inline bool reader_controls_valid(const reader_controls_config_t *c) {
    if(c->version!=1 || reader_controls_seconds(c)>600)return false;
    for(unsigned i=0;i<RC_SLOT_COUNT;++i)if(c->actions[i]>=RC_ACTION_COUNT)return false;
    for(unsigned i=0;i<2;++i)if(c->sides[i]>=RC_SIDE_COUNT)return false;
    uint32_t h=0;for(unsigned i=0;i<4;++i)h|=(uint32_t)c->checksum[i]<<(8*i);
    return h==reader_controls_checksum(c);
}
static inline reader_controls_config_t reader_controls_defaults(bool s31) {
    reader_controls_config_t c={.version=1,
        .actions={RC_FULLSCREEN,RC_NONE,RC_CONTENTS,RC_BOOKMARK,RC_TOOLS,RC_AUTOTURN,RC_PREVIOUS,RC_NEXT},
        .sides={s31?RC_SIDE_BRIGHTNESS:RC_SIDE_OFF,s31?RC_SIDE_WARMTH:RC_SIDE_OFF},.seconds_le={READER_AUTO_MAX_SECONDS,0}};
    reader_controls_seal(&c);return c;
}
static inline bool reader_controls_action_allowed(unsigned a,bool s31) {
    return a<RC_ACTION_COUNT && (s31||a<RC_LIGHT||a==RC_TOOLS_TOGGLE);
}
/// 只读当前机型的设置。/ Read preferences filtered for this board.
const reader_controls_config_t *app_settings_reader_controls(void);
/// 成功提交后才更新缓存。/ Update cached preferences only after commit succeeds.
bool app_settings_set_reader_control(unsigned slot,unsigned action);
bool app_settings_set_reader_side(unsigned side,unsigned action);
bool app_settings_set_reader_auto_seconds(unsigned seconds);

// 中文：时钟只在安全正文中走一次，不追赶被暂停的历史页。
// English: Advance at most once in a safe reader body; never catch up paused pages.
typedef struct {bool running;int64_t due_ms;} reader_auto_clock_t;
static inline void reader_auto_stop(reader_auto_clock_t *s){s->running=false;s->due_ms=0;}
static inline void reader_auto_start(reader_auto_clock_t *s,int64_t now,unsigned seconds){s->running=true;s->due_ms=now+(int64_t)seconds*1000;}
static inline bool reader_auto_due(reader_auto_clock_t *s,int64_t now,unsigned seconds,bool safe,bool end){
    if(!s->running)return false;
    if(end){reader_auto_stop(s);return false;}
    if(!safe){s->due_ms=now+(int64_t)seconds*1000;return false;}
    if(now<s->due_ms)return false;
    s->due_ms=now+(int64_t)seconds*1000;return true;
}
