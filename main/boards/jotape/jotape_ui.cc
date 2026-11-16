#include "jotape_ui.h"

#include <cstdio>
#include <cstring>

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
lv_color_t DarkGreen() { return lv_color_hex(0x04140B); }
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

void JotapeFace::Create(lv_obj_t* parent, int size) {
    size_ = size;
    const int head_h = size * 82 / 100;

    root_ = Decor(parent);
    lv_obj_set_size(root_, size, head_h);
    lv_obj_set_style_bg_color(root_, DarkGreen(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root_, Green(), 0);
    lv_obj_set_style_border_width(root_, size >= 90 ? 3 : 2, 0);
    lv_obj_set_style_radius(root_, size * 26 / 100, 0);

    // Visor cap on top of the head
    lv_obj_t* cap = Decor(root_);
    lv_obj_set_size(cap, size * 36 / 100, size * 12 / 100);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, size * 5 / 100);
    lv_obj_set_style_border_color(cap, Green(), 0);
    lv_obj_set_style_border_width(cap, 2, 0);
    lv_obj_set_style_radius(cap, 4, 0);
    lv_obj_t* cap_line = Decor(cap);
    lv_obj_set_size(cap_line, size * 18 / 100, 2);
    lv_obj_center(cap_line);
    lv_obj_set_style_bg_color(cap_line, DimGreen(), 0);
    lv_obj_set_style_bg_opa(cap_line, LV_OPA_COVER, 0);

    for (int i = 0; i < 2; i++) {
        eyes_[i] = Decor(root_);
        lv_obj_set_style_bg_color(eyes_[i], lv_color_hex(0x0B2E1A), 0);
        lv_obj_set_style_bg_opa(eyes_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(eyes_[i], Green(), 0);
        lv_obj_set_style_border_width(eyes_[i], size >= 90 ? 3 : 2, 0);
        lv_obj_set_style_radius(eyes_[i], LV_RADIUS_CIRCLE, 0);

        pupils_[i] = Decor(eyes_[i]);
        lv_obj_set_style_bg_color(pupils_[i], Green(), 0);
        lv_obj_set_style_bg_opa(pupils_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pupils_[i], LV_RADIUS_CIRCLE, 0);

        glints_[i] = Decor(eyes_[i]);
        lv_obj_set_style_bg_color(glints_[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(glints_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(glints_[i], LV_RADIUS_CIRCLE, 0);
    }

    mouth_ = Decor(root_);
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(0x0B2E1A), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mouth_, Green(), 0);
    lv_obj_set_style_border_width(mouth_, 2, 0);
    for (int i = 0; i < 2; i++) {
        grille_[i] = Decor(mouth_);
        lv_obj_set_style_bg_color(grille_[i], Green(), 0);
        lv_obj_set_style_bg_opa(grille_[i], LV_OPA_COVER, 0);
    }

    ApplyEyes();
    ApplyMouth();
}

void JotapeFace::ApplyEyes() {
    if (root_ == nullptr) {
        return;
    }
    const int base = size_ * 27 / 100;
    const int scale = big_eyes_ ? 118 : 100;
    int w = base * eye_w_pct_ / 100 * scale / 100;
    int h = base * eye_h_pct_ / 100 * scale / 100;
    const bool closed = closed_ || sleepy_;
    if (closed) {
        h = LV_MAX(3, size_ / 28);
    }
    const bool show_pupil = !closed && h >= base * 45 / 100;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(eyes_[i], w, h);
        const int dx = (i == 0 ? -1 : 1) * size_ * 21 / 100;
        lv_obj_align(eyes_[i], LV_ALIGN_CENTER, dx, -size_ * 4 / 100 + eye_dy_);

        const int pupil = LV_MIN(w, h) * 55 / 100;
        lv_obj_set_size(pupils_[i], pupil, pupil);
        lv_obj_align(pupils_[i], LV_ALIGN_CENTER, pupil_dx_ * pupil / 100, pupil_dy_ * pupil / 100);
        const int glint = LV_MAX(2, LV_MIN(w, h) * 18 / 100);
        lv_obj_set_size(glints_[i], glint, glint);
        lv_obj_align(glints_[i], LV_ALIGN_CENTER, -w * 14 / 100, -h * 14 / 100);
        if (show_pupil) {
            lv_obj_remove_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(glints_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(glints_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void JotapeFace::ApplyMouth() {
    if (root_ == nullptr) {
        return;
    }
    const int base_w = size_ * 44 / 100;
    const int base_h = size_ * 15 / 100;
    const int w = base_w * mouth_w_pct_ / 100;
    int h;
    if (talk_pct_ >= 0) {
        h = base_h * (35 + talk_pct_ * 115 / 100) / 100;
    } else {
        h = base_h * mouth_h_pct_ / 100;
    }
    h = LV_MAX(4, h);
    lv_obj_set_size(mouth_, w, h);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, size_ * 25 / 100);
    lv_obj_set_style_radius(mouth_, w <= h + 2 ? LV_RADIUS_CIRCLE : 4, 0);

    const bool show_grille = h >= 10 && w > h + 2;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(grille_[i], LV_MAX(2, w - 10), 2);
        lv_obj_align(grille_[i], LV_ALIGN_TOP_MID, 0, (h - 4) * (i + 1) / 3 - 1);
        if (show_grille) {
            lv_obj_remove_flag(grille_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(grille_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void JotapeFace::SetExpression(const char* emotion) {
    eye_w_pct_ = 100;
    eye_h_pct_ = 100;
    eye_dy_ = 0;
    pupil_dx_ = 0;
    pupil_dy_ = 0;
    mouth_w_pct_ = 100;
    mouth_h_pct_ = 100;
    sleepy_ = false;

    const char* e = emotion != nullptr ? emotion : "neutral";
    if (Is(e, "happy") || Is(e, "laughing") || Is(e, "funny") || Is(e, "loving") ||
        Is(e, "confident") || Is(e, "cool") || Is(e, "relaxed") || Is(e, "delicious") ||
        Is(e, "kissy") || Is(e, "winking") || Is(e, "silly")) {
        eye_w_pct_ = 108;
        eye_h_pct_ = 66;
        pupil_dy_ = -12;
        mouth_w_pct_ = 120;
        mouth_h_pct_ = 150;
    } else if (Is(e, "sad") || Is(e, "crying")) {
        eye_h_pct_ = 80;
        eye_dy_ = size_ * 4 / 100;
        pupil_dy_ = 25;
        mouth_w_pct_ = 65;
        mouth_h_pct_ = 70;
    } else if (Is(e, "angry")) {
        eye_h_pct_ = 62;
        eye_dy_ = size_ * 2 / 100;
        mouth_w_pct_ = 80;
        mouth_h_pct_ = 60;
    } else if (Is(e, "surprised") || Is(e, "shocked")) {
        eye_w_pct_ = 118;
        eye_h_pct_ = 118;
        mouth_w_pct_ = 40;
        mouth_h_pct_ = 125;
    } else if (Is(e, "thinking") || Is(e, "confused")) {
        pupil_dx_ = 30;
        pupil_dy_ = -30;
        mouth_w_pct_ = 60;
    } else if (Is(e, "embarrassed")) {
        eye_h_pct_ = 70;
        pupil_dy_ = 20;
        mouth_w_pct_ = 60;
        mouth_h_pct_ = 60;
    } else if (Is(e, "sleepy")) {
        sleepy_ = true;
        mouth_w_pct_ = 50;
        mouth_h_pct_ = 60;
    }
    ApplyEyes();
    ApplyMouth();
}

void JotapeFace::SetEyesClosed(bool closed) {
    if (closed_ == closed) {
        return;
    }
    closed_ = closed;
    ApplyEyes();
}

void JotapeFace::SetMouthOpen(int percent) {
    talk_pct_ = percent < 0 ? -1 : LV_MIN(percent, 100);
    ApplyMouth();
}

void JotapeFace::SetBigEyes(bool big) {
    if (big_eyes_ == big) {
        return;
    }
    big_eyes_ = big;
    ApplyEyes();
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
    AddFrame(p, 4, 4, 232, 232, true);
    AddBracket(p, 10, 10, false, false, 18);
    AddBracket(p, 229, 10, true, false, 18);
    AddBracket(p, 10, 229, false, true, 18);
    AddBracket(p, 229, 229, true, true, 18);

    lv_obj_t* title = NewLabel(p, &font30_, Green(), "JOTAPE");
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 3, 14);

    boot_face_.Create(p, 104);
    lv_obj_align(boot_face_.root(), LV_ALIGN_CENTER, 0, -4);

    lv_obj_t* l1 = NewLabel(p, &font20_, Green(), "TERMINAL CONVERSACIONAL");
    lv_obj_align(l1, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_t* l2 = NewLabel(p, &font20_, Green(), "SISTEMA v1.0");
    lv_obj_align(l2, LV_ALIGN_BOTTOM_MID, 0, -16);
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
    lv_obj_set_width(terminal_status_, 212);
    lv_label_set_long_mode(terminal_status_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(terminal_status_, 14, 40);

    terminal_face_.Create(p, 76);
    lv_obj_align(terminal_face_.root(), LV_ALIGN_TOP_MID, 0, 66);

    terminal_message_ = NewLabel(p, &font20_, Green(), "");
    lv_obj_set_size(terminal_message_, 212, 92);
    lv_label_set_long_mode(terminal_message_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(terminal_message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(terminal_message_, 14, 136);
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

    AddBracket(p, 26, 150, false, false, 16);
    AddBracket(p, 213, 150, true, false, 16);
    lv_obj_t* left = NewLabel(p, &font20_, Orange(), "//////");
    lv_obj_align(left, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_obj_t* right = NewLabel(p, &font20_, Orange(), "//////");
    lv_obj_align(right, LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    home_face_.Create(p, 104);
    lv_obj_align(home_face_.root(), LV_ALIGN_BOTTOM_MID, 0, 12);
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

void JotapeUi::CreateListening() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kListening)] = NewPanel(screen_);

    lv_obj_t* header = Decor(p);
    lv_obj_set_size(header, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 4, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);
    NewLabel(header, &font_jotape_icons_22, Yellow(), ICON_MIC);
    lv_obj_t* text = NewLabel(header, &font30_, Yellow(), "ESCUCHANDO...");
    lv_obj_set_style_text_letter_space(text, 1, 0);

    AddFrame(p, 14, 48, 212, 184, true);
    AddBracket(p, 18, 52, false, false, 14);
    AddBracket(p, 221, 52, true, false, 14);
    AddBracket(p, 18, 227, false, true, 14);
    AddBracket(p, 221, 227, true, true, 14);

    listen_face_.Create(p, 112);
    lv_obj_align(listen_face_.root(), LV_ALIGN_TOP_MID, 0, 94);

    for (int i = 0; i < 10; i++) {
        const int x = i < 5 ? 24 + i * 6 : 190 + (i - 5) * 6;
        bars_[i] = Rect(p, x, 136, 3, 8, Orange());
        lv_obj_set_style_radius(bars_[i], 1, 0);
    }
}

void JotapeUi::CreateSpeaking() {
    lv_obj_t* p = panels_[static_cast<int>(JotapeScreen::kSpeaking)] = NewPanel(screen_);

    lv_obj_t* title = NewLabel(p, &font30_, Green(), "> JOTAPE");
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_pos(title, 12, 4);
    lv_obj_t* tag = NewLabel(p, &font20_, Orange(), "HABLANDO");
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -12, 12);

    speak_face_.Create(p, 100);
    lv_obj_align(speak_face_.root(), LV_ALIGN_TOP_MID, 0, 42);

    subtitle_box_ = Decor(p);
    lv_obj_set_pos(subtitle_box_, 10, 134);
    lv_obj_set_size(subtitle_box_, 220, 100);
    lv_obj_set_style_border_color(subtitle_box_, DimGreen(), 0);
    lv_obj_set_style_border_width(subtitle_box_, 2, 0);
    lv_obj_set_style_radius(subtitle_box_, 4, 0);
    lv_obj_set_style_bg_color(subtitle_box_, lv_color_hex(0x031008), 0);
    lv_obj_set_style_bg_opa(subtitle_box_, LV_OPA_COVER, 0);

    subtitle_label_ = NewLabel(subtitle_box_, &font20_, Green(), "");
    lv_obj_set_size(subtitle_label_, 204, 88);
    lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(subtitle_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(subtitle_box_, LV_OBJ_FLAG_HIDDEN);
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
        speak_face_.SetMouthOpen(-1);
    }
    current_ = screen;

    if (screen == JotapeScreen::kMenu) {
        menu_touched_ = lv_tick_get();
        SelectMenuRow(0);
    } else if (screen == JotapeScreen::kHome) {
        last_clock_min_ = -1;
        RefreshClock();
    }
    listen_face_.SetBigEyes(screen == JotapeScreen::kListening);
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
    const char* text = content != nullptr ? content : "";
    lv_label_set_text(terminal_message_, text);
    lv_label_set_text(subtitle_label_, text);
    if (text[0] == '\0' || subtitle_hidden_) {
        lv_obj_add_flag(subtitle_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(subtitle_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void JotapeUi::SetSubtitleHidden(bool hidden) {
    subtitle_hidden_ = hidden;
    if (subtitle_box_ == nullptr) {
        return;
    }
    const char* text = lv_label_get_text(subtitle_label_);
    if (hidden || text == nullptr || text[0] == '\0') {
        lv_obj_add_flag(subtitle_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(subtitle_box_, LV_OBJ_FLAG_HIDDEN);
    }
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

    if (current_ == JotapeScreen::kListening) {
        for (int i = 0; i < 10; i++) {
            const int h = 6 + Random() % 42;
            lv_obj_set_height(bars_[i], h);
            lv_obj_set_y(bars_[i], 140 - h / 2);
        }
    } else if (current_ == JotapeScreen::kSpeaking) {
        speak_face_.SetMouthOpen(Random() % 101);
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
