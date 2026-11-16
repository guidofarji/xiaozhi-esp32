#ifndef JOTAPE_UI_H
#define JOTAPE_UI_H

// JOTAPE retro-terminal user interface.
//
// Pure LVGL: no ESP-IDF or application dependencies, so it can also be built
// on a host machine to render preview screenshots. JotapeDisplay adapts it to
// the firmware Display interface.

#include <lvgl.h>

#include <ctime>
#include <functional>
#include <string>
#include <vector>

enum class JotapeScreen {
    kBoot,       // Splash shown at power-on
    kTerminal,   // Connecting, Wi-Fi setup, activation, upgrade, errors
    kHome,       // Clock
    kMenu,       // Functions list
    kListening,  // Listening to the user
    kSpeaking,   // JOTAPE talking
    kWeather,    // Current weather
    kAlarms,     // Alarm list
    kTimer,      // Countdown timer
    kRinging,    // An alarm or the timer is ringing
};

enum class JotapeMenuItem {
    kClock = 0,
    kWeather,
    kAlarms,
    kTimer,
    kNews,
    kSettings,
    kCount,
};

// Weather shown on the weather screen (filled by the board).
struct JotapeWeatherView {
    bool valid = false;
    std::string city;
    std::string status;       // shown when !valid
    std::string description;  // e.g. "Parcialmente nublado"
    int temperature = 0;
    int temp_max = 0;
    int temp_min = 0;
    int weather_code = 0;  // WMO code
    bool is_day = true;
};

struct JotapeAlarmItemView {
    std::string time;    // "07:30"
    std::string repeat;  // "LUN-VIE"
    std::string label;
};

struct JotapeAlarmsView {
    std::vector<JotapeAlarmItemView> alarms;
    std::string next_alarm;  // "07:30" or empty
    bool timer_active = false;
    int timer_remaining_s = 0;
    int timer_total_s = 0;
    std::string timer_label;
    bool ringing = false;
    bool ringing_timer = false;
    std::string ring_title;
    std::string ring_label;
};

enum class JotapeFaceSize { kSmall, kBig, kXL };

// JOTAPE's head: a fixed helmet image with glowing eyes drawn on its empty
// face screen. Eyes are either rounded bars/dots (animatable) or small tinted
// shape images (^, > <, hearts, spirals...).
class JotapeFace {
public:
    void Create(lv_obj_t* parent, JotapeFaceSize size);
    lv_obj_t* root() const { return root_; }
    void SetExpression(const char* emotion);
    void SetEyesClosed(bool closed);  // blink
    void SetTalkLevel(int percent);   // 0..100 while talking, -1 to stop
    void SetListening(bool listening);
    void SetLook(int dx);  // glance sideways, in pixels

private:
    struct Eye {
        const lv_image_dsc_t* image = nullptr;  // nullptr = draw a bar
        int w_pct = 100;                        // bar size, percent of base
        int h_pct = 100;
        bool dot = false;  // round dot instead of a pill
    };
    void Apply();
    const lv_image_dsc_t* Shape(const char* name) const;

    JotapeFaceSize size_ = JotapeFaceSize::kBig;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* bars_[2] = {};
    lv_obj_t* images_[2] = {};
    lv_obj_t* tear_ = nullptr;
    lv_obj_t* question_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;
    Eye eyes_[2];
    int eye_dy_ = 0;  // percent of screen height
    bool show_tear_ = false;
    bool show_question_ = false;
    lv_color_t tint_;
    bool closed_ = false;
    bool listening_ = false;
    int talk_ = -1;
    int look_dx_ = 0;
};

class JotapeUi {
public:
    struct Fonts {
        // Fallback for characters the pixel font lacks (CJK, emoji...).
        const lv_font_t* fallback_text = nullptr;
        // Font used by the Wi-Fi / battery / mute status icons.
        const lv_font_t* status_icons = nullptr;
    };

    void Create(lv_obj_t* screen, const Fonts& fonts);
    void UpdateFonts(const Fonts& fonts);

    // Requests a screen. While the boot splash is held, the request is
    // applied once the splash time is over.
    void Show(JotapeScreen screen);
    JotapeScreen current() const { return current_; }

    void SetStatus(const char* status);
    void SetMessage(const char* content);
    void SetEmotion(const char* emotion);
    void ShowToast(const char* text, uint32_t duration_ms);
    void SetSubtitleHidden(bool hidden);
    void SetWeather(const JotapeWeatherView& weather);
    void SetAlarms(const JotapeAlarmsView& alarms);

    // Called every second to refresh the clock; return false if the time is
    // not known yet.
    std::function<bool(struct tm*)> time_provider;
    // Called when a menu entry is tapped (except kClock, handled internally).
    std::function<void(JotapeMenuItem)> on_menu_item;
    // Current loudness 0..100 of the voice being heard or spoken (0 = silence).
    // The sound wave only shows while this is above a small threshold.
    std::function<int()> sound_level;
    // Touch actions on the timer and ringing screens.
    std::function<void(int seconds)> on_timer_start;
    std::function<void()> on_timer_cancel;
    std::function<void()> on_ring_stop;

