// Copyright 2024 vy
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "transactions.h"
#include "lib/lib8tion/lib8tion.h"
// Manual number formatting (avoids pulling in snprintf/stdio on AVR)
static void put_u8(char *buf, uint8_t val) {
    buf[0] = (val >= 100) ? '0' + val / 100 : ' ';
    buf[1] = (val >= 10)  ? '0' + (val / 10) % 10 : ' ';
    buf[2] = '0' + val % 10;
    buf[3] = '\0';
}
static void put_02u(char *buf, uint8_t val) {
    buf[0] = '0' + val / 10;
    buf[1] = '0' + val % 10;
    buf[2] = '\0';
}

// ── Layers ──────────────────────────────────────────────
enum sofle_layers {
    _QWERTY,
  //  _COLEMAK,
    _LOWER,
    _RAISE,
    _ADJUST,
};

// ── Custom keycodes ─────────────────────────────────────
enum custom_keycodes {
    KC_PRVWD = QK_USER,
    KC_NXTWD,
    KC_LSTRT,
    KC_LEND,
    TMR_BTN,   // Right encoder button: single=start/stop, double=reset
    TMR_SET,   // Left encoder button: toggle set mode
};

#define KC_QWERTY PDF(_QWERTY)
//#define KC_COLEMAK PDF(_COLEMAK)

// ── LED TEST MODE ────────────────────────────────────────
#define LED_TEST_MODE 0  // set to 1 to enable
#if LED_TEST_MODE
static uint8_t test_led_index = 0;
#endif

// ── Beekeeb LED mapping (matrix row/col → physical LED index) ──
// Left half (rows 0-4, cols 0-5)
static const uint8_t PROGMEM beekeeb_map_left[5][6] = {
    {31, 30, 22, 21, 12, 11},  // row 0: ` 1 2 3 4 5
    {32, 29, 23, 20, 13, 10},  // row 1: esc q w e r t
    {33, 28, 24, 19, 14,  9},  // row 2: tab a s d f g
    {34, 27, 25, 18, 15,  8},  // row 3: lsft z x c v b
    {26, 17, 16,  7,  6, 255}, // row 4: win alt ctrl lo spc enc
};
// Right half: same pattern, offset by 36
static const uint8_t PROGMEM beekeeb_map_right[5][6] = {
    {67, 66, 58, 57, 48, 47},
    {68, 65, 59, 56, 49, 46},
    {69, 64, 60, 55, 50, 45},
    {70, 63, 61, 54, 51, 44},
    {62, 53, 52, 43, 42, 255},
};

// Track last keypresses for reactive LED
#define MAX_RECENT_KEYS 5
static struct { uint8_t led; uint16_t timer; } recent_keys[MAX_RECENT_KEYS];
static uint8_t recent_key_count = 0;

// ── Countdown timer state (master side) ──────────────────
static bool     timer_running    = false;
static bool     timer_alarm      = false;   // true when countdown reached 0
static bool     timer_set_mode   = false;   // true when setting time with encoder
static uint32_t timer_start_ms   = 0;
static uint32_t timer_elapsed_ms = 0;       // accumulated elapsed
static uint32_t timer_target_ms  = 0;       // countdown target (set by encoder)

// Double press tracking for right encoder button
static uint32_t tmr_last_press   = 0;
static bool     tmr_waiting      = false;

// ── Sync structure (shared between halves) ──────────────
typedef struct {
    bool     running;
    bool     alarm;
    bool     set_mode;
    uint32_t remaining_ms;
    uint32_t target_ms;
    uint8_t  recent_count;
    uint8_t  recent_leds[MAX_RECENT_KEYS];
    uint8_t  recent_fades[MAX_RECENT_KEYS];  // 0=just pressed, 255=expired
    uint8_t  wpm;
} timer_sync_t;

static timer_sync_t synced_timer = {0};

// ── Keymap ──────────────────────────────────────────────
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

