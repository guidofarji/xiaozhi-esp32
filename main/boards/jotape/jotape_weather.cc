#include "jotape_weather.h"

#include <esp_log.h>
#include <cJSON.h>

#include <cmath>
#include <cstdio>
#include <memory>

#include "board.h"
#include "settings.h"

#define TAG "JotapeWeather"

namespace {

constexpr const char* kSettingsNs = "jotape";
constexpr uint32_t kRefreshMs = 30 * 60 * 1000;  // 30 minutes
constexpr uint32_t kRetryMs = 30 * 1000;
constexpr uint32_t kStartDelayMs = 8 * 1000;  // let Wi-Fi and the OTA check settle

std::string UrlEncode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

struct JsonDeleter {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

}  // namespace

JotapeWeather& JotapeWeather::GetInstance() {
    static JotapeWeather instance;
    return instance;
}

const char* JotapeWeather::Describe(int code) {
    switch (code) {
        case 0:
            return "Despejado";
        case 1:
            return "Mayormente despejado";
        case 2:
            return "Parcialmente nublado";
        case 3:
            return "Nublado";
        case 45:
        case 48:
            return "Niebla";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            return "Llovizna";
        case 61:
        case 63:
        case 66:
            return "Lluvia";
        case 65:
        case 67:
            return "Lluvia fuerte";
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86:
            return "Nieve";
        case 80:
        case 81:
        case 82:
            return "Chaparrones";
        case 95:
        case 96:
        case 99:
            return "Tormenta";
        default:
            return "Sin datos";
    }
}

void JotapeWeather::Start(std::function<void(const JotapeWeatherData&)> on_update) {
    if (task_ != nullptr) {
        return;
    }
    on_update_ = std::move(on_update);
    SetStatus("BUSCANDO UBICACIÓN...");
    xTaskCreate(TaskEntry, "jotape_weather", 8192, this, 2, &task_);
}

JotapeWeatherData JotapeWeather::Get() {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_;
}

void JotapeWeather::RequestCity(const std::string& city) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_city_ = city;
        city_requested_ = true;
    }
    RequestRefresh();
}

void JotapeWeather::RequestRefresh() {
    if (task_ != nullptr) {
        xTaskNotifyGive(task_);
    }
}

void JotapeWeather::TaskEntry(void* arg) { static_cast<JotapeWeather*>(arg)->Run(); }

void JotapeWeather::Run() {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kStartDelayMs));
    while (true) {
        bool ok = ResolveLocation() && FetchForecast();
        // Wait for the next refresh, or wake up early on a request.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ok ? kRefreshMs : kRetryMs));
    }
}

bool JotapeWeather::ResolveLocation() {
    std::string requested;
    bool has_request = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (city_requested_) {
            requested = pending_city_;
            has_request = true;
            city_requested_ = false;
        }
    }

    if (has_request) {
        Settings settings(kSettingsNs, true);
        if (requested.empty() || requested == "auto") {
            settings.SetString("w_city", "");
            has_location_ = false;
        } else {
            SetStatus("BUSCANDO CIUDAD...");
            if (!Geocode(requested)) {
                SetStatus("CIUDAD NO ENCONTRADA");
                return false;
            }
            settings.SetString("w_city", location_name_);
            settings.SetInt("w_lat", static_cast<int32_t>(std::lround(latitude_ * 100000)));
            settings.SetInt("w_lon", static_cast<int32_t>(std::lround(longitude_ * 100000)));
            return true;
        }
    }

    if (has_location_) {
        return true;
    }

    // A city chosen earlier by voice wins over the IP location.
    {
        Settings settings(kSettingsNs, false);
        std::string city = settings.GetString("w_city", "");
        if (!city.empty()) {
            location_name_ = city;
            latitude_ = settings.GetInt("w_lat", 0) / 100000.0;
            longitude_ = settings.GetInt("w_lon", 0) / 100000.0;
            has_location_ = true;
            return true;
        }
    }

    if (!LocateByIp()) {
        SetStatus("SIN UBICACIÓN");
        return false;
    }
    return true;
}

