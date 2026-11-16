#include "jotape_ui.h"

#include "jotape_eyes.h"
#include "jotape_helmet.h"

#include <cstdio>
#include <cstring>
#include <string>

LV_FONT_DECLARE(font_jotape_20);
LV_FONT_DECLARE(font_jotape_30);
LV_FONT_DECLARE(font_jotape_clock_80);
LV_FONT_DECLARE(font_jotape_icons_22);
LV_FONT_DECLARE(font_jotape_weather_64);
LV_FONT_DECLARE(font_jotape_temp_64);
LV_FONT_DECLARE(font_jotape_bigicons_72);

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr uint32_t kBootHoldMs = 2500;
constexpr uint32_t kMenuTimeoutMs = 20000;
constexpr uint32_t kTimerPeriodMs = 50;

// Material Symbols glyphs in font_jotape_icons_22
#define ICON_MIC "\xEE\x80\xA9"        // U+E029 mic
#define ICON_SCHEDULE "\xEE\xA2\xB5"   // U+E8B5 schedule
#define ICON_CLOUD "\xEE\x8A\xBD"      // U+E2BD cloud
#define ICON_BELL "\xEE\x9F\xB4"       // U+E7F4 notifications
#define ICON_HOURGLASS "\xEE\xA2\x8B"  // U+E88B hourglass_empty
#define ICON_NEWSPAPER "\xEE\xAE\x81"  // U+EB81 newspaper
#define ICON_SETTINGS "\xEE\xA2\xB8"   // U+E8B8 settings

lv_color_t Green() { return lv_color_hex(0x39FF88); }
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
    {ICON_SCHEDULE, "HORA Y FECHA"},  {ICON_CLOUD, "CLIMA"},        {ICON_BELL, "ALARMAS"},
    {ICON_HOURGLASS, "TEMPORIZADOR"}, {ICON_NEWSPAPER, "NOTICIAS"}, {ICON_SETTINGS, "AJUSTES"},
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

// Uppercase for ASCII and Latin-1 letters encoded in UTF-8 (á -> Á, ñ -> Ñ).
std::string Upper(const std::string& text) {
    std::string out = text;
    for (size_t i = 0; i < out.size(); i++) {
        unsigned char c = out[i];
        if (c >= 'a' && c <= 'z') {
            out[i] = static_cast<char>(c - 32);
        } else if (c == 0xC3 && i + 1 < out.size()) {
            unsigned char n = out[i + 1];
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7) {
                out[i + 1] = static_cast<char>(n - 0x20);
            }
            i++;
        }
    }
    return out;
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

void JotapeUi::AddFrame(lv_obj_t* parent, int x, int y, int w, int h, bool green_border) {
    lv_obj_t* frame = Decor(parent);
    lv_obj_set_pos(frame, x, y);
    lv_obj_set_size(frame, w, h);
    lv_obj_set_style_border_color(frame, green_border ? DimGreen() : Orange(), 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_radius(frame, 6, 0);
}

void JotapeUi::AddBracket(lv_obj_t* parent, int x, int y, bool right, bool bottom, int len) {
    const int t = 3;
    Rect(parent, right ? x - len + 1 : x, bottom ? y - t + 1 : y, len, t, Orange());
    Rect(parent, right ? x - t + 1 : x, bottom ? y - len + 1 : y, t, len, Orange());
}

void JotapeUi::Create(lv_obj_t* screen, const Fonts& fonts) {
    screen_ = screen;
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    font20_ = font_jotape_20;
    font30_ = font_jotape_30;
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

    // Toast on top of every panel
    toast_ = Decor(screen_);
    lv_obj_set_size(toast_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toast_, Orange(), 0);
    lv_obj_set_style_border_width(toast_, 2, 0);
    lv_obj_set_style_radius(toast_, 4, 0);
    lv_obj_set_style_pad_hor(toast_, 10, 0);
    lv_obj_set_style_pad_ver(toast_, 4, 0);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -12);
    toast_label_ = NewLabel(toast_, &font20_, Orange(), "");
    lv_obj_set_style_max_width(toast_label_, kWidth - 40, 0);
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(screen_, GestureCb, LV_EVENT_GESTURE, this);

    boot_started_ = lv_tick_get();
    next_blink_ = boot_started_ + 1800;
    ApplyScreen(JotapeScreen::kBoot);
    timer_ = lv_timer_create(TimerCb, kTimerPeriodMs, this);
}