[_QWERTY] = LAYOUT(
  KC_GRV,   KC_1,   KC_2,    KC_3,    KC_4,    KC_5,                     KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_MINUS,
  KC_ESC,   KC_Q,   KC_W,    KC_E,    KC_R,    KC_T,                     KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,  KC_BSPC,
  KC_TAB,   KC_A,   KC_S,    KC_D,    KC_F,    KC_G,                     KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN,  KC_QUOT,
  KC_LSFT,  KC_Z,   KC_X,    KC_C,    KC_V,    KC_B, TMR_SET,     TMR_BTN,KC_N,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,  KC_RSFT,
                 KC_LGUI,KC_LALT,KC_LCTL, TL_LOWR, KC_SPC,      KC_ENT,  TL_UPPR, KC_RCTL, KC_RALT, KC_RGUI
),
/*
[_COLEMAK] = LAYOUT(
  KC_GRV,   KC_1,   KC_2,    KC_3,    KC_4,    KC_5,                      KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_GRV,
  KC_ESC,   KC_Q,   KC_W,    KC_F,    KC_P,    KC_G,                      KC_J,    KC_L,    KC_U,    KC_Y, KC_SCLN,  KC_BSPC,
  KC_TAB,   KC_A,   KC_R,    KC_S,    KC_T,    KC_D,                      KC_H,    KC_N,    KC_E,    KC_I,    KC_O,  KC_QUOT,
  KC_LSFT,  KC_Z,   KC_X,    KC_C,    KC_V,    KC_B, TMR_SET,      TMR_BTN,KC_K,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,  KC_RSFT,
                 KC_LGUI,KC_LALT,KC_LCTL,TL_LOWR, KC_ENT,        KC_SPC,  TL_UPPR, KC_RCTL, KC_RALT, KC_RGUI
),
*/
[_LOWER] = LAYOUT(
  _______,   KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,                       KC_F6,   KC_F7,   KC_F8,   KC_F9,  KC_F10,  KC_F11,
  KC_GRV,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                       KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_F12,
  _______, KC_EXLM,   KC_AT, KC_HASH,  KC_DLR, KC_PERC,                       KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_PIPE,
  _______,  KC_EQL, KC_MINS, KC_PLUS, KC_LCBR, KC_RCBR, _______,       TMR_BTN, KC_LBRC, KC_RBRC, KC_NUBS, S(KC_NUBS), KC_BSLS, _______,
                       _______, _______, _______, _______, _______,       _______, _______, _______, _______, _______
),

[_RAISE] = LAYOUT(
  _______, _______ , _______ , _______ , _______ , _______,                           _______,  _______  , _______,  _______ ,  _______ ,_______,
  _______,  KC_INS,  KC_PSCR,   KC_APP,  XXXXXXX, XXXXXXX,                        KC_PGUP, KC_PRVWD,   KC_UP, KC_NXTWD,C(KC_BSPC), KC_BSPC,
  _______, KC_LALT,  KC_LCTL,  KC_LSFT,  XXXXXXX, KC_CAPS,                       KC_PGDN,  KC_LEFT, KC_DOWN, KC_RGHT,  KC_DEL, KC_BSPC,
  _______, C(KC_Z), C(KC_X), C(KC_C), C(KC_V), XXXXXXX,  _______,       TMR_BTN,  XXXXXXX, KC_LSTRT, XXXXXXX, KC_LEND,   XXXXXXX, _______,
                         _______, _______, _______, _______, _______,       _______, _______, _______, _______, _______
),

  // ADJUST: LOWER + RAISE
  [_ADJUST] = LAYOUT(
  XXXXXXX , XXXXXXX,  XXXXXXX ,  XXXXXXX , XXXXXXX, XXXXXXX,                     XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  QK_BOOT  , XXXXXXX,KC_QWERTY,XXXXXXX,XXXXXXX,XXXXXXX,                     XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  XXXXXXX , XXXXXXX,XXXXXXX, XXXXXXX,    XXXXXXX,  XXXXXXX,                     XXXXXXX, KC_VOLD, TMR_SET, KC_VOLU, XXXXXXX, XXXXXXX,
  XXXXXXX , XXXXXXX, XXXXXXX, XXXXXXX,    XXXXXXX,  XXXXXXX, XXXXXXX,   TMR_BTN, XXXXXXX, KC_MPRV, KC_MPLY, KC_MNXT, XXXXXXX, XXXXXXX,
                   _______, _______, _______, _______, _______,     _______, _______, _______, _______, _______
  )
};