    lv_obj_t* network_label() const { return network_label_; }
    lv_obj_t* battery_label() const { return battery_label_; }
    lv_obj_t* mute_label() const { return mute_label_; }

    // Test hook: forces the boot splash to end now.
    void EndBootHold();
    // Test hook: advances animations by one step.
    void Tick();

private:
    lv_obj_t* NewPanel(lv_obj_t* parent);
    lv_obj_t* NewLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
    // Frosted glass card: tinted gradient, thin translucent border, top sheen, soft glow.
    lv_obj_t* Glass(lv_obj_t* parent, int x, int y, int w, int h, int radius, lv_color_t tint);
    void TintGlass(lv_obj_t* glass, lv_color_t tint);
    lv_obj_t* NewButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h);
    void CreateWave(lv_obj_t* parent, lv_obj_t** bars, lv_color_t color);
    void UpdateWave(lv_obj_t** bars, int level);
    void HideWave(lv_obj_t** bars);
    static void AddFloat(lv_obj_t* obj, int base_y, int amplitude, uint32_t period_ms);

    void CreateBoot();
    void CreateTerminal();
    void CreateHome();
    void CreateMenu();
    void CreateListening();
    void CreateSpeaking();
    void CreateWeather();
    void CreateAlarms();
    void CreateTimer();
    void CreateRinging();

    void ApplyScreen(JotapeScreen screen);
    void RefreshClock();
    void OnTimer();
    JotapeFace* ActiveFace();
    uint32_t Random();

    static void TimerCb(lv_timer_t* timer);
    static void HomeClickCb(lv_event_t* e);
    static void BackHomeCb(lv_event_t* e);
    static void MenuTileCb(lv_event_t* e);
    static void GestureCb(lv_event_t* e);
    static void TimerButtonCb(lv_event_t* e);
    static void RingClickCb(lv_event_t* e);

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* panels_[10] = {};
    JotapeScreen current_ = JotapeScreen::kBoot;
    JotapeScreen pending_ = JotapeScreen::kBoot;
    bool boot_hold_ = true;
    uint32_t boot_started_ = 0;
    uint32_t menu_touched_ = 0;
    uint32_t tick_count_ = 0;
    bool subtitle_hidden_ = false;

    const lv_font_t* status_icon_font_ = nullptr;
    lv_font_t text14_;  // Inter with fallback for other scripts
    lv_font_t text18_;
    lv_font_t dots3_;  // dot-matrix titles with Inter fallback

    JotapeFace boot_face_;
    JotapeFace terminal_face_;
    JotapeFace home_face_;
    JotapeFace listen_face_;
    JotapeFace speak_face_;

    lv_obj_t* terminal_status_ = nullptr;
    lv_obj_t* terminal_message_ = nullptr;

    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* clock_hours_ = nullptr;
    lv_obj_t* clock_minutes_ = nullptr;
    lv_obj_t* network_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* mute_label_ = nullptr;
    lv_obj_t* home_info_ = nullptr;
    lv_obj_t* home_info_icon_ = nullptr;
    lv_obj_t* home_info_text_ = nullptr;

    lv_obj_t* menu_tiles_[static_cast<int>(JotapeMenuItem::kCount)] = {};

    lv_obj_t* weather_card_ = nullptr;
    lv_obj_t* weather_city_ = nullptr;
    lv_obj_t* weather_icon_ = nullptr;
    lv_obj_t* weather_temp_ = nullptr;
    lv_obj_t* weather_max_ = nullptr;
    lv_obj_t* weather_min_ = nullptr;
    lv_obj_t* weather_desc_ = nullptr;

    JotapeAlarmsView alarms_view_;
    lv_obj_t* alarm_rows_[5] = {};
    lv_obj_t* alarm_times_[5] = {};
    lv_obj_t* alarm_infos_[5] = {};
    lv_obj_t* alarm_empty_ = nullptr;

    lv_obj_t* timer_arc_ = nullptr;
    lv_obj_t* timer_count_ = nullptr;
    lv_obj_t* timer_text_ = nullptr;
    lv_obj_t* timer_presets_[3] = {};
    lv_obj_t* timer_cancel_ = nullptr;

    lv_obj_t* ring_icon_ = nullptr;
    lv_obj_t* ring_title_ = nullptr;
    lv_obj_t* ring_label_ = nullptr;

    lv_obj_t* listen_wave_[10] = {};
    lv_obj_t* speak_wave_[10] = {};
    lv_obj_t* toast_ = nullptr;
    lv_obj_t* toast_label_ = nullptr;
    uint32_t toast_until_ = 0;
    uint32_t next_blink_ = 0;
    uint32_t next_look_ = 0;
    uint32_t look_end_ = 0;
    uint32_t blink_end_ = 0;
    int last_clock_min_ = -1;
    uint32_t rng_ = 0x4A07A9E1;
    int wave_level_ = 0;  // smoothed sound level shown by the wave
    lv_timer_t* timer_ = nullptr;
};

#endif  // JOTAPE_UI_H
