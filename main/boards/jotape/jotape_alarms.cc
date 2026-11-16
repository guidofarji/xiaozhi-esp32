#include "jotape_alarms.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "application.h"
#include "assets/lang_config.h"
#include "settings.h"

#define TAG "JotapeAlarms"

namespace {

constexpr const char* kSettingsNs = "jotape";
constexpr size_t kMaxAlarms = 8;
constexpr int64_t kRingDurationUs = 60LL * 1000 * 1000;  // stop by itself after a minute
constexpr int64_t kSoundEveryUs = 2500LL * 1000;

bool LocalTime(struct tm& out) {
    time_t now = time(nullptr);
    localtime_r(&now, &out);
    return out.tm_year >= 2025 - 1900;
}

}  // namespace

JotapeAlarms& JotapeAlarms::GetInstance() {
    static JotapeAlarms instance;
    return instance;
}

uint8_t JotapeAlarms::ParseRepeat(const std::string& repeat) {
    if (repeat == "daily") {
        return kJotapeDaysDaily;
    }
    if (repeat == "weekdays") {
        return kJotapeDaysWeekdays;
    }
    if (repeat == "weekends") {
        return kJotapeDaysWeekends;
    }
    return kJotapeDaysOnce;
}

std::string JotapeAlarms::RepeatText(uint8_t days) {
    switch (days) {
        case kJotapeDaysOnce:
            return "UNA VEZ";
        case kJotapeDaysDaily:
            return "DIARIA";
        case kJotapeDaysWeekdays:
            return "LUN-VIE";
        case kJotapeDaysWeekends:
            return "FINDE";
        default: {
            static const char* kLetters = "DLMXJVS";
            std::string text;
            for (int d = 0; d < 7; d++) {
                if (days & (1 << d)) {
                    text += kLetters[d];
                }
            }
            return text;
        }
    }
}

void JotapeAlarms::Start(std::function<void(const JotapeAlarmState&)> on_change,
                         std::function<void()> on_ring) {
    if (task_ != nullptr) {
        return;
    }
    on_change_ = std::move(on_change);
    on_ring_ = std::move(on_ring);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Load();
    }
    xTaskCreate(TaskEntry, "jotape_alarms", 4096, this, 3, &task_);
    Publish();
}

int JotapeAlarms::AddAlarm(int hour, int minute, uint8_t days, const std::string& label,
                           std::string& error) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        error = "Hora invalida";
        return -1;
    }
    int id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (alarms_.size() >= kMaxAlarms) {
            error = "Ya hay demasiadas alarmas (maximo 8)";
            return -1;
        }
        JotapeAlarm alarm;
        alarm.id = id = next_id_++;
        alarm.hour = hour;
        alarm.minute = minute;
        alarm.days = days & kJotapeDaysDaily;
        alarm.label = label.substr(0, 32);
        alarms_.push_back(alarm);
        std::sort(alarms_.begin(), alarms_.end(), [](const JotapeAlarm& a, const JotapeAlarm& b) {
            return a.hour * 60 + a.minute < b.hour * 60 + b.minute;
        });
        Save();
    }
    Publish();
    return id;
}

bool JotapeAlarms::DeleteAlarm(int id) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(alarms_.begin(), alarms_.end(),
                                 [id](const JotapeAlarm& a) { return a.id == id; });
        removed = it != alarms_.end();
        alarms_.erase(it, alarms_.end());
        if (removed) {
            Save();
        }
    }
    if (removed) {
        Publish();
    }
    return removed;
}

int JotapeAlarms::DeleteAll() {
    int count;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        count = static_cast<int>(alarms_.size());
        alarms_.clear();
        Save();
    }
    Publish();
    return count;
}

void JotapeAlarms::StartTimer(int seconds, const std::string& label) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timer_active_ = seconds > 0;
        timer_total_s_ = seconds;
        timer_end_us_ = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
        timer_label_ = label.substr(0, 32);
    }
    Publish();
}

bool JotapeAlarms::CancelTimer() {
    bool was_active;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        was_active = timer_active_;
        timer_active_ = false;
    }
    Publish();
    return was_active;
}

bool JotapeAlarms::StopRinging() {
    bool was_ringing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        was_ringing = ringing_;
        ringing_ = false;
    }
    if (was_ringing) {
        Publish();
    }
    return was_ringing;
}

JotapeAlarmState JotapeAlarms::State() {
    std::lock_guard<std::mutex> lock(mutex_);
    return StateLocked();
}

JotapeAlarmState JotapeAlarms::StateLocked() {
    JotapeAlarmState state;
    state.alarms = alarms_;
    state.timer_active = timer_active_;
    state.timer_total_s = timer_total_s_;
    state.timer_label = timer_label_;
    if (timer_active_) {
        int64_t left = timer_end_us_ - esp_timer_get_time();
        state.timer_remaining_s = left > 0 ? static_cast<int>((left + 999999) / 1000000) : 0;
    }
    state.ringing = ringing_;
    state.ringing_timer = ringing_timer_;
    state.ring_title = ring_title_;
    state.ring_label = ring_label_;

    // Next alarm: smallest time ahead within the next 7 days
    struct tm now;
    if (LocalTime(now) && !alarms_.empty()) {
        int best = -1;
        const JotapeAlarm* best_alarm = nullptr;
        const int now_min = now.tm_hour * 60 + now.tm_min;
        for (const auto& a : alarms_) {
            for (int day = 0; day < 8; day++) {
                const int wday = (now.tm_wday + day) % 7;
                if (a.days != kJotapeDaysOnce && !(a.days & (1 << wday))) {
                    continue;
                }
                const int delta = day * 1440 + a.hour * 60 + a.minute - now_min;
                if (delta <= 0) {
                    continue;
                }
                if (best < 0 || delta < best) {
                    best = delta;
                    best_alarm = &a;
                }
                break;
            }
        }
        if (best_alarm != nullptr) {
            char text[8];
            snprintf(text, sizeof(text), "%02d:%02d", best_alarm->hour, best_alarm->minute);
            state.next_alarm = text;
        }
    }
    return state;
}

