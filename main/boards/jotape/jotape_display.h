#ifndef JOTAPE_DISPLAY_H
#define JOTAPE_DISPLAY_H

#include "display/lcd_display.h"
#include "jotape_ui.h"

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

private:
    JotapeUi::Fonts CurrentFonts() const;
    void SyncScreen();

    JotapeUi ui_;
};

#endif  // JOTAPE_DISPLAY_H
