#ifndef JOTAPE_ALARMS_H
#define JOTAPE_ALARMS_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Day bits for JotapeAlarm::days (bit 0 = Sunday ... bit 6 = Saturday).
constexpr uint8_t kJotapeDaysOnce = 0x00;
constexpr uint8_t kJotapeDaysDaily = 0x7F;
constexpr uint8_t kJotapeDaysWeekdays = 0x3E;
constexpr uint8_t kJotapeDaysWeekends = 0x41;

struct JotapeAlarm {
    int id = 0;
    int hour = 0;
    int minute = 0;
    uint8_t days = kJotapeDaysOnce;  // 0 = rings once, then it is removed
    std::string label;
};

struct JotapeAlarmState {
    std::vector<JotapeAlarm> alarms;
    std::string next_alarm;  // "07:30" or empty
    bool timer_active = false;
    int timer_remaining_s = 0;
    int timer_total_s = 0;
    std::string timer_label;
    bool ringing = false;
    bool ringing_timer = false;  // true: timer finished, false: alarm
    std::string ring_title;      // "07:30" for alarms
    std::string ring_label;
};

// Alarms (persisted in NVS) and one countdown timer, checked by a background
// task. All methods are thread-safe.
class JotapeAlarms {
public:
    static JotapeAlarms& GetInstance();

    // on_change: state changed (also every second while a timer runs or it
    // rings). on_ring: an alarm or timer just started ringing. Both run on
    // the alarm task.
    void Start(std::function<void(const JotapeAlarmState&)> on_change,
               std::function<void()> on_ring);

    // Returns the new alarm id, or -1 with `error` set.
    int AddAlarm(int hour, int minute, uint8_t days, const std::string& label, std::string& error);
    bool DeleteAlarm(int id);
    int DeleteAll();
    void StartTimer(int seconds, const std::string& label);
    bool CancelTimer();
    bool StopRinging();
    JotapeAlarmState State();

    static uint8_t ParseRepeat(const std::string& repeat);
    static std::string RepeatText(uint8_t days);

private:
    JotapeAlarms() = default;
    static void TaskEntry(void* arg);
    void Run();
    void Load();
    void Save();                                                                // mutex_ held
    void Ring(bool timer, const std::string& title, const std::string& label);  // mutex_ held
    JotapeAlarmState StateLocked();
    void Publish();

    std::mutex mutex_;
    std::vector<JotapeAlarm> alarms_;
    int next_id_ = 1;

    bool timer_active_ = false;
    int64_t timer_end_us_ = 0;
    int timer_total_s_ = 0;
    std::string timer_label_;

    bool ringing_ = false;
    bool ringing_timer_ = false;
    std::string ring_title_;
    std::string ring_label_;
    int64_t ring_started_us_ = 0;
    int64_t last_sound_us_ = 0;

    int last_minute_key_ = -1;
    TaskHandle_t task_ = nullptr;
    std::function<void(const JotapeAlarmState&)> on_change_;
    std::function<void()> on_ring_;
};

#endif  // JOTAPE_ALARMS_H
