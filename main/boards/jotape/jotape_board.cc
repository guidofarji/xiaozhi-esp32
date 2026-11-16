#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "jotape_alarms.h"
#include "jotape_audio_codec.h"
#include "jotape_display.h"
#include "jotape_motion.h"
#include "jotape_weather.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"

#include <esp_log.h>
#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <esp_timer.h>

#include "power_save_timer.h"
#include "assets/lang_config.h"

#include <esp_lcd_touch_cst816s.h>
#include <esp_lvgl_port.h>
#include "driver/gpio.h"

#include "iot_button.h"

#include "power_manager.h"
#include "esp_ota_ops.h"

#define TAG "jotape_board"

class CustomButton: public Button{
public:
    void OnPressDownDel(void) {
        if (button_handle_ == nullptr) {
            return;
        }
        on_press_down_ = NULL;
        iot_button_unregister_cb(button_handle_, BUTTON_PRESS_DOWN, nullptr);
    }
    void OnPressUpDel(void) {
        if (button_handle_ == nullptr) {
            return;
        }
        on_press_up_ = NULL;
        iot_button_unregister_cb(button_handle_, BUTTON_PRESS_UP, nullptr);
    }
};



class CustomBoard : public WifiBoard {
private:
    CustomButton pwr_button_;
    CustomButton volume_up_button_;
    CustomButton volume_down_button_;
    i2c_master_bus_handle_t i2c_bus_;
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    std::atomic<JotapeAudioCodec*> codec_{nullptr};
    int rotation_ = -1;