void JotapeUi::UpdateFonts(const Fonts& fonts) {
    font20_.fallback = fonts.fallback_text;
    font30_.fallback = fonts.fallback_text;
    if (fonts.status_icons != nullptr) {
        lv_obj_t* labels[] = {network_label_, battery_label_, mute_label_};
        for (auto label : labels) {
            if (label != nullptr) {
                lv_obj_set_style_text_font(label, fonts.status_icons, 0);
            }
        }
    }
    // Labels cache glyph metrics: force a relayout with the new fallback.
    if (screen_ != nullptr) {
        lv_obj_report_style_change(nullptr);
    }
    status_icon_font_ = fonts.status_icons;
}

void JotapeUi::CreateBoot() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kBoot)] = NewPanel(screen_);
    boot_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(boot_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(boot_face_.root(), 0, 5, 1800);
}

void JotapeUi::CreateTerminal() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kTerminal)] = NewPanel(screen_);
    AddFrame(p, 4, 4, 232, 232, true);
    AddBracket(p, 10, 229, false, true, 14);
    AddBracket(p, 229, 229, true, true, 14);

    lv_obj_t* title = NewLabel(p, &font30_, Green(), "JOTAPE");
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_obj_set_pos(title, 14, 8);

    terminal_status_ = NewLabel(p, &font20_, Orange(), "> INICIANDO...");
    lv_obj_set_size(terminal_status_, 212, 22);
    lv_label_set_long_mode(terminal_status_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(terminal_status_, 14, 40);

    terminal_face_.Create(p, JotapeFaceSize::kSmall);
    lv_obj_align(terminal_face_.root(), LV_ALIGN_TOP_MID, 0, 60);

    terminal_message_ = NewLabel(p, &font20_, Green(), "");
    lv_obj_set_size(terminal_message_, 216, 110);
    lv_label_set_long_mode(terminal_message_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(terminal_message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(terminal_message_, 12, 122);
}

void JotapeUi::CreateHome() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kHome)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, HomeClickCb, LV_EVENT_CLICKED, this);

    date_label_ = NewLabel(p, &font20_, Green(), "");
    lv_obj_set_style_text_letter_space(date_label_, 1, 0);
    lv_obj_set_pos(date_label_, 14, 10);

    lv_obj_t* icons = Decor(p);
    lv_obj_set_size(icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 4, 0);
    lv_obj_align(icons, LV_ALIGN_TOP_RIGHT, -12, 9);
    const lv_font_t* icon_font = status_icon_font_ != nullptr ? status_icon_font_ : &font20_;
    mute_label_ = NewLabel(icons, icon_font, Green(), "");
    network_label_ = NewLabel(icons, icon_font, Green(), "");
    battery_label_ = NewLabel(icons, icon_font, Green(), "");

    clock_label_ = NewLabel(p, &font_jotape_clock_80, Green(), "--:--");
    lv_obj_set_style_text_letter_space(clock_label_, 2, 0);
    lv_obj_align(clock_label_, LV_ALIGN_TOP_MID, 0, 36);

    home_info_ = Decor(p);
    lv_obj_set_size(home_info_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(home_info_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_info_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(home_info_, 4, 0);
    lv_obj_align(home_info_, LV_ALIGN_TOP_MID, 0, 116);
    home_info_icon_ = NewLabel(home_info_, &font_jotape_icons_22, Orange(), ICON_BELL);
    home_info_text_ = NewLabel(home_info_, &font20_, Orange(), "");
    lv_obj_add_flag(home_info_, LV_OBJ_FLAG_HIDDEN);

    home_face_.Create(p, JotapeFaceSize::kBig);
    lv_obj_align(home_face_.root(), LV_ALIGN_BOTTOM_MID, 0, 26);
    AddFloat(home_face_.root(), 26, 3, 2200);
}

void JotapeUi::CreateMenu() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kMenu)] = NewPanel(screen_);

    lv_obj_t* slashes = NewLabel(p, &font30_, Orange(), "//");
    lv_obj_set_pos(slashes, 12, 6);
    lv_obj_t* title = NewLabel(p, &font30_, Green(), "FUNCIONES");
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_set_pos(title, 44, 6);
    Rect(p, 12, 42, 216, 2, DimGreen());

    for (int i = 0; i < static_cast<int>(JotapeMenuItem::kCount); i++) {
        lv_obj_t* row = lv_obj_create(p);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(row, 10, 50 + i * 31);
        lv_obj_set_size(row, 220, 28);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_border_color(row, Orange(), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_add_event_cb(row, MenuRowCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(row, MenuRowCb, LV_EVENT_CLICKED, this);
        menu_rows_[i] = row;

        menu_icons_[i] = NewLabel(row, &font_jotape_icons_22, Green(), kMenuEntries[i].icon);
        lv_obj_align(menu_icons_[i], LV_ALIGN_LEFT_MID, 8, 0);
        menu_texts_[i] = NewLabel(row, &font20_, Green(), kMenuEntries[i].text);
        lv_obj_align(menu_texts_[i], LV_ALIGN_LEFT_MID, 40, 0);
    }
    SelectMenuRow(0);
}

void JotapeUi::CreateWave(lv_obj_t* parent, lv_obj_t** bars, lv_color_t color) {
    for (int i = 0; i < 10; i++) {
        const int x = i < 5 ? 12 + i * 6 : 198 + (i - 5) * 6;
        bars[i] = Rect(parent, x, kHeight / 2 - 4, 3, 8, color);
        lv_obj_set_style_radius(bars[i], 1, 0);
        lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);  // only shown with sound
    }
}

