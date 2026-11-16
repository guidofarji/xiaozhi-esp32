#ifndef JOTAPE_DISPLAY_H
#define JOTAPE_DISPLAY_H

#include "display/lcd_display.h"
#include "jotape_ui.h"

#include <functional>

// SPI LCD display that replaces the stock layout with the JOTAPE terminal UI.
class JotapeDisplay : public SpiLcdDisplay {
public:
    JotapeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);

    void SetupUI() override;
    void SetStatus(const char* status) override;
    using LvglDisplay::ShowNotification;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    bool SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetTheme(Theme* theme) override;
    void SetHideSubtitle(bool hide) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

    // Loudness 0..100 used to animate the sound wave (called from the UI task).
    void SetSoundLevelProvider(std::function<int()> provider);
    // Updates the weather screen (any task).
    void SetWeather(const JotapeWeatherView& weather);

private:
    JotapeUi::Fonts CurrentFonts() const;
    void SyncScreen();

    JotapeUi ui_;
    std::function<int()> sound_level_;
    JotapeWeatherView weather_;
};

#endif  // JOTAPE_DISPLAY_H
