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

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr uint32_t kBootHoldMs = 2500;
constexpr uint32_t kMenuTimeoutMs = 20000;
constexpr uint32_t kTimerPeriodMs = 100;

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
    }
}

void JotapeUi::UpdateWave(lv_obj_t** bars) {
    for (int i = 0; i < 10; i++) {
        // Taller towards the head, like a sound wave
        const int idx = i < 5 ? i : 9 - i;
        const int max_h = 20 + idx * 10;
        const int h = 6 + Random() % max_h;
        lv_obj_set_height(bars[i], h);
        lv_obj_set_y(bars[i], kHeight / 2 - h / 2);
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

    if (screen == JotapeScreen::kMenu) {
        menu_touched_ = lv_tick_get();
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

    if (current_ == JotapeScreen::kMenu && lv_tick_diff(now, menu_touched_) >= kMenuTimeoutMs) {
        Show(JotapeScreen::kHome);
    }

    if (current_ == JotapeScreen::kHome && tick_count_ % 10 == 0) {
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

    if (current_ == JotapeScreen::kListening) {
        UpdateWave(listen_wave_);
    } else if (current_ == JotapeScreen::kSpeaking) {
        UpdateWave(speak_wave_);
        speak_face_.SetTalkLevel(Random() % 101);
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
    }
}