// ── Timer helpers ───────────────────────────────────────
static uint32_t get_remaining(void) {
    uint32_t elapsed = timer_elapsed_ms;
    if (timer_running) {
        elapsed += timer_elapsed32(timer_start_ms);
    }
    if (elapsed >= timer_target_ms) return 0;
    return timer_target_ms - elapsed;
}

// ── Split sync: slave callback ──────────────────────────
static void timer_sync_slave_handler(uint8_t in_buflen, const void *in_data,
                                     uint8_t out_buflen, void *out_data) {
    memcpy(&synced_timer, in_data, sizeof(timer_sync_t));
}

// ── Init: register split transaction ────────────────────
void keyboard_post_init_user(void) {
    transaction_register_rpc(RPC_ID_USER_TIMER_SYNC, timer_sync_slave_handler);

    // Force purple solid splash + underglow via indicators callback
    rgb_matrix_mode(RGB_MATRIX_SOLID_SPLASH);
    rgb_matrix_sethsv(200, 255, 120);
}

// ── Housekeeping: check alarm + single press + sync ─────
void housekeeping_task_user(void) {
    if (!is_keyboard_master()) return;

    // Check if countdown reached 0
    if (timer_running && !timer_alarm && get_remaining() == 0) {
        timer_alarm   = true;
        timer_running = false;
    }

    // Single press timeout: start/stop after 300ms with no double
    if (tmr_waiting && timer_elapsed32(tmr_last_press) >= 300) {
        tmr_waiting = false;
        if (timer_alarm) {
            // Alarm active: single press stops alarm
            timer_alarm = false;
        } else if (timer_running) {
            // Running: pause
            timer_elapsed_ms += timer_elapsed32(timer_start_ms);
            timer_running = false;
        } else if (timer_target_ms > 0 && !timer_set_mode) {
            // Stopped with target set: start countdown
            timer_start_ms = timer_read32();
            timer_running  = true;
        }
    }

    static uint32_t last_sync = 0;
    if (timer_elapsed32(last_sync) > 100) {
        last_sync = timer_read32();

        timer_sync_t payload = {
            .running      = timer_running,
            .alarm        = timer_alarm,
            .set_mode     = timer_set_mode,
            .remaining_ms = get_remaining(),
            .target_ms    = timer_target_ms,
            .recent_count = recent_key_count,
            .wpm          = get_current_wpm(),
        };
        for (uint8_t i = 0; i < recent_key_count; i++) {
            payload.recent_leds[i] = recent_keys[i].led;
            uint16_t elapsed = timer_elapsed(recent_keys[i].timer);
            payload.recent_fades[i] = elapsed >= 1000 ? 255 : (elapsed * 255 / 1000);
        }
        transaction_rpc_send(RPC_ID_USER_TIMER_SYNC, sizeof(payload), &payload);
    }
}

