#include "jotape_display.h"

#include <esp_log.h>

#include <cstring>
#include <ctime>

#include "application.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "JotapeDisplay"

JotapeDisplay::JotapeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

JotapeUi::Fonts JotapeDisplay::CurrentFonts() const {
    JotapeUi::Fonts fonts;
    auto theme = static_cast<LvglTheme*>(current_theme_);
    if (theme != nullptr) {
        if (theme->text_font() != nullptr) {
            fonts.fallback_text = theme->text_font()->font();
        }
        if (theme->icon_font() != nullptr) {
            fonts.status_icons = theme->icon_font()->font();
        }
    }
    return fonts;
}

void JotapeDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    Display::SetupUI();
    DisplayLockGuard lock(this);

    ui_.time_provider = [](struct tm* out) {
        time_t now = time(nullptr);
        localtime_r(&now, out);
        return out->tm_year >= 2025 - 1900;
    };
    ui_.sound_level = sound_level_;
    ui_.Create(lv_screen_active(), CurrentFonts());
    ui_.SetWeather(weather_);
    ui_.SetAlarms(alarms_);

    // The base class updates these icons from UpdateStatusBar().
    network_label_ = ui_.network_label();
    battery_label_ = ui_.battery_label();
    mute_label_ = ui_.mute_label();
}

void JotapeDisplay::SyncScreen() {
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateIdle: {
            const auto current = ui_.current();
            if (alarms_.ringing) {
                ui_.Show(JotapeScreen::kRinging);
            } else if (current != JotapeScreen::kMenu && current != JotapeScreen::kWeather &&
                       current != JotapeScreen::kAlarms && current != JotapeScreen::kTimer) {
                ui_.Show(JotapeScreen::kHome);
            }
            break;
        }
        case kDeviceStateConnecting:
        case kDeviceStateListening:
            ui_.Show(JotapeScreen::kListening);
            break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            ui_.Show(JotapeScreen::kSpeaking);
            break;
        default:
            ui_.Show(JotapeScreen::kTerminal);
            break;
    }
}

void JotapeDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return;
    }
    ui_.SetStatus(status);
    SyncScreen();
}

void JotapeDisplay::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return;
    }
    ui_.ShowToast(notification, duration_ms > 0 ? duration_ms : 3000);
}

bool JotapeDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return false;
    }
    ui_.SetEmotion(emotion);
    return true;
}

void JotapeDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return;
    }
    const bool has_text = content != nullptr && content[0] != '\0';
    // Alerts raised while idle would be lost behind the clock: show a toast.
    if (has_text && role != nullptr && strcmp(role, "system") == 0 &&
        Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        ui_.ShowToast(content, 4000);
        return;
    }
    ui_.SetMessage(content);
}

void JotapeDisplay::RemoveChatMessage(const char* role, const char* content) {
    (void)role;
    (void)content;
    // The JOTAPE layout has a single text slot; it is replaced by the next message.
}

void JotapeDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return;
    }
    ui_.SetMessage("");
}

void JotapeDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    // Keep the JOTAPE colors; only follow font changes (the text font is
    // replaced when assets load, and the old one is freed afterwards).
    Display::SetTheme(theme);
    if (setup_ui_called_) {
        ui_.UpdateFonts(CurrentFonts());
    }
}

void JotapeDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    ui_.SetSubtitleHidden(hide);
}

void JotapeDisplay::SetSoundLevelProvider(std::function<int()> provider) {
    DisplayLockGuard lock(this);
    sound_level_ = provider;
    ui_.sound_level = provider;
}

void JotapeDisplay::SetAlarms(const JotapeAlarmsView& alarms) {
    DisplayLockGuard lock(this);
    alarms_ = alarms;
    if (setup_ui_called_) {
        ui_.SetAlarms(alarms_);
        SyncScreen();
    }
}

void JotapeDisplay::SetAlarmActions(std::function<void(int)> timer_start,
                                    std::function<void()> timer_cancel,
                                    std::function<void()> ring_stop) {
    DisplayLockGuard lock(this);
    ui_.on_timer_start = timer_start;
    ui_.on_timer_cancel = timer_cancel;
    ui_.on_ring_stop = ring_stop;
}

void JotapeDisplay::SetWeather(const JotapeWeatherView& weather) {
    DisplayLockGuard lock(this);
    weather_ = weather;
    if (setup_ui_called_) {
        ui_.SetWeather(weather_);
    }
}

void JotapeDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    // No camera on this board; nothing to preview.
    (void)image;
}
