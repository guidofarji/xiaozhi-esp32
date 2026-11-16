#include "jotape_ui.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "jotape_eyes.h"
#include "jotape_glow.h"
#include "jotape_helmet.h"

LV_FONT_DECLARE(font_jotape_20);
LV_FONT_DECLARE(font_jotape_30);
LV_FONT_DECLARE(font_jotape_inter_14);
LV_FONT_DECLARE(font_jotape_inter_18);
LV_FONT_DECLARE(font_jotape_icons_22);
LV_FONT_DECLARE(font_jotape_icons_30);
LV_FONT_DECLARE(font_jotape_weather_64);
LV_FONT_DECLARE(font_jotape_bigicons_72);
LV_FONT_DECLARE(font_jotape_dots_3);
LV_FONT_DECLARE(font_jotape_dots_5);
LV_FONT_DECLARE(font_jotape_dots_6);
LV_FONT_DECLARE(font_jotape_dots_7);

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr uint32_t kBootHoldMs = 2500;
constexpr uint32_t kMenuTimeoutMs = 20000;
constexpr uint32_t kTimerPeriodMs = 50;

// Material Symbols glyphs (font_jotape_icons_22 / _30)
#define ICON_SCHEDULE "\xEE\xA2\xB5"   // U+E8B5 schedule
#define ICON_CLOUD "\xEE\x8A\xBD"      // U+E2BD cloud
#define ICON_BELL "\xEE\x9F\xB4"       // U+E7F4 notifications
#define ICON_HOURGLASS "\xEE\xA2\x8B"  // U+E88B hourglass_empty
#define ICON_NEWSPAPER "\xEE\xAE\x81"  // U+EB81 newspaper
#define ICON_SETTINGS "\xEE\xA2\xB8"   // U+E8B8 settings
#define BIG_ALARM "\xEE\xA1\x95"       // U+E855 alarm (72 px)
#define BIG_HOURGLASS "\xEE\xA2\x8B"   // U+E88B (72 px)
#define DEGREE "\xC2\xB0"

lv_color_t Green() { return lv_color_hex(0x39FF88); }
lv_color_t Mint() { return lv_color_hex(0xC8FFE0); }
lv_color_t DimGreen() { return lv_color_hex(0x1E7A48); }
lv_color_t Orange() { return lv_color_hex(0xFF8A1F); }
lv_color_t Yellow() { return lv_color_hex(0xFFC93C); }