// ── Key processing ──────────────────────────────────────
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Track keypress for reactive LED
    if (record->event.pressed) {
        uint8_t row = record->event.key.row;
        uint8_t col = record->event.key.col;
        uint8_t led = 255;
        if (row < 5) {
            led = pgm_read_byte(&beekeeb_map_left[row][col]);
        } else if (row < 10) {
            led = pgm_read_byte(&beekeeb_map_right[row - 5][col]);
        }
        if (led != 255) {
            // Shift oldest entries
            if (recent_key_count < MAX_RECENT_KEYS) recent_key_count++;
            for (uint8_t i = recent_key_count - 1; i > 0; i--) {
                recent_keys[i] = recent_keys[i - 1];
            }
            recent_keys[0].led   = led;
            recent_keys[0].timer = timer_read();
        }
    }

    switch (keycode) {
        case TMR_SET:
            if (record->event.pressed) {
                if (!timer_running && !timer_alarm) {
                    timer_set_mode = !timer_set_mode;
                }
            }
            return false;

        case TMR_BTN:
            if (record->event.pressed) {
                if (tmr_waiting && timer_elapsed32(tmr_last_press) < 300) {
                    // Double press: reset everything
                    tmr_waiting      = false;
                    timer_running    = false;
                    timer_alarm      = false;
                    timer_set_mode   = false;
                    timer_elapsed_ms = 0;
                    timer_start_ms   = 0;
                } else {
                    tmr_waiting    = true;
                    tmr_last_press = timer_read32();
                }
            }
            return false;

        case KC_PRVWD:
            if (record->event.pressed) {
                register_mods(mod_config(MOD_LCTL));
                register_code(KC_LEFT);
            } else {
                unregister_mods(mod_config(MOD_LCTL));
                unregister_code(KC_LEFT);
            }
            break;
        case KC_NXTWD:
            if (record->event.pressed) {
                register_mods(mod_config(MOD_LCTL));
                register_code(KC_RIGHT);
            } else {
                unregister_mods(mod_config(MOD_LCTL));
                unregister_code(KC_RIGHT);
            }
            break;
        case KC_LSTRT:
            if (record->event.pressed) {
                register_code(KC_HOME);
            } else {
                unregister_code(KC_HOME);
            }
            break;
        case KC_LEND:
            if (record->event.pressed) {
                register_code(KC_END);
            } else {
                unregister_code(KC_END);
            }
            break;
    }
    return true;
}

// ── Encoder: right encoder adjusts timer when in set mode ─
bool encoder_update_user(uint8_t index, bool clockwise) {
    // Beekeeb left encoder has swapped A/B pins
    if (index == 0) clockwise = !clockwise;

#if LED_TEST_MODE
    if (index == 1) {
        if (clockwise) {
            test_led_index = (test_led_index + 1) % RGB_MATRIX_LED_COUNT;
        } else {
            test_led_index = (test_led_index == 0) ? RGB_MATRIX_LED_COUNT - 1 : test_led_index - 1;
        }
        return false;
    }
#endif
    if (index == 1 && timer_set_mode && !timer_running && !timer_alarm) {
        // Right encoder: ±1 min
        if (clockwise) {
            timer_target_ms += 60000;
        } else if (timer_target_ms >= 60000) {
            timer_target_ms -= 60000;
        }
        // Reset elapsed when changing target
        timer_elapsed_ms = 0;
        timer_start_ms   = 0;
        return false;
    }
    // Left encoder: volume (handled here because beekeeb has swapped pins)
    if (index == 0) {
        tap_code(clockwise ? KC_VOLU : KC_VOLD);
        return false;
    }
    return true;
}

// ── RGB: purple base, pressed key fades red → purple ──
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
#if LED_TEST_MODE
    for (uint8_t i = led_min; i < led_max; i++) {
        if (i == test_led_index) {
            rgb_matrix_set_color(i, 255, 255, 255);  // bright white
        } else {
            rgb_matrix_set_color(i, 5, 0, 5);  // dim
        }
    }
    return false;