bool JotapeWeather::LocateByIp() {
    std::string body;
    if (!HttpGet("http://ip-api.com/json/?fields=status,city,lat,lon", body)) {
        return false;
    }
    JsonPtr root(cJSON_Parse(body.c_str()));
    if (!root) {
        return false;
    }
    cJSON* status = cJSON_GetObjectItem(root.get(), "status");
    cJSON* city = cJSON_GetObjectItem(root.get(), "city");
    cJSON* lat = cJSON_GetObjectItem(root.get(), "lat");
    cJSON* lon = cJSON_GetObjectItem(root.get(), "lon");
    if (!cJSON_IsString(status) || std::string(status->valuestring) != "success" ||
        !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
        return false;
    }
    latitude_ = lat->valuedouble;
    longitude_ = lon->valuedouble;
    location_name_ = cJSON_IsString(city) ? city->valuestring : "";
    has_location_ = true;
    ESP_LOGI(TAG, "IP location: %s (%.3f, %.3f)", location_name_.c_str(), latitude_, longitude_);
    return true;
}

bool JotapeWeather::Geocode(const std::string& query) {
    std::string body;
    std::string url =
        "https://geocoding-api.open-meteo.com/v1/search?count=1&language=es&format=json&name=" +
        UrlEncode(query);
    if (!HttpGet(url, body)) {
        return false;
    }
    JsonPtr root(cJSON_Parse(body.c_str()));
    cJSON* results = root ? cJSON_GetObjectItem(root.get(), "results") : nullptr;
    cJSON* first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
    if (first == nullptr) {
        return false;
    }
    cJSON* name = cJSON_GetObjectItem(first, "name");
    cJSON* lat = cJSON_GetObjectItem(first, "latitude");
    cJSON* lon = cJSON_GetObjectItem(first, "longitude");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
        return false;
    }
    latitude_ = lat->valuedouble;
    longitude_ = lon->valuedouble;
    location_name_ = cJSON_IsString(name) ? name->valuestring : query;
    has_location_ = true;
    ESP_LOGI(TAG, "City: %s (%.3f, %.3f)", location_name_.c_str(), latitude_, longitude_);
    return true;
}

bool JotapeWeather::FetchForecast() {
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,is_day"
             "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
             latitude_, longitude_);
    std::string body;
    if (!HttpGet(url, body)) {
        SetStatus("SIN CONEXIÓN");
        return false;
    }
    JsonPtr root(cJSON_Parse(body.c_str()));
    cJSON* current = root ? cJSON_GetObjectItem(root.get(), "current") : nullptr;
    cJSON* daily = root ? cJSON_GetObjectItem(root.get(), "daily") : nullptr;
    cJSON* temp = current ? cJSON_GetObjectItem(current, "temperature_2m") : nullptr;
    cJSON* code = current ? cJSON_GetObjectItem(current, "weather_code") : nullptr;
    cJSON* is_day = current ? cJSON_GetObjectItem(current, "is_day") : nullptr;
    cJSON* tmax = daily ? cJSON_GetObjectItem(daily, "temperature_2m_max") : nullptr;
    cJSON* tmin = daily ? cJSON_GetObjectItem(daily, "temperature_2m_min") : nullptr;
    cJSON* tmax0 = cJSON_IsArray(tmax) ? cJSON_GetArrayItem(tmax, 0) : nullptr;
    cJSON* tmin0 = cJSON_IsArray(tmin) ? cJSON_GetArrayItem(tmin, 0) : nullptr;
    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(code)) {
        SetStatus("SIN DATOS");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.valid = true;
        data_.city = location_name_;
        data_.status.clear();
        data_.temperature = static_cast<int>(std::lround(temp->valuedouble));
        data_.weather_code = code->valueint;
        data_.is_day = !cJSON_IsNumber(is_day) || is_day->valueint != 0;
        data_.temp_max = cJSON_IsNumber(tmax0) ? static_cast<int>(std::lround(tmax0->valuedouble))
                                               : data_.temperature;
        data_.temp_min = cJSON_IsNumber(tmin0) ? static_cast<int>(std::lround(tmin0->valuedouble))
                                               : data_.temperature;
    }
    ESP_LOGI(TAG, "%s: %d C, code %d", location_name_.c_str(), data_.temperature,
             data_.weather_code);
    Publish();
    return true;
}

bool JotapeWeather::HttpGet(const std::string& url, std::string& body) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp();
    if (!http) {
        return false;
    }
    http->SetTimeout(10000);
    http->SetHeader("User-Agent", "jotape-weather/1.0");
    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "GET %s failed: %s", url.c_str(), opened.error().ToString().c_str());
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        ESP_LOGW(TAG, "GET %s: bad status", url.c_str());
        http->Close();
        return false;
    }
    body = http->ReadAll();
    http->Close();
    return !body.empty();
}

void JotapeWeather::SetStatus(const char* status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.status = status;
    }
    Publish();
}

void JotapeWeather::Publish() {
    if (on_update_) {
        on_update_(Get());
    }
}