const char* const kDays[] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
const char* const kMonths[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                               "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};

struct MenuEntry {
    const char* icon;
    const char* text;
};

const MenuEntry kMenuEntries[] = {
    {ICON_SCHEDULE, "Hora"},   {ICON_CLOUD, "Clima"},        {ICON_BELL, "Alarmas"},
    {ICON_HOURGLASS, "Timer"}, {ICON_NEWSPAPER, "Noticias"}, {ICON_SETTINGS, "Ajustes"},
};

// Plain object with no theme styles, not clickable or scrollable.
lv_obj_t* Decor(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* Rect(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color) {
    lv_obj_t* obj = Decor(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

bool Is(const char* a, const char* b) { return strcmp(a, b) == 0; }

// Smooth 0..255 oscillation for breathing effects.
int Breath(uint32_t now, uint32_t period_ms) {
    const int angle = static_cast<int>((now % period_ms) * 360 / period_ms);
    return (lv_trigo_sin(angle) + LV_TRIGO_SIN_MAX) * 255 / (2 * LV_TRIGO_SIN_MAX);
}

// Material Symbols glyph for a WMO weather code.
const char* WeatherIcon(int code, bool is_day) {
    if (code == 0 || code == 1) {
        return is_day ? "\xEE\xA0\x9A" : "\xEE\x94\x9C";  // sunny / dark_mode
    }
    if (code == 2) {
        return is_day ? "\xEF\x85\xB2" : "\xEE\xA9\x86";  // partly cloudy day / night
    }
    if (code == 45 || code == 48) {
        return "\xEE\xA0\x98";  // foggy
    }
    if (code >= 51 && code <= 67) {
        return code == 65 || code == 67 ? "\xEF\x98\x9F" : "\xEF\x85\xB6";  // rainy
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return "\xEE\x8B\x8D";  // snowy
    }
    if (code >= 80 && code <= 82) {
        return "\xEF\x85\xB6";  // rainy
    }
    if (code >= 95) {
        return "\xEE\xAF\x9B";  // thunderstorm
    }
    return "\xEE\x8A\xBD";  // cloud
}

// True once `now` has reached `deadline` (wrap-around safe).
bool Reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace

/* ------------------------------------------------------------------ */
/* JotapeFace                                                          */
/* ------------------------------------------------------------------ */

namespace {

struct ShapeEntry {
    const char* name;
    const lv_image_dsc_t* xl;
    const lv_image_dsc_t* big;
    const lv_image_dsc_t* small;
};

const ShapeEntry kShapes[] = {
    {"heart", &jotape_eye_heart_xl, &jotape_eye_heart_big, &jotape_eye_heart_small},
    {"sparkle", &jotape_eye_sparkle_xl, &jotape_eye_sparkle_big, &jotape_eye_sparkle_small},
    {"spiral", &jotape_eye_spiral_xl, &jotape_eye_spiral_big, &jotape_eye_spiral_small},
    {"chev_up", &jotape_eye_chev_up_xl, &jotape_eye_chev_up_big, &jotape_eye_chev_up_small},
    {"chev_right", &jotape_eye_chev_right_xl, &jotape_eye_chev_right_big,
     &jotape_eye_chev_right_small},
    {"chev_left", &jotape_eye_chev_left_xl, &jotape_eye_chev_left_big, &jotape_eye_chev_left_small},
    {"angry_l", &jotape_eye_angry_l_xl, &jotape_eye_angry_l_big, &jotape_eye_angry_l_small},
    {"angry_r", &jotape_eye_angry_r_xl, &jotape_eye_angry_r_big, &jotape_eye_angry_r_small},
    {"sad_l", &jotape_eye_sad_l_xl, &jotape_eye_sad_l_big, &jotape_eye_sad_l_small},
    {"sad_r", &jotape_eye_sad_r_xl, &jotape_eye_sad_r_big, &jotape_eye_sad_r_small},
    {"tear", &jotape_eye_tear_xl, &jotape_eye_tear_big, &jotape_eye_tear_small},
    {"arc", &jotape_eye_arc_xl, &jotape_eye_arc_big, &jotape_eye_arc_small},
};

lv_color_t EyeGreen() { return lv_color_hex(0x5DFFA0); }
lv_color_t HeartPink() { return lv_color_hex(0xFF4D7A); }

}  // namespace

const lv_image_dsc_t* JotapeFace::Shape(const char* name) const {
    for (const auto& s : kShapes) {
        if (strcmp(s.name, name) == 0) {
            switch (size_) {
                case JotapeFaceSize::kXL:
                    return s.xl;
                case JotapeFaceSize::kBig:
                    return s.big;
                default:
                    return s.small;
            }
        }
    }
    return nullptr;
}

void JotapeFace::Create(lv_obj_t* parent, JotapeFaceSize size) {
    size_ = size;
    tint_ = EyeGreen();

    root_ = lv_image_create(parent);
    if (size == JotapeFaceSize::kXL) {
        lv_image_set_src(root_, &jotape_helmet_150);
    } else if (size == JotapeFaceSize::kBig) {
        lv_image_set_src(root_, &jotape_helmet_112);
    } else {
        lv_image_set_src(root_, &jotape_helmet_60);
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    screen_ = Decor(root_);
    if (size == JotapeFaceSize::kXL) {
        lv_obj_set_pos(screen_, JOTAPE_HELMET_150_SCREEN_X, JOTAPE_HELMET_150_SCREEN_Y);
        screen_w_ = JOTAPE_HELMET_150_SCREEN_W;
        screen_h_ = JOTAPE_HELMET_150_SCREEN_H;
    } else if (size == JotapeFaceSize::kBig) {
        lv_obj_set_pos(screen_, JOTAPE_HELMET_112_SCREEN_X, JOTAPE_HELMET_112_SCREEN_Y);
        screen_w_ = JOTAPE_HELMET_112_SCREEN_W;
        screen_h_ = JOTAPE_HELMET_112_SCREEN_H;
    } else {
        lv_obj_set_pos(screen_, JOTAPE_HELMET_60_SCREEN_X, JOTAPE_HELMET_60_SCREEN_Y);
        screen_w_ = JOTAPE_HELMET_60_SCREEN_W;
        screen_h_ = JOTAPE_HELMET_60_SCREEN_H;
    }
    lv_obj_set_size(screen_, screen_w_, screen_h_);

    for (int i = 0; i < 2; i++) {
        bars_[i] = Decor(screen_);
        lv_obj_set_style_bg_color(bars_[i], EyeGreen(), 0);
        lv_obj_set_style_bg_opa(bars_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bars_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_color(bars_[i], EyeGreen(), 0);
        lv_obj_set_style_shadow_width(bars_[i], size == JotapeFaceSize::kSmall ? 4 : 10, 0);
        lv_obj_set_style_shadow_opa(bars_[i], LV_OPA_40, 0);

        images_[i] = lv_image_create(screen_);
        lv_obj_remove_flag(images_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_image_recolor_opa(images_[i], LV_OPA_COVER, 0);
    }

    tear_ = lv_image_create(screen_);
    lv_image_set_src(tear_, Shape("tear"));
    lv_obj_remove_flag(tear_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor(tear_, lv_color_hex(0x7FD8FF), 0);
    lv_obj_set_style_image_recolor_opa(tear_, LV_OPA_COVER, 0);

    question_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(
        question_, size == JotapeFaceSize::kSmall ? &font_jotape_20 : &font_jotape_30, 0);
    lv_obj_set_style_text_color(question_, lv_color_hex(0xFFC93C), 0);
    lv_label_set_text(question_, "?");

    SetExpression("neutral");
}

void JotapeFace::SetExpression(const char* emotion) {
    const char* e = emotion != nullptr ? emotion : "neutral";
    Eye bar;
    eyes_[0] = eyes_[1] = bar;
    eye_dy_ = 0;
    show_tear_ = false;
    show_question_ = false;
    tint_ = EyeGreen();

    auto both = [this](const char* shape) { eyes_[0].image = eyes_[1].image = Shape(shape); };

    if (Is(e, "happy") || Is(e, "funny") || Is(e, "delicious")) {
        both("chev_up");
    } else if (Is(e, "laughing")) {
        eyes_[0].image = Shape("chev_right");
        eyes_[1].image = Shape("chev_left");
    } else if (Is(e, "loving") || Is(e, "kissy")) {
        both("heart");
        tint_ = HeartPink();
    } else if (Is(e, "confident")) {
        both("sparkle");
    } else if (Is(e, "surprised") || Is(e, "shocked")) {
        eyes_[0].dot = eyes_[1].dot = true;
        eyes_[0].w_pct = eyes_[1].w_pct = 190;
        eyes_[0].h_pct = eyes_[1].h_pct = 75;
    } else if (Is(e, "relaxed") || Is(e, "embarrassed")) {
        both("arc");
    } else if (Is(e, "sleepy")) {
        eyes_[0].w_pct = eyes_[1].w_pct = 220;
        eyes_[0].h_pct = eyes_[1].h_pct = 14;
        eye_dy_ = 10;
    } else if (Is(e, "cool")) {
        eyes_[0].w_pct = eyes_[1].w_pct = 260;
        eyes_[0].h_pct = eyes_[1].h_pct = 34;
        eye_dy_ = -4;
    } else if (Is(e, "sad")) {
        eyes_[0].image = Shape("sad_l");
        eyes_[1].image = Shape("sad_r");
        eye_dy_ = 6;
    } else if (Is(e, "crying")) {
        eyes_[0].image = Shape("sad_l");
        eyes_[1].image = Shape("sad_r");
        eye_dy_ = 2;
        show_tear_ = true;
    } else if (Is(e, "angry")) {
        eyes_[0].image = Shape("angry_l");
        eyes_[1].image = Shape("angry_r");
    } else if (Is(e, "thinking")) {
        eyes_[0].w_pct = eyes_[1].w_pct = 220;
        eyes_[0].h_pct = eyes_[1].h_pct = 16;
        show_question_ = true;
    } else if (Is(e, "confused") || Is(e, "silly")) {
        both("spiral");
    } else if (Is(e, "winking")) {
        eyes_[1].image = Shape("chev_up");
    }
    Apply();
}

void JotapeFace::Apply() {
    if (root_ == nullptr) {
        return;
    }
    const int base_w = LV_MAX(3, screen_w_ * 11 / 100);
    const int base_h = LV_MAX(6, screen_h_ * 42 / 100);
    const int cy = screen_h_ / 2 + screen_h_ * eye_dy_ / 100;
    for (int i = 0; i < 2; i++) {
        const Eye& eye = eyes_[i];
        const int cx = (i == 0 ? 30 : 70) * screen_w_ / 100 + look_dx_;
        if (eye.image != nullptr) {
            lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(images_[i], eye.image);
            lv_obj_set_style_image_recolor(images_[i], tint_, 0);
            const int bounce =
                talk_ >= 0 ? (talk_ * (size_ == JotapeFaceSize::kSmall ? 1 : 4) / 100) : 0;
            lv_obj_set_pos(images_[i], cx - eye.image->header.w / 2,
                           cy - eye.image->header.h / 2 - bounce);
            if (closed_) {
                lv_obj_add_flag(images_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(images_[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        lv_obj_add_flag(images_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        int w = base_w * eye.w_pct / 100;
        int h = base_h * eye.h_pct / 100;
        if (eye.dot) {
            w = h = LV_MIN(w, h);
        }
        if (listening_ && !eye.dot && eye.h_pct >= 100) {
            h = h * 120 / 100;
        }
        if (talk_ >= 0 && !eye.dot && eye.h_pct >= 100) {
            h = h * (70 + talk_ * 45 / 100) / 100;
        }
        if (closed_) {
            h = LV_MAX(2, base_h / 8);
            w = LV_MAX(w, base_w * 2);
        }
        lv_obj_set_size(bars_[i], w, h);
        lv_obj_set_pos(bars_[i], cx - w / 2, cy - h / 2);
    }
    if (show_tear_ && !closed_) {
        lv_obj_remove_flag(tear_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(tear_, 30 * screen_w_ / 100 - 2, cy + screen_h_ * 14 / 100);
    } else {
        lv_obj_add_flag(tear_, LV_OBJ_FLAG_HIDDEN);
    }
    if (show_question_) {
        lv_obj_remove_flag(question_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(question_, LV_ALIGN_TOP_RIGHT, -screen_w_ * 6 / 100, 0);
    } else {
        lv_obj_add_flag(question_, LV_OBJ_FLAG_HIDDEN);
    }
}

void JotapeFace::SetEyesClosed(bool closed) {
    if (closed_ == closed) {
        return;
    }
    closed_ = closed;
    Apply();
}

void JotapeFace::SetTalkLevel(int percent) {
    talk_ = percent < 0 ? -1 : LV_MIN(percent, 100);
    Apply();
}

void JotapeFace::SetListening(bool listening) {
    if (listening_ == listening) {
        return;
    }
    listening_ = listening;
    Apply();
}

void JotapeFace::SetLook(int dx) {
    if (look_dx_ == dx) {
        return;
    }
    look_dx_ = dx;
    Apply();
}

/* ------------------------------------------------------------------ */
/* JotapeUi                                                            */
/* ------------------------------------------------------------------ */

lv_obj_t* JotapeUi::NewPanel(lv_obj_t* parent) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, kWidth, kHeight);
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

lv_obj_t* JotapeUi::NewLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color,
                             const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

void JotapeUi::TintGlass(lv_obj_t* g, lv_color_t tint) {
    lv_obj_set_style_bg_color(g, lv_color_mix(tint, lv_color_black(), 50), 0);
    lv_obj_set_style_border_color(g, tint, 0);
    lv_obj_set_style_shadow_color(g, tint, 0);
}

lv_obj_t* JotapeUi::Glass(lv_obj_t* parent, int x, int y, int w, int h, int radius,
                          lv_color_t tint) {
    lv_obj_t* g = Decor(parent);
    lv_obj_set_pos(g, x, y);
    lv_obj_set_size(g, w, h);
    lv_obj_set_style_radius(g, radius, 0);
    lv_obj_set_style_bg_grad_color(g, lv_color_hex(0x030605), 0);
    lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g, 1, 0);
    lv_obj_set_style_border_opa(g, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(g, 14, 0);
    lv_obj_set_style_shadow_opa(g, LV_OPA_20, 0);
    TintGlass(g, tint);

    // Glass sheen: white highlight fading out over the top half
    lv_obj_t* sheen = Decor(g);
    lv_obj_add_flag(sheen, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(sheen, 0, 0);
    lv_obj_set_size(sheen, lv_pct(100), lv_pct(45));
    lv_obj_set_style_radius(sheen, radius > 1 ? radius - 1 : 0, 0);
    lv_obj_set_style_bg_color(sheen, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_color(sheen, lv_color_white(), 0);
    lv_obj_set_style_bg_main_opa(sheen, 22, 0);
    lv_obj_set_style_bg_grad_opa(sheen, 0, 0);
    lv_obj_set_style_bg_grad_dir(sheen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(sheen, LV_OPA_COVER, 0);

    // Inner rim: a faint white edge just inside the border gives the pane
    // thickness; brighter on the top/left where the light hits.
    lv_obj_t* rim = Decor(g);
    lv_obj_add_flag(rim, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(rim, 0, 0);
    lv_obj_set_size(rim, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(rim, radius > 1 ? radius - 1 : 0, 0);
    lv_obj_set_style_border_color(rim, lv_color_white(), 0);
    lv_obj_set_style_border_width(rim, 1, 0);
    lv_obj_set_style_border_opa(rim, 30, 0);
    lv_obj_t* lit = Decor(g);
    lv_obj_add_flag(lit, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(lit, 1, 1);  // one pixel inside the border: a second, lit edge
    lv_obj_set_size(lit, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(lit, radius > 1 ? radius - 1 : 0, 0);
    lv_obj_set_style_border_color(lit, lv_color_white(), 0);
    lv_obj_set_style_border_width(lit, 1, 0);
    lv_obj_set_style_border_opa(lit, 95, 0);
    lv_obj_set_style_border_side(
        lit, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), 0);

    // Specular glint along the top edge, fading out to both sides
    for (int side = 0; side < 2; side++) {
        lv_obj_t* glint = Decor(g);
        lv_obj_add_flag(glint, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(glint, side == 0 ? lv_pct(12) : lv_pct(40), 0);
        lv_obj_set_size(glint, lv_pct(side == 0 ? 28 : 40), 1);
        lv_obj_set_style_bg_color(glint, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_color(glint, lv_color_white(), 0);
        lv_obj_set_style_bg_main_opa(glint, side == 0 ? 0 : 230, 0);
        lv_obj_set_style_bg_grad_opa(glint, side == 0 ? 230 : 0, 0);
        lv_obj_set_style_bg_grad_dir(glint, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(glint, LV_OPA_COVER, 0);
    }
    // Soft reflection on the bottom edge (light passing through the pane)
    for (int side = 0; side < 2; side++) {
        lv_obj_t* glint = Decor(g);
        lv_obj_add_flag(glint, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(glint, lv_pct(30), 1);
        lv_obj_align(glint, LV_ALIGN_BOTTOM_LEFT, side == 0 ? lv_pct(20) : lv_pct(50), 0);
        lv_obj_set_style_bg_color(glint, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_color(glint, lv_color_white(), 0);
        lv_obj_set_style_bg_main_opa(glint, side == 0 ? 0 : 90, 0);
        lv_obj_set_style_bg_grad_opa(glint, side == 0 ? 90 : 0, 0);
        lv_obj_set_style_bg_grad_dir(glint, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(glint, LV_OPA_COVER, 0);
    }
    return g;
}

lv_obj_t* JotapeUi::NewGlow(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* glow = lv_image_create(parent);
    lv_image_set_src(glow, &jotape_glow_200);
    lv_obj_remove_flag(glow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor(glow, color, 0);
    lv_obj_set_style_image_recolor_opa(glow, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(glow, LV_OPA_50, 0);
    return glow;
}

lv_obj_t* JotapeUi::NewButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h) {
    lv_obj_t* btn = Glass(parent, x, y, w, h, h / 2, Orange());
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_t* label = NewLabel(btn, &text14_, Mint(), text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, TimerButtonCb, LV_EVENT_CLICKED, this);
    return btn;
}

void JotapeUi::Create(lv_obj_t* screen, const Fonts& fonts) {
    screen_ = screen;
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    text14_ = font_jotape_inter_14;
    text18_ = font_jotape_inter_18;
    dots3_ = font_jotape_dots_3;
    UpdateFonts(fonts);

    CreateBoot();
    CreateTerminal();
    CreateHome();
    CreateMenu();
    CreateListening();
    CreateSpeaking();
    CreateWeather();
    CreateAlarms();
    CreateTimer();
    CreateRinging();

    // Toast: glass pill above every panel
    toast_ = Glass(screen_, 0, 0, 10, 10, 16, Orange());
    lv_obj_set_size(toast_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(toast_, 14, 0);
    lv_obj_set_style_pad_ver(toast_, 6, 0);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -14);
    toast_label_ = NewLabel(toast_, &text14_, Orange(), "");
    lv_obj_set_style_max_width(toast_label_, kWidth - 50, 0);
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(screen_, GestureCb, LV_EVENT_GESTURE, this);

    boot_started_ = lv_tick_get();
    next_blink_ = boot_started_ + 1800;
    ApplyScreen(JotapeScreen::kBoot);
    timer_ = lv_timer_create(TimerCb, kTimerPeriodMs, this);
}

void JotapeUi::UpdateFonts(const Fonts& fonts) {
    text14_.fallback = fonts.fallback_text;
    text18_.fallback = fonts.fallback_text;
    dots3_.fallback = &text14_;
    if (fonts.status_icons != nullptr) {
        lv_obj_t* labels[] = {network_label_, battery_label_, mute_label_};
        for (auto label : labels) {
            if (label != nullptr) {
                lv_obj_set_style_text_font(label, fonts.status_icons, 0);
            }
        }
    }
    if (screen_ != nullptr) {
        lv_obj_report_style_change(nullptr);
    }
    status_icon_font_ = fonts.status_icons;
}

void JotapeUi::AddFloat(lv_obj_t* obj, int base_y, int amplitude, uint32_t period_ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a,
                        [](void* var, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(var), v); });
    lv_anim_set_values(&a, base_y - amplitude, base_y + amplitude);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_reverse_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* ---- Boot / listening / speaking: floating head with a glow ---- */

void JotapeUi::CreateBoot() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kBoot)] = NewPanel(screen_);
    boot_glow_ = NewGlow(p, Green());
    lv_obj_center(boot_glow_);
    boot_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(boot_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(boot_face_.root(), 0, 5, 1800);
}

void JotapeUi::CreateWave(lv_obj_t* parent, lv_obj_t** bars, lv_color_t color) {
    for (int i = 0; i < 10; i++) {
        const int x = i < 5 ? 8 + i * 7 : 199 + (i - 5) * 7;
        bars[i] = Rect(parent, x, kHeight / 2 - 4, 5, 8, lv_color_white());
        lv_obj_set_style_radius(bars[i], LV_RADIUS_CIRCLE, 0);
        // Capsule with a bright core fading into the palette color
        lv_obj_set_style_bg_color(bars[i], lv_color_mix(lv_color_white(), color, 110), 0);
        lv_obj_set_style_bg_grad_color(bars[i], color, 0);
        lv_obj_set_style_bg_grad_dir(bars[i], LV_GRAD_DIR_VER, 0);
        lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);  // only shown with sound
    }
}

void JotapeUi::UpdateWave(lv_obj_t** bars, int level) {
    for (int i = 0; i < 10; i++) {
        const int idx = i < 5 ? i : 9 - i;  // taller towards the head
        const int max_h = (22 + idx * 12) * level / 100;
        const int h = 5 + (max_h > 0 ? static_cast<int>(Random() % (max_h + 1)) : 0);
        lv_obj_set_height(bars[i], h);
        lv_obj_set_y(bars[i], kHeight / 2 - h / 2);
        lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void JotapeUi::HideWave(lv_obj_t** bars) {
    for (int i = 0; i < 10; i++) {
        if (!lv_obj_has_flag(bars[i], LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void JotapeUi::CreateListening() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kListening)] = NewPanel(screen_);
    listen_glow_ = NewGlow(p, Orange());
    lv_obj_center(listen_glow_);
    listen_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(listen_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(listen_face_.root(), 0, 5, 1800);
    CreateWave(p, listen_wave_, Orange());
}

void JotapeUi::CreateSpeaking() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kSpeaking)] = NewPanel(screen_);
    speak_glow_ = NewGlow(p, Green());
    lv_obj_center(speak_glow_);
    speak_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(speak_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(speak_face_.root(), 0, 5, 1800);
    CreateWave(p, speak_wave_, Green());
}

/* ---- Terminal: Wi-Fi setup, activation, errors ---- */

void JotapeUi::CreateTerminal() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kTerminal)] = NewPanel(screen_);
    lv_obj_t* card = Glass(p, 8, 8, 224, 224, 24, Green());

    lv_obj_t* title = NewLabel(card, &dots3_, Green(), "JOTAPE");
    lv_obj_set_pos(title, 16, 16);
    terminal_status_ = NewLabel(card, &text14_, Orange(), "Iniciando...");
    lv_obj_set_size(terminal_status_, 120, text14_.line_height);
    lv_label_set_long_mode(terminal_status_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(terminal_status_, 16, 42);

    terminal_face_.Create(card, JotapeFaceSize::kSmall);
    lv_obj_align(terminal_face_.root(), LV_ALIGN_TOP_RIGHT, -12, 8);

    terminal_message_ = NewLabel(card, &text14_, Mint(), "");
    lv_obj_set_size(terminal_message_, 196, 136);
    lv_label_set_long_mode(terminal_message_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(terminal_message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(terminal_message_, 2, 0);
    lv_obj_set_pos(terminal_message_, 13, 76);
}

/* ---- Home: dot-matrix clock and the head peeking from below ---- */

void JotapeUi::CreateHome() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kHome)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, HomeClickCb, LV_EVENT_CLICKED, this);

    home_glow_ = NewGlow(p, Green());
    lv_obj_align(home_glow_, LV_ALIGN_BOTTOM_MID, 0, 70);

    lv_obj_t* pill = Glass(p, 12, 8, 216, 30, 15, Green());
    date_label_ = NewLabel(pill, &text14_, Mint(), "");
    lv_obj_align(date_label_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t* icons = Decor(pill);
    lv_obj_set_size(icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 4, 0);
    lv_obj_align(icons, LV_ALIGN_RIGHT_MID, -10, 0);
    const lv_font_t* icon_font = status_icon_font_ != nullptr ? status_icon_font_ : &text14_;
    mute_label_ = NewLabel(icons, icon_font, Mint(), "");
    network_label_ = NewLabel(icons, icon_font, Mint(), "");
    battery_label_ = NewLabel(icons, icon_font, Mint(), "");

    // Two-tone dot-matrix clock: hours green, minutes orange
    lv_obj_t* clock = Decor(p);
    lv_obj_set_size(clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_ROW);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, 52);
    clock_hours_ = NewLabel(clock, &font_jotape_dots_7, Green(), "--");
    NewLabel(clock, &font_jotape_dots_7, Mint(), ":");
    clock_minutes_ = NewLabel(clock, &font_jotape_dots_7, Orange(), "--");

    home_info_ = Glass(p, 0, 0, 10, 10, 13, Orange());
    lv_obj_set_size(home_info_, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_pad_hor(home_info_, 12, 0);
    lv_obj_set_flex_flow(home_info_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_info_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(home_info_, 4, 0);
    lv_obj_align(home_info_, LV_ALIGN_TOP_MID, 0, 120);
    home_info_icon_ = NewLabel(home_info_, &font_jotape_icons_22, Orange(), ICON_BELL);
    home_info_text_ = NewLabel(home_info_, &text14_, Orange(), "");
    lv_obj_add_flag(home_info_, LV_OBJ_FLAG_HIDDEN);

    home_face_.Create(p, JotapeFaceSize::kBig);
    lv_obj_align(home_face_.root(), LV_ALIGN_BOTTOM_MID, 0, 26);
    AddFloat(home_face_.root(), 26, 3, 2200);
}

/* ---- Menu: grid of glass tiles ---- */

void JotapeUi::CreateMenu() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kMenu)] = NewPanel(screen_);
    lv_obj_t* title = NewLabel(p, &dots3_, Green(), "FUNCIONES");
    lv_obj_set_pos(title, 14, 12);

    for (int i = 0; i < static_cast<int>(JotapeMenuItem::kCount); i++) {
        const int x = 12 + (i % 3) * 76;
        const int y = 40 + (i / 3) * 96;
        lv_obj_t* tile = Glass(p, x, y, 68, 88, 18, i == 0 ? Orange() : Green());
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(tile, Orange(), LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, MenuTileCb, LV_EVENT_CLICKED, this);
        menu_tiles_[i] = tile;

        lv_obj_t* icon = NewLabel(tile, &font_jotape_icons_30, i == 0 ? Orange() : Green(),
                                  kMenuEntries[i].icon);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_t* text = NewLabel(tile, &text14_, Mint(), kMenuEntries[i].text);
        lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -12);
    }
}

/* ---- Weather card ---- */

void JotapeUi::CreateWeather() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kWeather)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, BackHomeCb, LV_EVENT_CLICKED, this);
    weather_card_ = Glass(p, 8, 8, 224, 224, 26, Green());
    lv_obj_add_flag(weather_card_, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* title = NewLabel(weather_card_, &dots3_, Orange(), "CLIMA");
    lv_obj_set_pos(title, 18, 16);
    weather_city_ = NewLabel(weather_card_, &text18_, Mint(), "");
    lv_obj_set_size(weather_city_, 188, text18_.line_height);
    lv_label_set_long_mode(weather_city_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(weather_city_, 18, 38);

    weather_icon_ = NewLabel(weather_card_, &font_jotape_weather_64, Green(), "");
    lv_obj_set_pos(weather_icon_, 12, 70);
    weather_temp_ = NewLabel(weather_card_, &font_jotape_dots_6, Mint(), "--" DEGREE);
    lv_obj_align(weather_temp_, LV_ALIGN_TOP_RIGHT, -16, 86);

    lv_obj_t* max_pill = Glass(weather_card_, 14, 150, 94, 28, 14, Orange());
    weather_max_ = NewLabel(max_pill, &text14_, Orange(), "");
    lv_obj_center(weather_max_);
    lv_obj_t* min_pill = Glass(weather_card_, 116, 150, 94, 28, 14, Green());
    weather_min_ = NewLabel(min_pill, &text14_, Green(), "");
    lv_obj_center(weather_min_);

    weather_desc_ = NewLabel(weather_card_, &text14_, Mint(), "Buscando ubicación...");
    lv_obj_set_width(weather_desc_, 196);
    lv_label_set_long_mode(weather_desc_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(weather_desc_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(weather_desc_, 14, 190);
}

void JotapeUi::SetWeather(const JotapeWeatherView& weather) {
    if (weather_card_ == nullptr) {
        return;
    }
    lv_label_set_text(weather_city_, weather.city.c_str());
    if (!weather.valid) {
        lv_label_set_text(weather_icon_, "");
        lv_label_set_text(weather_temp_, "--" DEGREE);
        lv_label_set_text(weather_max_, "");
        lv_label_set_text(weather_min_, "");
        lv_label_set_text(weather_desc_, weather.status.c_str());
        return;
    }
    const bool sunny = weather.weather_code <= 1 && weather.is_day;
    TintGlass(weather_card_, sunny ? Orange() : Green());
    lv_label_set_text(weather_icon_, WeatherIcon(weather.weather_code, weather.is_day));
    lv_obj_set_style_text_color(weather_icon_, sunny ? Yellow() : Green(), 0);
    lv_label_set_text_fmt(weather_temp_, "%d" DEGREE, weather.temperature);
    lv_label_set_text_fmt(weather_max_, "Máx %d" DEGREE, weather.temp_max);
    lv_label_set_text_fmt(weather_min_, "Mín %d" DEGREE, weather.temp_min);
    lv_label_set_text(weather_desc_, weather.description.c_str());
}

/* ---- Alarms ---- */

void JotapeUi::CreateAlarms() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kAlarms)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, BackHomeCb, LV_EVENT_CLICKED, this);
    lv_obj_t* title = NewLabel(p, &dots3_, Green(), "ALARMAS");
    lv_obj_set_pos(title, 14, 12);

    for (int i = 0; i < 5; i++) {
        alarm_rows_[i] = Glass(p, 12, 38 + i * 39, 216, 33, 16, Green());
        alarm_times_[i] = NewLabel(alarm_rows_[i], &text18_, Mint(), "");
        lv_obj_align(alarm_times_[i], LV_ALIGN_LEFT_MID, 14, 0);
        alarm_infos_[i] = NewLabel(alarm_rows_[i], &text14_, Green(), "");
        lv_obj_set_width(alarm_infos_[i], 128);
        lv_label_set_long_mode(alarm_infos_[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(alarm_infos_[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(alarm_infos_[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_add_flag(alarm_rows_[i], LV_OBJ_FLAG_HIDDEN);
    }
    alarm_empty_ = NewLabel(p, &text14_, Mint(),
                            "Sin alarmas\n\nDecile a JOTAPE:\n\"Poné una alarma a las 7:30\"");
    lv_obj_set_width(alarm_empty_, 200);
    lv_obj_set_style_text_align(alarm_empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(alarm_empty_, LV_ALIGN_CENTER, 0, 10);
}

/* ---- Timer: progress ring ---- */

void JotapeUi::CreateTimer() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kTimer)] = NewPanel(screen_);
    timer_arc_ = lv_arc_create(p);
    lv_obj_remove_style_all(timer_arc_);
    lv_obj_remove_flag(timer_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(timer_arc_, 166, 166);
    lv_obj_align(timer_arc_, LV_ALIGN_TOP_MID, 0, 16);
    lv_arc_set_rotation(timer_arc_, 270);
    lv_arc_set_bg_angles(timer_arc_, 0, 360);
    lv_arc_set_range(timer_arc_, 0, 1000);
    lv_arc_set_value(timer_arc_, 0);
    lv_obj_set_style_arc_width(timer_arc_, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(timer_arc_, DimGreen(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(timer_arc_, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_arc_width(timer_arc_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(timer_arc_, Green(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(timer_arc_, true, LV_PART_INDICATOR);

    timer_count_ = NewLabel(p, &font_jotape_dots_5, Mint(), "00:00");
    lv_obj_align(timer_count_, LV_ALIGN_TOP_MID, 0, 76);
    timer_text_ = NewLabel(p, &text14_, Green(), "Elegí un tiempo");
    lv_obj_set_width(timer_text_, 120);
    lv_label_set_long_mode(timer_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(timer_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(timer_text_, LV_ALIGN_TOP_MID, 0, 126);

    const char* presets[] = {"1 min", "5 min", "10 min"};
    for (int i = 0; i < 3; i++) {
        timer_presets_[i] = NewButton(p, presets[i], 12 + i * 76, 196, 68, 34);
    }
    timer_cancel_ = NewButton(p, "Cancelar", 60, 196, 120, 34);
    lv_obj_add_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
}

/* ---- Ringing ---- */

void JotapeUi::CreateRinging() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kRinging)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, RingClickCb, LV_EVENT_CLICKED, this);

    ring_glow_ = NewGlow(p, Orange());
    lv_obj_center(ring_glow_);
    ring_icon_ = NewLabel(p, &font_jotape_bigicons_72, Orange(), BIG_ALARM);
    lv_obj_align(ring_icon_, LV_ALIGN_TOP_MID, 0, 14);
    ring_title_ = NewLabel(p, &font_jotape_dots_6, Mint(), "");
    lv_obj_align(ring_title_, LV_ALIGN_TOP_MID, 0, 104);
    ring_label_ = NewLabel(p, &text18_, Orange(), "");
    lv_obj_set_width(ring_label_, 210);
    lv_label_set_long_mode(ring_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ring_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ring_label_, LV_ALIGN_TOP_MID, 0, 162);
    lv_obj_t* hint = NewLabel(p, &text14_, Yellow(), "Tocá para apagar");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
}

void JotapeUi::SetAlarms(const JotapeAlarmsView& view) {
    alarms_view_ = view;
    if (home_info_ == nullptr) {
        return;
    }
    if (view.timer_active) {
        lv_label_set_text(home_info_icon_, ICON_HOURGLASS);
        lv_label_set_text_fmt(home_info_text_, "%02d:%02d", view.timer_remaining_s / 60,
                              view.timer_remaining_s % 60);
        lv_obj_remove_flag(home_info_, LV_OBJ_FLAG_HIDDEN);
    } else if (!view.next_alarm.empty()) {
        lv_label_set_text(home_info_icon_, ICON_BELL);
        lv_label_set_text(home_info_text_, view.next_alarm.c_str());
        lv_obj_remove_flag(home_info_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(home_info_, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < 5; i++) {
        if (i < static_cast<int>(view.alarms.size())) {
            const auto& a = view.alarms[i];
            lv_label_set_text(alarm_times_[i], a.time.c_str());
            std::string info = a.repeat;
            if (!a.label.empty()) {
                info = a.label + " · " + info;
            }
            lv_label_set_text(alarm_infos_[i], info.c_str());
            lv_obj_remove_flag(alarm_rows_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(alarm_rows_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (view.alarms.empty()) {
        lv_obj_remove_flag(alarm_empty_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(alarm_empty_, LV_OBJ_FLAG_HIDDEN);
    }

    if (view.timer_active) {
        lv_label_set_text_fmt(timer_count_, "%02d:%02d", view.timer_remaining_s / 60,
                              view.timer_remaining_s % 60);
        const int total = view.timer_total_s > 0 ? view.timer_total_s : 1;
        lv_arc_set_value(timer_arc_, 1000 * (total - view.timer_remaining_s) / total);
        lv_label_set_text(timer_text_,
                          view.timer_label.empty() ? "En marcha" : view.timer_label.c_str());
        for (auto b : timer_presets_) {
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(timer_count_, "00:00");
        lv_arc_set_value(timer_arc_, 0);
        lv_label_set_text(timer_text_, "Elegí un tiempo");
        for (auto b : timer_presets_) {
            lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(ring_icon_, view.ringing_timer ? BIG_HOURGLASS : BIG_ALARM);
    if (view.ringing_timer) {
        lv_label_set_text(ring_title_, "00:00");
        lv_label_set_text(ring_label_, view.ring_label.empty()
                                           ? "¡Tiempo!"
                                           : ("¡" + view.ring_label + "!").c_str());
    } else {
        lv_label_set_text(ring_title_, view.ring_title.c_str());
        lv_label_set_text(ring_label_,
                          view.ring_label.empty() ? "Alarma" : view.ring_label.c_str());
    }
}

/* ---- Screen switching and state ---- */

void JotapeUi::Show(JotapeScreen screen) {
    pending_ = screen;
    if (boot_hold_) {
        return;
    }
    if (screen != current_) {
        ApplyScreen(screen);
    }
}

void JotapeUi::ApplyScreen(JotapeScreen screen) {
    for (auto panel : panels_) {
        if (panel != nullptr) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_remove_flag(panels_[static_cast<int>(screen)], LV_OBJ_FLAG_HIDDEN);

    if (current_ == JotapeScreen::kSpeaking && screen != JotapeScreen::kSpeaking) {
        speak_face_.SetTalkLevel(-1);
    }
    current_ = screen;
    wave_level_ = 0;
    HideWave(listen_wave_);
    HideWave(speak_wave_);

    if (screen == JotapeScreen::kMenu || screen == JotapeScreen::kWeather ||
        screen == JotapeScreen::kAlarms || screen == JotapeScreen::kTimer) {
        menu_touched_ = lv_tick_get();
    }
    if (screen == JotapeScreen::kHome) {
        last_clock_min_ = -1;
        RefreshClock();
    }
    listen_face_.SetListening(screen == JotapeScreen::kListening);
}

void JotapeUi::EndBootHold() {
    if (!boot_hold_) {
        return;
    }
    boot_hold_ = false;
    ApplyScreen(pending_);
}

void JotapeUi::SetStatus(const char* status) {
    if (terminal_status_ == nullptr || status == nullptr) {
        return;
    }
    lv_label_set_text(terminal_status_, status);
}

void JotapeUi::SetMessage(const char* content) {
    if (terminal_message_ == nullptr) {
        return;
    }
    // Inter has no full-width comma (used by some server/locale texts).
    std::string normalized = content != nullptr ? content : "";
    for (size_t pos = normalized.find("\xEF\xBC\x8C"); pos != std::string::npos;
         pos = normalized.find("\xEF\xBC\x8C", pos)) {
        normalized.replace(pos, 3, ", ");
    }
    lv_label_set_text(terminal_message_, normalized.c_str());
}

void JotapeUi::SetSubtitleHidden(bool hidden) { subtitle_hidden_ = hidden; }

void JotapeUi::SetEmotion(const char* emotion) {
    JotapeFace* faces[] = {&boot_face_, &terminal_face_, &home_face_, &listen_face_, &speak_face_};
    for (auto face : faces) {
        if (face->root() != nullptr) {
            face->SetExpression(emotion);
        }
    }
}

void JotapeUi::ShowToast(const char* text, uint32_t duration_ms) {
    if (toast_ == nullptr) {
        return;
    }
    lv_label_set_text(toast_label_, text != nullptr ? text : "");
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_until_ = lv_tick_get() + LV_MAX(duration_ms, 1u);
}

void JotapeUi::RefreshClock() {
    if (clock_hours_ == nullptr) {
        return;
    }
    struct tm now = {};
    if (!time_provider || !time_provider(&now)) {
        if (last_clock_min_ != -2) {
            last_clock_min_ = -2;
            lv_label_set_text(clock_hours_, "--");
            lv_label_set_text(clock_minutes_, "--");
            lv_label_set_text(date_label_, "");
        }
        return;
    }
    const int minute = now.tm_hour * 60 + now.tm_min;
    if (minute == last_clock_min_) {
        return;
    }
    last_clock_min_ = minute;
    lv_label_set_text_fmt(clock_hours_, "%02d", now.tm_hour);
    lv_label_set_text_fmt(clock_minutes_, "%02d", now.tm_min);
    const int wday = now.tm_wday >= 0 && now.tm_wday < 7 ? now.tm_wday : 0;
    const int mon = now.tm_mon >= 0 && now.tm_mon < 12 ? now.tm_mon : 0;
    lv_label_set_text_fmt(date_label_, "%s %d %s", kDays[wday], now.tm_mday, kMonths[mon]);
}

JotapeFace* JotapeUi::ActiveFace() {
    switch (current_) {
        case JotapeScreen::kBoot:
            return &boot_face_;
        case JotapeScreen::kTerminal:
            return &terminal_face_;
        case JotapeScreen::kHome:
            return &home_face_;
        case JotapeScreen::kListening:
            return &listen_face_;
        case JotapeScreen::kSpeaking:
            return &speak_face_;
        default:
            return nullptr;
    }
}

uint32_t JotapeUi::Random() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return rng_ >> 8;
}

void JotapeUi::Tick() { OnTimer(); }

void JotapeUi::OnTimer() {
    const uint32_t now = lv_tick_get();
    tick_count_++;

    if (boot_hold_ && lv_tick_diff(now, boot_started_) >= kBootHoldMs) {
        EndBootHold();
    }
    if (toast_until_ != 0 && Reached(now, toast_until_)) {
        toast_until_ = 0;
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    }
    if ((current_ == JotapeScreen::kMenu || current_ == JotapeScreen::kWeather ||
         current_ == JotapeScreen::kAlarms ||
         (current_ == JotapeScreen::kTimer && !alarms_view_.timer_active)) &&
        lv_tick_diff(now, menu_touched_) >= kMenuTimeoutMs) {
        Show(JotapeScreen::kHome);
    }
    if (current_ == JotapeScreen::kHome && tick_count_ % 20 == 0) {
        RefreshClock();
    }

    // Breathing glows (every other tick keeps redraws light)
    if (tick_count_ % 2 == 0) {
        if (current_ == JotapeScreen::kBoot) {
            lv_obj_set_style_image_opa(boot_glow_, 60 + Breath(now, 3600) * 90 / 255, 0);
        } else if (current_ == JotapeScreen::kHome) {
            lv_obj_set_style_image_opa(home_glow_, 50 + Breath(now, 4400) * 70 / 255, 0);
        } else if (current_ == JotapeScreen::kRinging) {
            lv_obj_set_style_image_opa(ring_glow_, 70 + Breath(now, 900) * 185 / 255, 0);
            lv_obj_set_style_text_opa(ring_title_, (now / 450) % 2 ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }

    // Blink
    JotapeFace* face = ActiveFace();
    if (face != nullptr && current_ != JotapeScreen::kSpeaking) {
        if (blink_end_ != 0 && Reached(now, blink_end_)) {
            face->SetEyesClosed(false);
            blink_end_ = 0;
        } else if (blink_end_ == 0 && Reached(now, next_blink_)) {
            face->SetEyesClosed(true);
            blink_end_ = now + 140;
            next_blink_ = now + 2500 + Random() % 3000;
        }
    }

    // Glance sideways now and then
    if (face != nullptr && current_ != JotapeScreen::kSpeaking) {
        if (look_end_ != 0 && Reached(now, look_end_)) {
            face->SetLook(0);
            look_end_ = 0;
        } else if (look_end_ == 0 && Reached(now, next_look_)) {
            const int dx[] = {-4, -3, 3, 4};
            face->SetLook(dx[Random() % 4]);
            look_end_ = now + 900 + Random() % 600;
            next_look_ = now + 4000 + Random() % 5000;
        }
    }

    if (current_ == JotapeScreen::kListening || current_ == JotapeScreen::kSpeaking) {
        const int target = sound_level ? sound_level() : 0;
        // Grow quickly when sound starts, fade out gently in silence
        if (target > wave_level_) {
            wave_level_ += (target - wave_level_ + 1) * 6 / 10;
        } else {
            wave_level_ -= (wave_level_ - target + 3) / 4;
        }
        wave_level_ = LV_CLAMP(0, wave_level_, 100);
        const bool listening = current_ == JotapeScreen::kListening;
        lv_obj_t** bars = listening ? listen_wave_ : speak_wave_;
        if (wave_level_ < 5) {
            HideWave(bars);
        } else {
            UpdateWave(bars, wave_level_);
        }
        // The glow behind the head swells with the voice
        lv_obj_set_style_image_opa(listening ? listen_glow_ : speak_glow_,
                                   40 + wave_level_ * 190 / 100, 0);
        if (!listening) {
            speak_face_.SetTalkLevel(
                wave_level_ < 5 ? -1 : static_cast<int>(Random() % (wave_level_ + 1)));
        }
    }
}

void JotapeUi::TimerCb(lv_timer_t* timer) {
    static_cast<JotapeUi*>(lv_timer_get_user_data(timer))->OnTimer();
}

void JotapeUi::HomeClickCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ == JotapeScreen::kHome) {
        self->Show(JotapeScreen::kMenu);
    }
}

void JotapeUi::BackHomeCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ == JotapeScreen::kWeather || self->current_ == JotapeScreen::kAlarms) {
        self->Show(JotapeScreen::kHome);
    }
}

void JotapeUi::MenuTileCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ != JotapeScreen::kMenu) {
        return;
    }
    lv_obj_t* tile = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = -1;
    for (int i = 0; i < static_cast<int>(JotapeMenuItem::kCount); i++) {
        if (self->menu_tiles_[i] == tile) {
            index = i;
        }
    }
    if (index < 0) {
        return;
    }
    self->menu_touched_ = lv_tick_get();
    switch (static_cast<JotapeMenuItem>(index)) {
        case JotapeMenuItem::kClock:
            self->Show(JotapeScreen::kHome);
            break;
        case JotapeMenuItem::kWeather:
            self->Show(JotapeScreen::kWeather);
            break;
        case JotapeMenuItem::kAlarms:
            self->Show(JotapeScreen::kAlarms);
            break;
        case JotapeMenuItem::kTimer:
            self->Show(JotapeScreen::kTimer);
            break;
        default:
            if (self->on_menu_item) {
                self->on_menu_item(static_cast<JotapeMenuItem>(index));
            } else {
                self->ShowToast("Próximamente", 1500);
            }
            break;
    }
}

void JotapeUi::TimerButtonCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    self->menu_touched_ = lv_tick_get();
    if (btn == self->timer_cancel_) {
        if (self->on_timer_cancel) {
            self->on_timer_cancel();
        }
        return;
    }
    const int minutes[] = {1, 5, 10};
    for (int i = 0; i < 3; i++) {
        if (btn == self->timer_presets_[i] && self->on_timer_start) {
            self->on_timer_start(minutes[i] * 60);
        }
    }
}

void JotapeUi::RingClickCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ == JotapeScreen::kRinging && self->on_ring_stop) {
        self->on_ring_stop();
    }
}

void JotapeUi::GestureCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    const auto cur = self->current_;
    if (cur == JotapeScreen::kHome && dir == LV_DIR_LEFT) {
        self->Show(JotapeScreen::kMenu);
    } else if (cur == JotapeScreen::kMenu && dir == LV_DIR_RIGHT) {
        self->Show(JotapeScreen::kHome);
    } else if ((cur == JotapeScreen::kWeather || cur == JotapeScreen::kAlarms ||
                cur == JotapeScreen::kTimer) &&
               dir == LV_DIR_RIGHT) {
        self->Show(JotapeScreen::kMenu);
    }
}