#endif
    // Master uses local recent_keys, slave uses synced data
    bool master = is_keyboard_master();
    uint8_t count = master ? recent_key_count : synced_timer.recent_count;

    for (uint8_t i = led_min; i < led_max; i++) {
        // Base: purple
        uint8_t r = 80, g = 0, b = 120;

        for (uint8_t j = 0; j < count; j++) {
            uint8_t led_idx;
            uint8_t fade;
            if (master) {
                led_idx = recent_keys[j].led;
                uint16_t elapsed = timer_elapsed(recent_keys[j].timer);
                fade = elapsed >= 1000 ? 255 : (elapsed * 255 / 1000);
            } else {
                led_idx = synced_timer.recent_leds[j];
                fade = synced_timer.recent_fades[j];
            }
            if (led_idx == i && fade < 255) {
                // Blend red → purple over fade range
                r = 255 - (fade * (255 - 80) / 255);   // 255 → 80
                g = 0;
                b = fade * 120 / 255;                    // 0 → 120
                break;
            }
        }
        rgb_matrix_set_color(i, r, g, b);
    }
    return false;
}

// ── OLED: Timer display on slave (right) ────────────────
#ifdef OLED_ENABLE

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (!is_keyboard_master()) {
        return OLED_ROTATION_270;  // right side: portrait
    }
    return rotation;  // left side: keep board default
}

static void render_status(void) {
    // Blink OLED when alarm (toggle every 500ms)
    oled_invert(synced_timer.alarm && (timer_read() % 1000 < 500));

    char buf[6];


    oled_write_ln_P(PSTR("-----"), false);
    oled_write_ln_P(PSTR("Timer"), false);
    // ── Timer ──
    uint32_t ms;
    if (synced_timer.set_mode) {
        ms = synced_timer.target_ms;
    } else {
        ms = synced_timer.remaining_ms;
    }
    uint32_t total_s = ms / 1000;
    uint8_t secs  = total_s % 60;
    uint8_t mins  = (total_s / 60) % 60;
    uint8_t hours = total_s / 3600;

    if (hours > 0) {
        put_u8(buf, hours);
        // trim leading spaces, append ':'
        char *p = buf; while (*p == ' ') p++;
        oled_write(p, false);
        oled_write_P(PSTR(":"), false);
        put_02u(buf, mins);
        oled_write_ln(buf, false);
        buf[0] = ' '; buf[1] = ' '; buf[2] = ':';
        put_02u(buf + 3, secs);
        oled_write_ln(buf, false);
    } else {
        put_02u(buf, mins);
        buf[2] = ':';
        put_02u(buf + 3, secs);
        oled_write_ln(buf, false);
    }


    // ── Status indicator ──
    if (synced_timer.alarm) {
        oled_write_ln_P(PSTR("ALARM"), false);
    } else if (synced_timer.running) {
        oled_write_ln_P(PSTR(" RUN"), false);
    } else if (synced_timer.set_mode) {
        oled_write_ln_P(PSTR(" SET"), false);
    } else if (synced_timer.target_ms > 0) {
        oled_write_ln_P(PSTR("PAUSE"), false);
    } else {
        oled_write_ln_P(PSTR("  -"), false);
    }
    oled_write_ln_P(PSTR("-----"), false);
    // ── WPM ──
    oled_write_ln_P(PSTR(" WPM "), false);
    put_u8(buf, synced_timer.wpm);
    oled_write_ln(buf, false);

    oled_write_ln_P(PSTR("-----"), false);
}

bool oled_task_user(void) {
#if LED_TEST_MODE
    if (is_keyboard_master()) {
        char buf[6];
        oled_write_ln_P(PSTR(""), false);
        oled_write_ln_P(PSTR("LED"), false);
        oled_write_ln_P(PSTR("TEST"), false);
        oled_write_ln_P(PSTR(""), false);
        oled_write_P(PSTR("# "), false);
        put_u8(buf, test_led_index);
        oled_write_ln(buf, false);
        oled_write_ln_P(PSTR(""), false);
        oled_write_P(PSTR("/"), false);
        put_u8(buf, RGB_MATRIX_LED_COUNT);
        oled_write_ln(buf, false);
        return false;
    }
#endif
    if (!is_keyboard_master()) {
        // Slave (right): show status, skip kb-level logo
        render_status();
        return false;
    }
    // Master (left): let sofle.c render status as usual
    return true;
}

#endif
