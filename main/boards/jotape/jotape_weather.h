#ifndef JOTAPE_WEATHER_H
#define JOTAPE_WEATHER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>
#include <mutex>
#include <string>

// Current weather for the JOTAPE weather screen and voice answers.
struct JotapeWeatherData {
    bool valid = false;
    std::string city;     // display name, e.g. "Buenos Aires"
    std::string status;   // shown when not valid: "BUSCANDO UBICACIÓN..."
    int temperature = 0;  // degrees Celsius, rounded
    int temp_max = 0;
    int temp_min = 0;
    int weather_code = 0;  // WMO code from Open-Meteo
    bool is_day = true;
};

// Fetches weather from Open-Meteo in a background task (never on the main
// loop). The location comes from the IP address unless a city was chosen by
// voice; the choice is stored in NVS.
class JotapeWeather {
public:
    static JotapeWeather& GetInstance();

    // Starts the background task; `on_update` runs on that task.
    void Start(std::function<void(const JotapeWeatherData&)> on_update);

    // Thread-safe snapshot of the latest data.
    JotapeWeatherData Get();

    // Requests a new city ("auto" or empty = use the IP location). Returns
    // immediately; the task geocodes and refreshes.
    void RequestCity(const std::string& city);
    void RequestRefresh();

    // Spanish description of a WMO weather code.
    static const char* Describe(int code);

private:
    JotapeWeather() = default;
    static void TaskEntry(void* arg);
    void Run();
    bool ResolveLocation();
    bool LocateByIp();
    bool Geocode(const std::string& query);
    bool FetchForecast();
    bool HttpGet(const std::string& url, std::string& body);
    void Publish();
    void SetStatus(const char* status);

    std::mutex mutex_;
    JotapeWeatherData data_;
    std::function<void(const JotapeWeatherData&)> on_update_;
    TaskHandle_t task_ = nullptr;

    // Location in use (task-owned except pending_city_)
    bool has_location_ = false;
    double latitude_ = 0;
    double longitude_ = 0;
    std::string location_name_;
    std::string pending_city_;  // guarded by mutex_
    bool city_requested_ = false;
};

#endif  // JOTAPE_WEATHER_H