void JotapeUi::UpdateWave(lv_obj_t** bars, int level) {
    for (int i = 0; i < 10; i++) {
        // Taller towards the head, like a sound wave; scaled by loudness
        const int idx = i < 5 ? i : 9 - i;
        const int max_h = (20 + idx * 10) * level / 100;
        const int h = 4 + (max_h > 0 ? static_cast<int>(Random() % (max_h + 1)) : 0);
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

void JotapeUi::CreateListening() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kListening)] = NewPanel(screen_);
    listen_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(listen_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(listen_face_.root(), 0, 5, 1800);
    CreateWave(p, listen_wave_, Orange());
}

void JotapeUi::CreateSpeaking() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kSpeaking)] = NewPanel(screen_);
    speak_face_.Create(p, JotapeFaceSize::kXL);
    lv_obj_align(speak_face_.root(), LV_ALIGN_CENTER, 0, 0);
    AddFloat(speak_face_.root(), 0, 5, 1800);
    CreateWave(p, speak_wave_, Green());
}

void JotapeUi::CreateWeather() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kWeather)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, WeatherClickCb, LV_EVENT_CLICKED, this);

    lv_obj_t* header = Decor(p);
    lv_obj_set_size(header, 216, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_pos(header, 12, 6);
    NewLabel(header, &font30_, Orange(), "CLIMA");
    weather_city_ = NewLabel(header, &font30_, Green(), "");
    lv_obj_set_flex_grow(weather_city_, 1);
    lv_obj_set_height(weather_city_, font_jotape_30.line_height);
    lv_label_set_long_mode(weather_city_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    Rect(p, 12, 42, 216, 2, DimGreen());

    weather_icon_ = NewLabel(p, &font_jotape_weather_64, Green(), "");
    lv_obj_set_pos(weather_icon_, 16, 52);
    weather_temp_ = NewLabel(p, &font_jotape_temp_64, Green(),
                             "--\xC2\xB0"
                             "C");
    lv_obj_align(weather_temp_, LV_ALIGN_TOP_RIGHT, -14, 56);

    weather_range_ = NewLabel(p, &font20_, Green(), "");
    lv_obj_align(weather_range_, LV_ALIGN_TOP_MID, 0, 136);
    Rect(p, 12, 166, 216, 2, DimGreen());

    weather_desc_ = NewLabel(p, &font20_, Green(), "BUSCANDO UBICACIÓN...");
    lv_obj_set_width(weather_desc_, 216);
    lv_label_set_long_mode(weather_desc_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(weather_desc_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(weather_desc_, 12, 178);
}

void JotapeUi::SetWeather(const JotapeWeatherView& weather) {
    if (weather_city_ == nullptr) {
        return;
    }
    if (weather.city.empty()) {
        lv_label_set_text(weather_city_, "");
    } else {
        lv_label_set_text(weather_city_, ("\xC2\xB7 " + Upper(weather.city)).c_str());
    }
    if (!weather.valid) {
        lv_label_set_text(weather_icon_, "");
        lv_label_set_text(weather_temp_,
                          "--\xC2\xB0"
                          "C");
        lv_label_set_text(weather_range_, "");
        lv_label_set_text(weather_desc_, Upper(weather.status).c_str());
        return;
    }
    lv_label_set_text(weather_icon_, WeatherIcon(weather.weather_code, weather.is_day));
    const bool sunny = weather.weather_code <= 1 && weather.is_day;
    lv_obj_set_style_text_color(weather_icon_, sunny ? Yellow() : Green(), 0);
    lv_label_set_text_fmt(weather_temp_,
                          "%d\xC2\xB0"
                          "C",
                          weather.temperature);
    lv_label_set_text_fmt(weather_range_,
                          "MÁX %d\xC2\xB0"
                          "C / MÍN %d\xC2\xB0"
                          "C",
                          weather.temp_max, weather.temp_min);
    lv_label_set_text(weather_desc_, Upper(weather.description).c_str());
}

void JotapeUi::WeatherClickCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ == JotapeScreen::kWeather || self->current_ == JotapeScreen::kAlarms) {
        self->Show(JotapeScreen::kHome);
    }
}

lv_obj_t* JotapeUi::NewButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_color(btn, Orange(), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_bg_color(btn, Orange(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_t* label = NewLabel(btn, &font20_, Green(), text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, TimerButtonCb, LV_EVENT_CLICKED, this);
    return btn;
}

static void AddHeader(lv_obj_t* p, const lv_font_t* font, const char* title, lv_color_t slash,
                      lv_color_t text, lv_color_t line) {
    lv_obj_t* slashes = lv_label_create(p);
    lv_obj_set_style_text_font(slashes, font, 0);
    lv_obj_set_style_text_color(slashes, slash, 0);
    lv_label_set_text(slashes, "//");
    lv_obj_set_pos(slashes, 12, 6);
    lv_obj_t* label = lv_label_create(p);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, text, 0);
    lv_label_set_text(label, title);
    lv_obj_set_pos(label, 44, 6);
    lv_obj_t* rule = lv_obj_create(p);
    lv_obj_remove_style_all(rule);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(rule, 12, 42);
    lv_obj_set_size(rule, 216, 2);
    lv_obj_set_style_bg_color(rule, line, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
}

void JotapeUi::CreateAlarms() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kAlarms)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, WeatherClickCb, LV_EVENT_CLICKED, this);  // tap: back home
    AddHeader(p, &font30_, "ALARMAS", Orange(), Green(), DimGreen());
    for (int i = 0; i < 6; i++) {
        alarm_rows_[i] = NewLabel(p, &font20_, Green(), "");
        lv_obj_set_width(alarm_rows_[i], 216);
        lv_label_set_long_mode(alarm_rows_[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(alarm_rows_[i], 14, 52 + i * 30);
    }
    alarm_empty_ = NewLabel(p, &font20_, Green(),
                            "SIN ALARMAS\n\nDECILE A JOTAPE:\n\"PONÉ UNA ALARMA\nA LAS 7:30\"");
    lv_obj_set_width(alarm_empty_, 216);
    lv_obj_set_style_text_align(alarm_empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(alarm_empty_, LV_ALIGN_TOP_MID, 0, 70);
}

void JotapeUi::CreateTimer() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kTimer)] = NewPanel(screen_);
    AddHeader(p, &font30_, "TEMPORIZADOR", Orange(), Green(), DimGreen());

    timer_count_ = NewLabel(p, &font_jotape_clock_80, Green(), "00:00");
    lv_obj_set_style_text_letter_space(timer_count_, 2, 0);
    lv_obj_align(timer_count_, LV_ALIGN_TOP_MID, 0, 44);

    Rect(p, 20, 128, 200, 6, lv_color_hex(0x0E3A22));
    timer_bar_ = Rect(p, 20, 128, 0, 6, Green());

    timer_text_ = NewLabel(p, &font20_, Green(), "ELEGÍ UN TIEMPO");
    lv_obj_set_width(timer_text_, 216);
    lv_label_set_long_mode(timer_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(timer_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(timer_text_, 12, 144);

    const char* presets[] = {"1 MIN", "5 MIN", "10 MIN"};
    for (int i = 0; i < 3; i++) {
        timer_presets_[i] = NewButton(p, presets[i], 14 + i * 72, 182, 66, 44);
    }
    timer_cancel_ = NewButton(p, "CANCELAR", 30, 182, 180, 44);
    lv_obj_add_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
}

void JotapeUi::CreateRinging() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kRinging)] = NewPanel(screen_);
    lv_obj_add_event_cb(p, RingClickCb, LV_EVENT_CLICKED, this);

    ring_frame_ = Decor(p);
    lv_obj_set_pos(ring_frame_, 4, 4);
    lv_obj_set_size(ring_frame_, 232, 232);
    lv_obj_set_style_border_color(ring_frame_, Orange(), 0);
    lv_obj_set_style_border_width(ring_frame_, 4, 0);
    lv_obj_set_style_radius(ring_frame_, 10, 0);

    ring_icon_ = NewLabel(p, &font_jotape_bigicons_72, Orange(), "\xEE\xA1\x95");
    lv_obj_align(ring_icon_, LV_ALIGN_TOP_MID, 0, 18);
    ring_title_ = NewLabel(p, &font30_, Green(), "");
    lv_obj_align(ring_title_, LV_ALIGN_TOP_MID, 0, 110);
    ring_label_ = NewLabel(p, &font20_, Green(), "");
    lv_obj_set_width(ring_label_, 210);
    lv_label_set_long_mode(ring_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ring_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ring_label_, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_t* hint = NewLabel(p, &font20_, Yellow(), "TOCÁ PARA APAGAR");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void JotapeUi::SetAlarms(const JotapeAlarmsView& view) {
    alarms_view_ = view;
    if (home_info_ == nullptr) {
        return;
    }
    // Home: running timer first, else next alarm
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

    // Alarm list
    for (int i = 0; i < 6; i++) {
        if (i < static_cast<int>(view.alarms.size())) {
            const auto& a = view.alarms[i];
            std::string text = a.time + "  " + a.repeat;
            if (!a.label.empty()) {
                text += "  " + Upper(a.label);
            }
            lv_label_set_text(alarm_rows_[i], text.c_str());
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

    // Timer
    if (view.timer_active) {
        lv_label_set_text_fmt(timer_count_, "%02d:%02d", view.timer_remaining_s / 60,
                              view.timer_remaining_s % 60);
        const int total = view.timer_total_s > 0 ? view.timer_total_s : 1;
        lv_obj_set_width(timer_bar_, 200 * (total - view.timer_remaining_s) / total);
        lv_label_set_text(timer_text_,
                          view.timer_label.empty() ? "EN MARCHA" : Upper(view.timer_label).c_str());
        for (auto b : timer_presets_) {
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(timer_count_, "00:00");
        lv_obj_set_width(timer_bar_, 0);
        lv_label_set_text(timer_text_, "ELEGÍ UN TIEMPO");
        for (auto b : timer_presets_) {
            lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(timer_cancel_, LV_OBJ_FLAG_HIDDEN);
    }

    // Ringing
    lv_label_set_text(ring_icon_, view.ringing_timer ? "\xEE\xA2\x8B" : "\xEE\xA1\x95");
    if (view.ringing_timer) {
        lv_label_set_text(ring_title_, "¡TIEMPO!");
    } else {
        lv_label_set_text_fmt(ring_title_, "ALARMA %s", view.ring_title.c_str());
    }
    lv_label_set_text(ring_label_, Upper(view.ring_label).c_str());
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
    if (screen == JotapeScreen::kMenu) {
        SelectMenuRow(0);
    } else if (screen == JotapeScreen::kHome) {
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
    lv_label_set_text_fmt(terminal_status_, "> %s", status);
}

void JotapeUi::SetMessage(const char* content) {
    if (terminal_message_ == nullptr) {
        return;
    }
    // The pixel font has no full-width comma (used by some server/locale texts).
    std::string normalized = content != nullptr ? content : "";
    for (size_t pos = normalized.find("\xEF\xBC\x8C"); pos != std::string::npos;
         pos = normalized.find("\xEF\xBC\x8C", pos)) {
        normalized.replace(pos, 3, ", ");
    }
    const char* text = normalized.c_str();
    lv_label_set_text(terminal_message_, text);
}

void JotapeUi::SetSubtitleHidden(bool hidden) {
    // The JOTAPE layout shows no subtitles; kept for the Display interface.
    subtitle_hidden_ = hidden;
}

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

void JotapeUi::SelectMenuRow(int index) {
    menu_selected_ = index;
    for (int i = 0; i < static_cast<int>(JotapeMenuItem::kCount); i++) {
        const bool selected = i == index;
        lv_obj_set_style_border_width(menu_rows_[i], selected ? 2 : 0, 0);
        lv_obj_set_style_text_color(menu_icons_[i], selected ? Yellow() : Green(), 0);
        lv_obj_set_style_text_color(menu_texts_[i], selected ? Yellow() : Green(), 0);
        if (selected) {
            lv_label_set_text_fmt(menu_texts_[i], "> %s", kMenuEntries[i].text);
        } else {
            lv_label_set_text(menu_texts_[i], kMenuEntries[i].text);
        }
    }
}

void JotapeUi::RefreshClock() {
    if (clock_label_ == nullptr) {
        return;
    }
    struct tm now = {};
    if (!time_provider || !time_provider(&now)) {
        if (last_clock_min_ != -2) {
            last_clock_min_ = -2;
            lv_label_set_text(clock_label_, "--:--");
            lv_label_set_text(date_label_, "");
        }
        return;
    }
    const int minute = now.tm_hour * 60 + now.tm_min;
    if (minute == last_clock_min_) {
        return;
    }
    last_clock_min_ = minute;
    lv_label_set_text_fmt(clock_label_, "%02d:%02d", now.tm_hour, now.tm_min);
    const int wday = now.tm_wday >= 0 && now.tm_wday < 7 ? now.tm_wday : 0;
    const int mon = now.tm_mon >= 0 && now.tm_mon < 12 ? now.tm_mon : 0;
    lv_label_set_text_fmt(date_label_, "%s %02d %s", kDays[wday], now.tm_mday, kMonths[mon]);
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

    if (current_ == JotapeScreen::kRinging) {
        const bool on = (now / 400) % 2 == 0;
        lv_obj_set_style_border_opa(ring_frame_, on ? LV_OPA_COVER : LV_OPA_20, 0);
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
        lv_obj_t** bars = current_ == JotapeScreen::kListening ? listen_wave_ : speak_wave_;
        if (wave_level_ < 5) {
            HideWave(bars);
        } else {
            UpdateWave(bars, wave_level_);
        }
        if (current_ == JotapeScreen::kSpeaking) {
            // Eyes pulse only while sound is actually coming out
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

void JotapeUi::MenuRowCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    if (self->current_ != JotapeScreen::kMenu) {
        return;
    }
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int index = -1;
    for (int i = 0; i < static_cast<int>(JotapeMenuItem::kCount); i++) {
        if (self->menu_rows_[i] == row) {
            index = i;
        }
    }
    if (index < 0) {
        return;
    }
    self->menu_touched_ = lv_tick_get();
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        self->SelectMenuRow(index);
        return;
    }
    auto item = static_cast<JotapeMenuItem>(index);
    if (item == JotapeMenuItem::kClock) {
        self->Show(JotapeScreen::kHome);
    } else if (item == JotapeMenuItem::kWeather) {
        self->Show(JotapeScreen::kWeather);
    } else if (item == JotapeMenuItem::kAlarms) {
        self->Show(JotapeScreen::kAlarms);
    } else if (item == JotapeMenuItem::kTimer) {
        self->Show(JotapeScreen::kTimer);
    } else if (self->on_menu_item) {
        self->on_menu_item(item);
    } else {
        self->ShowToast("PRÓXIMAMENTE", 1500);
    }
}

void JotapeUi::GestureCb(lv_event_t* e) {
    auto self = static_cast<JotapeUi*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (self->current_ == JotapeScreen::kHome && dir == LV_DIR_LEFT) {
        self->Show(JotapeScreen::kMenu);
    } else if (self->current_ == JotapeScreen::kMenu && dir == LV_DIR_RIGHT) {
        self->Show(JotapeScreen::kHome);
    } else if ((self->current_ == JotapeScreen::kWeather ||
                self->current_ == JotapeScreen::kAlarms ||
                self->current_ == JotapeScreen::kTimer) &&
               dir == LV_DIR_RIGHT) {
        self->Show(JotapeScreen::kMenu);
    }
}