void JotapeAlarms::Publish() {
    if (on_change_) {
        on_change_(State());
    }
}

void JotapeAlarms::Ring(bool timer, const std::string& title, const std::string& label) {
    ringing_ = true;
    ringing_timer_ = timer;
    ring_title_ = title;
    ring_label_ = label;
    ring_started_us_ = esp_timer_get_time();
    last_sound_us_ = 0;
    ESP_LOGI(TAG, "Ringing: %s %s", title.c_str(), label.c_str());
}

void JotapeAlarms::TaskEntry(void* arg) { static_cast<JotapeAlarms*>(arg)->Run(); }

void JotapeAlarms::Run() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
        bool changed = false;
        bool started_ringing = false;
        bool play_sound = false;
        bool tick = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int64_t now_us = esp_timer_get_time();

            // Alarms: check once per new minute
            struct tm now;
            if (LocalTime(now)) {
                const int key = now.tm_yday * 1440 + now.tm_hour * 60 + now.tm_min;
                if (key != last_minute_key_) {
                    if (last_minute_key_ >= 0) {
                        for (auto it = alarms_.begin(); it != alarms_.end();) {
                            const bool day_ok =
                                it->days == kJotapeDaysOnce || (it->days & (1 << now.tm_wday));
                            if (day_ok && it->hour == now.tm_hour && it->minute == now.tm_min) {
                                char title[8];
                                snprintf(title, sizeof(title), "%02d:%02d", it->hour, it->minute);
                                Ring(false, title, it->label);
                                started_ringing = true;
                                if (it->days == kJotapeDaysOnce) {
                                    it = alarms_.erase(it);
                                    Save();
                                    continue;
                                }
                            }
                            ++it;
                        }
                    }
                    last_minute_key_ = key;
                    changed = true;  // refresh "next alarm"
                }
            }

            // Timer
            if (timer_active_) {
                if (now_us >= timer_end_us_) {
                    timer_active_ = false;
                    Ring(true, "", timer_label_);
                    started_ringing = true;
                }
                tick = true;
            }

            // Ringing: repeat the sound, stop after a minute
            if (ringing_) {
                if (now_us - ring_started_us_ > kRingDurationUs) {
                    ringing_ = false;
                    changed = true;
                } else if (now_us - last_sound_us_ >= kSoundEveryUs) {
                    last_sound_us_ = now_us;
                    play_sound = true;
                    tick = true;
                }
            }
        }

        if (play_sound) {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                // Do not talk over a conversation.
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    app.PlaySound(Lang::Sounds::OGG_EXCLAMATION);
                }
            });
        }
        if (started_ringing && on_ring_) {
            on_ring_();
        }
        static int64_t last_publish_us = 0;
        const int64_t now_us = esp_timer_get_time();
        if (changed || started_ringing || (tick && now_us - last_publish_us >= 900000)) {
            last_publish_us = now_us;
            Publish();
        }
    }
}

void JotapeAlarms::Load() {
    Settings settings(kSettingsNs, false);
    std::string json = settings.GetString("alarms", "[]");
    cJSON* root = cJSON_Parse(json.c_str());
    alarms_.clear();
    if (cJSON_IsArray(root)) {
        cJSON* item;
        cJSON_ArrayForEach (item, root) {
            cJSON* h = cJSON_GetObjectItem(item, "h");
            cJSON* m = cJSON_GetObjectItem(item, "m");
            cJSON* d = cJSON_GetObjectItem(item, "d");
            cJSON* l = cJSON_GetObjectItem(item, "l");
            if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) {
                continue;
            }
            JotapeAlarm alarm;
            alarm.id = next_id_++;
            alarm.hour = h->valueint;
            alarm.minute = m->valueint;
            alarm.days = cJSON_IsNumber(d) ? static_cast<uint8_t>(d->valueint) : kJotapeDaysOnce;
            alarm.label = cJSON_IsString(l) ? l->valuestring : "";
            alarms_.push_back(alarm);
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d alarms", static_cast<int>(alarms_.size()));
}

void JotapeAlarms::Save() {
    cJSON* root = cJSON_CreateArray();
    for (const auto& a : alarms_) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "h", a.hour);
        cJSON_AddNumberToObject(item, "m", a.minute);
        cJSON_AddNumberToObject(item, "d", a.days);
        cJSON_AddStringToObject(item, "l", a.label.c_str());
        cJSON_AddItemToArray(root, item);
    }
    char* text = cJSON_PrintUnformatted(root);
    Settings settings(kSettingsNs, true);
    settings.SetString("alarms", text != nullptr ? text : "[]");
    cJSON_free(text);
    cJSON_Delete(root);
}