    // Rotates the UI (panel scan direction + LVGL coordinates + touch).
    void ApplyRotation(int rotation) {
        static const int kGaps[4][2] = {{JOTAPE_GAP_ROTATION_0},
                                        {JOTAPE_GAP_ROTATION_90},
                                        {JOTAPE_GAP_ROTATION_180},
                                        {JOTAPE_GAP_ROTATION_270}};
        if (rotation < 0 || rotation > 3 || rotation == rotation_ || panel_ == nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        lv_display_set_rotation(lv_display_get_default(),
                                static_cast<lv_display_rotation_t>(rotation));
        esp_lcd_panel_set_gap(panel_, DISPLAY_OFFSET_X + kGaps[rotation][0],
                              DISPLAY_OFFSET_Y + kGaps[rotation][1]);
        lv_obj_invalidate(lv_screen_active());
        if (touch_ != nullptr) {
            // Map raw touch points to the rotated LVGL coordinates
            // (the driver mirrors first, then swaps).
            static const bool kTouch[4][3] = {
                // swap_xy, mirror_x, mirror_y
                {false, false, false},  // 0
                {true, false, true},    // 90
                {false, true, true},    // 180
                {true, true, false},    // 270
            };
            esp_lcd_touch_set_swap_xy(touch_, kTouch[rotation][0]);
            esp_lcd_touch_set_mirror_x(touch_, kTouch[rotation][1]);
            esp_lcd_touch_set_mirror_y(touch_, kTouch[rotation][2]);
        }
        rotation_ = rotation;
        ESP_LOGI(TAG, "Screen rotation: %d deg", rotation * 90);
    }

    // Polls the accelerometer and keeps the face upright.
    static void RotationTask(void* arg) {
        auto self = static_cast<CustomBoard*>(arg);
        int candidate = -1;
        int stable = 0;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(150));
            JotapeMotion::Accel a;
            if (!JotapeMotion::Read(a)) {
                continue;
            }
            const int ax = std::abs(a.x);
            const int ay = std::abs(a.y);
            int next = -1;
            // Ignore lying flat or ambiguous diagonal positions.
            if (std::max(ax, ay) > 550 && std::abs(ax - ay) > 250) {
                if (ax > ay) {
                    next = a.x > 0 ? JOTAPE_ROTATION_GRAVITY_POS_X : JOTAPE_ROTATION_GRAVITY_NEG_X;
                } else {
                    next = a.y > 0 ? JOTAPE_ROTATION_GRAVITY_POS_Y : JOTAPE_ROTATION_GRAVITY_NEG_Y;
                }
            }
            if (next < 0 || next != candidate) {
                candidate = next;
                stable = 0;
                continue;
            }
            if (++stable == 4 && next != self->rotation_) {
                self->ApplyRotation(next);
            }
        }
    }

    void InitializeRotation() {
        ApplyRotation(JOTAPE_DEFAULT_ROTATION);
        if (JotapeMotion::Initialize(i2c_bus_) == ESP_OK) {
            xTaskCreate(RotationTask, "jotape_rotate", 3072, this, 2, nullptr);
        }
    }
    LcdDisplay* display_;
    PowerManager* power_manager_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(BATTERY_CHARGING_PIN, BATTERY_ADC_PIN, BATTERY_EN_PIN);
        power_manager_->PowerON();
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(20);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            power_manager_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }
    
    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize QSPI bus");
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeTouch()
    {
        esp_err_t ret = ESP_OK;
        esp_lcd_touch_handle_t touch_handle;

        esp_lcd_panel_io_handle_t tp_io_handle = NULL;

        esp_lcd_panel_io_i2c_config_t tp_io_config = {};
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
        tp_io_config.scl_speed_hz = 100000;
        tp_io_config.control_phase_bytes = 1;
        tp_io_config.dc_bit_offset = 0;
        tp_io_config.lcd_cmd_bits = 8;
        tp_io_config.lcd_param_bits = 0;
        tp_io_config.flags.disable_control_phase = 1;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));

        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = TOOUCH_RST_PIN,
            .int_gpio_num = TOOUCH_INT_PIN,
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        ESP_LOGI(TAG, "Initialize touch controller CST816");
        ret = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle);
        if (ret == ESP_OK)
        {
            touch_ = touch_handle;
            const lvgl_port_touch_cfg_t touch_cfg = {
                .disp = lv_display_get_default(), 
                .handle = touch_handle,
            };
            lvgl_port_add_touch(&touch_cfg);
            ESP_LOGI(TAG, "Touch panel initialized successfully");
        }
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));


        // 初始化液晶屏驱动芯片
        ESP_LOGI(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
         
       
        esp_lcd_panel_reset(panel);
 
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        
        panel_ = panel;
        display_ = new JotapeDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY
                                    );
    }

    void InitializeButtons() {

        pwr_button_.OnPressUp([this]() {
            pwr_button_.OnClick([this]() {
                if (JotapeAlarms::GetInstance().StopRinging()) {
                    return;  // the click only silences the alarm
                }
                auto& app = Application::GetInstance();
                app.ToggleChatState();
            });
            pwr_button_.OnLongPress([this]() {
                printf("Power button long press\n");
                // gpio_set_level(BATTERY_EN_PIN, 0);
                if (power_manager_ != nullptr){
                    power_manager_->PowerOff();
                }
            });

            pwr_button_.OnDoubleClick([this]() {
                static uint8_t brightness_last = 0;
                auto backlight = Board::GetInstance().GetBacklight();
                if (backlight->brightness() == 0) {
                    brightness_last = 0;
                    if (brightness_last == 0) {
                        backlight->SetBrightness(50, true);
                    } else {
                        backlight->SetBrightness(brightness_last, true);
                    }
                } else {
                    brightness_last = backlight->brightness();
                    backlight->SetBrightness(0);
                }
            });
            pwr_button_.OnMultipleClick([this]() {
                EnterWifiConfigMode();
            }, 3);
            pwr_button_.OnPressUpDel();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = Board::GetInstance().GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
        });

        volume_up_button_.OnLongPress([this]() {
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = Board::GetInstance().GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
        });

        volume_down_button_.OnLongPress([this]() {
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });

#if CONFIG_USE_DEVICE_AEC
        volume_down_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
    }

    // 初始化工具
    void InitializeWeather() {
        auto display = static_cast<JotapeDisplay*>(display_);
        JotapeWeather::GetInstance().Start([display](const JotapeWeatherData& data) {
            JotapeWeatherView view;
            view.valid = data.valid;
            view.city = data.city;
            view.status = data.status;
            view.description = JotapeWeather::Describe(data.weather_code);
            view.temperature = data.temperature;
            view.temp_max = data.temp_max;
            view.temp_min = data.temp_min;
            view.weather_code = data.weather_code;
            view.is_day = data.is_day;
            display->SetWeather(view);
        });
    }

    void InitializeAlarms() {
        auto display = static_cast<JotapeDisplay*>(display_);
        auto& alarms = JotapeAlarms::GetInstance();
        display->SetAlarmActions(
            [](int seconds) { JotapeAlarms::GetInstance().StartTimer(seconds, ""); },
            []() { JotapeAlarms::GetInstance().CancelTimer(); },
            []() { JotapeAlarms::GetInstance().StopRinging(); });
        alarms.Start(
            [display](const JotapeAlarmState& state) {
                JotapeAlarmsView view;
                for (const auto& a : state.alarms) {
                    char time_text[8];
                    snprintf(time_text, sizeof(time_text), "%02d:%02d", a.hour, a.minute);
                    view.alarms.push_back({time_text, JotapeAlarms::RepeatText(a.days), a.label});
                }
                view.next_alarm = state.next_alarm;
                view.timer_active = state.timer_active;
                view.timer_remaining_s = state.timer_remaining_s;
                view.timer_total_s = state.timer_total_s;
                view.timer_label = state.timer_label;
                view.ringing = state.ringing;
                view.ringing_timer = state.ringing_timer;
                view.ring_title = state.ring_title;
                view.ring_label = state.ring_label;
                display->SetAlarms(view);
            },
            [this]() {
                // Turn the screen back on so the alarm can be seen.
                if (power_save_timer_ != nullptr) {
                    power_save_timer_->WakeUp();
                }
            });
    }

    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.alarm.set",
            "Set an alarm on JOTAPE. time is 24h \"HH:MM\" local time. repeat is one of "
            "\"once\", \"daily\", \"weekdays\" (Mon-Fri) or \"weekends\". label is optional "
            "(e.g. \"trabajo\"). For a countdown like 'in 10 minutes' use self.timer.start instead.",
            PropertyList({Property("time", kPropertyTypeString),
                          Property("repeat", kPropertyTypeString, std::string("once")),
                          Property("label", kPropertyTypeString, std::string(""))}),
            [](const PropertyList& properties) -> ReturnValue {
                auto time_text = properties["time"].value<std::string>();
                int hour = -1, minute = -1;
                if (sscanf(time_text.c_str(), "%d:%d", &hour, &minute) != 2) {
                    return std::string("Formato de hora invalido, usar HH:MM");
                }
                std::string error;
                int id = JotapeAlarms::GetInstance().AddAlarm(
                    hour, minute, JotapeAlarms::ParseRepeat(properties["repeat"].value<std::string>()),
                    properties["label"].value<std::string>(), error);
                if (id < 0) {
                    return error;
                }
                char text[80];
                snprintf(text, sizeof(text), "Alarma %d puesta a las %02d:%02d", id, hour, minute);
                return std::string(text);
            });
        mcp_server.AddTool("self.alarm.list",
            "List the alarms set on JOTAPE with their id, time, repeat and label.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto state = JotapeAlarms::GetInstance().State();
                if (state.alarms.empty()) {
                    return std::string("No hay alarmas");
                }
                std::string text;
                for (const auto& a : state.alarms) {
                    char line[96];
                    snprintf(line, sizeof(line), "id %d: %02d:%02d %s %s\n", a.id, a.hour, a.minute,
                             JotapeAlarms::RepeatText(a.days).c_str(), a.label.c_str());
                    text += line;
                }
                return text;
            });
        mcp_server.AddTool("self.alarm.delete",
            "Delete one alarm by id (see self.alarm.list), or all alarms with id = 0.",
            PropertyList({Property("id", kPropertyTypeInteger, 0, 1000)}),
            [](const PropertyList& properties) -> ReturnValue {
                int id = properties["id"].value<int>();
                auto& alarms = JotapeAlarms::GetInstance();
                if (id == 0) {
                    return std::string("Alarmas borradas: ") + std::to_string(alarms.DeleteAll());
                }
                return std::string(alarms.DeleteAlarm(id) ? "Alarma borrada" : "No existe esa alarma");
            });
        mcp_server.AddTool("self.alarm.stop",
            "Silence the alarm or finished timer that is ringing right now.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                return std::string(JotapeAlarms::GetInstance().StopRinging() ? "Alarma apagada"
                                                                             : "No hay nada sonando");
            });
        mcp_server.AddTool("self.timer.start",
            "Start a countdown timer on JOTAPE (replaces a running one). Give the duration in "
            "seconds (e.g. 10 minutes = 600). label is optional (e.g. \"pizza\").",
            PropertyList({Property("seconds", kPropertyTypeInteger, 1, 86400),
                          Property("label", kPropertyTypeString, std::string(""))}),
            [](const PropertyList& properties) -> ReturnValue {
                int seconds = properties["seconds"].value<int>();
                JotapeAlarms::GetInstance().StartTimer(seconds,
                                                       properties["label"].value<std::string>());
                return std::string("Temporizador de ") + std::to_string(seconds) + " segundos en marcha";
            });
        mcp_server.AddTool("self.timer.cancel", "Cancel the running countdown timer.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                return std::string(JotapeAlarms::GetInstance().CancelTimer() ? "Temporizador cancelado"
                                                                            : "No habia temporizador");
            });
        mcp_server.AddTool("self.timer.status", "How much time is left on the countdown timer.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto state = JotapeAlarms::GetInstance().State();
                if (!state.timer_active) {
                    return std::string("No hay temporizador en marcha");
                }
                return std::string("Faltan ") + std::to_string(state.timer_remaining_s) + " segundos";
            });
        mcp_server.AddTool("self.weather.get_current",
            "Get the current weather where JOTAPE is (city, temperature in Celsius, "
            "today's max/min and conditions in Spanish). Use it when the user asks about the "
            "weather or temperature.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto data = JotapeWeather::GetInstance().Get();
                if (!data.valid) {
                    return std::string("Todavia no hay datos del clima (") + data.status + ")";
                }
                char text[200];
                snprintf(text, sizeof(text), "%s: %d C, %s. Maxima %d C, minima %d C.",
                         data.city.c_str(), data.temperature,
                         JotapeWeather::Describe(data.weather_code), data.temp_max, data.temp_min);
                return std::string(text);
            });
        mcp_server.AddTool("self.weather.set_city",
            "Change the city used for the weather. Pass a city name (e.g. 'Cordoba, Argentina'), "
            "or 'auto' to go back to the automatic location from the internet connection. "
            "The new weather is ready a few seconds later; then call self.weather.get_current.",
            PropertyList({Property("city", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ReturnValue {
                auto city = properties["city"].value<std::string>();
                JotapeWeather::GetInstance().RequestCity(city);
                return std::string("Buscando el clima de ") + city + "...";
            });
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "Reboot the device and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });
    }

public:
    CustomBoard() :
        pwr_button_(PWR_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        InitializeRotation();
        InitializeWeather();
        InitializeAlarms();
        static_cast<JotapeDisplay*>(display_)->SetSoundLevelProvider([this]() {
            JotapeAudioCodec* codec = codec_.load();
            if (codec == nullptr) {
                return 0;
            }
            auto& app = Application::GetInstance();
            switch (app.GetDeviceState()) {
                case kDeviceStateListening:
                    // Only react to speech, not background noise.
                    return app.GetAudioService().IsVoiceDetected()
                               ? std::max(30, codec->input_level())
                               : 0;
                case kDeviceStateSpeaking:
                    return codec->output_level();
                default:
                    return 0;
            }
        });
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
       static JotapeAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            true);
        codec_.store(&audio_codec);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(CustomBoard);
