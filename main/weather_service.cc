#include "weather_service.h"

#include "board.h"
#include "network_interface.h"

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace {

static const char* TAG = "WeatherService";
static constexpr size_t kMaxHttpBodyBytes = 16 * 1024;
static constexpr int kHttpTimeoutMs = 10000;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string UrlEncode(const std::string& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

bool IsNumber(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    std::strtod(value.c_str(), &end);
    return end != value.c_str() && *end == '\0';
}

std::string JsonString(cJSON* root)
{
    char* raw = cJSON_PrintUnformatted(root);
    std::string result(raw ? raw : "{}");
    if (raw) {
        cJSON_free(raw);
    }
    return result;
}

std::string ErrorJson(const char* error, const char* setup_required = nullptr)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error);
    if (setup_required != nullptr) {
        cJSON_AddStringToObject(root, "setup_required", setup_required);
    }
    std::string result = JsonString(root);
    cJSON_Delete(root);
    return result;
}

bool HttpGetLimited(const std::string& url, std::string& body, int& status_code)
{
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        body = "network is unavailable";
        return false;
    }

    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        body = "failed to create http client";
        return false;
    }

    http->SetTimeout(kHttpTimeoutMs);
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetHeader("User-Agent", "xiaozhi-card-weather/1.0");

    if (!http->Open("GET", url)) {
        body = "failed to open http connection";
        return false;
    }

    status_code = http->GetStatusCode();
    char buffer[512];
    while (true) {
        int bytes = http->Read(buffer, sizeof(buffer));
        if (bytes < 0) {
            http->Close();
            body = "failed to read http response";
            return false;
        }
        if (bytes == 0) {
            break;
        }
        if (body.size() + static_cast<size_t>(bytes) > kMaxHttpBodyBytes) {
            http->Close();
            body = "http response is too large";
            return false;
        }
        body.append(buffer, bytes);
    }

    http->Close();
    return true;
}

const char* WeatherCodeTextZh(int code)
{
    if (code == 0) return "晴";
    if (code == 1) return "大部晴朗";
    if (code == 2) return "局部多云";
    if (code == 3) return "阴";
    if (code == 45 || code == 48) return "雾";
    if (code >= 51 && code <= 57) return "毛毛雨";
    if (code >= 61 && code <= 67) return "雨";
    if (code >= 71 && code <= 77) return "雪";
    if (code >= 80 && code <= 82) return "阵雨";
    if (code == 85 || code == 86) return "阵雪";
    if (code == 95) return "雷暴";
    if (code == 96 || code == 99) return "雷暴伴冰雹";
    return "未知";
}

const char* WeatherCodeTextEn(int code)
{
    if (code == 0) return "Clear sky";
    if (code == 1) return "Mainly clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Rain showers";
    if (code == 85 || code == 86) return "Snow showers";
    if (code == 95) return "Thunderstorm";
    if (code == 96 || code == 99) return "Thunderstorm with hail";
    return "Unknown";
}

const char* WeatherCodeText(int code, const std::string& lang)
{
    return ToLower(lang).find("en") == 0 ? WeatherCodeTextEn(code) : WeatherCodeTextZh(code);
}

bool ResolveLocationByName(const std::string& location,
                           const std::string& lang,
                           std::string& latitude,
                           std::string& longitude,
                           std::string& resolved_name,
                           std::string& country,
                           std::string& timezone,
                           std::string& error)
{
    std::string language = ToLower(lang).find("en") == 0 ? "en" : "zh";
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                      UrlEncode(location) + "&count=1&language=" + language + "&format=json";

    std::string body;
    int status = 0;
    if (!HttpGetLimited(url, body, status)) {
        error = body;
        return false;
    }
    if (status != 200) {
        error = "geocoding http status " + std::to_string(status);
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        error = "failed to parse geocoding response";
        return false;
    }

    cJSON* results = cJSON_GetObjectItem(root, "results");
    cJSON* first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
    if (!cJSON_IsObject(first)) {
        cJSON_Delete(root);
        error = "location not found";
        return false;
    }

    cJSON* lat = cJSON_GetObjectItem(first, "latitude");
    cJSON* lon = cJSON_GetObjectItem(first, "longitude");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
        cJSON_Delete(root);
        error = "geocoding response missing coordinates";
        return false;
    }

    latitude = std::to_string(lat->valuedouble);
    longitude = std::to_string(lon->valuedouble);

    cJSON* name = cJSON_GetObjectItem(first, "name");
    cJSON* admin1 = cJSON_GetObjectItem(first, "admin1");
    cJSON* country_item = cJSON_GetObjectItem(first, "country");
    cJSON* timezone_item = cJSON_GetObjectItem(first, "timezone");

    if (cJSON_IsString(name)) {
        resolved_name = name->valuestring;
    }
    if (cJSON_IsString(admin1) && admin1->valuestring[0] != '\0') {
        if (!resolved_name.empty()) {
            resolved_name += ", ";
        }
        resolved_name += admin1->valuestring;
    }
    if (cJSON_IsString(country_item)) {
        country = country_item->valuestring;
    }
    if (cJSON_IsString(timezone_item)) {
        timezone = timezone_item->valuestring;
    }

    cJSON_Delete(root);
    return true;
}

void AddNumberIfPresent(cJSON* target, cJSON* source, const char* source_key, const char* target_key)
{
    cJSON* item = cJSON_GetObjectItem(source, source_key);
    if (cJSON_IsNumber(item)) {
        cJSON_AddNumberToObject(target, target_key, item->valuedouble);
    }
}

} // namespace

std::string WeatherService::GetCurrentWeather(const std::string& location,
                                              const std::string& latitude_arg,
                                              const std::string& longitude_arg,
                                              const std::string& unit_arg,
                                              const std::string& lang_arg)
{
    std::string latitude = latitude_arg;
    std::string longitude = longitude_arg;
    std::string resolved_name;
    std::string country;
    std::string timezone;
    std::string lang = lang_arg.empty() ? "zh" : lang_arg;
    std::string unit = unit_arg.empty() ? "metric" : ToLower(unit_arg);

    if (latitude.empty() && longitude.empty() && !location.empty()) {
        size_t comma = location.find(',');
        if (comma != std::string::npos) {
            latitude = location.substr(0, comma);
            longitude = location.substr(comma + 1);
        } else {
            std::string error;
            if (!ResolveLocationByName(location, lang, latitude, longitude, resolved_name, country, timezone, error)) {
                ESP_LOGW(TAG, "Failed to resolve location '%s': %s", location.c_str(), error.c_str());
                return ErrorJson(error.c_str(), "valid_location");
            }
        }
    }

    if (!IsNumber(latitude) || !IsNumber(longitude)) {
        return ErrorJson("Weather location is missing. Provide latitude/longitude or a city name.", "location");
    }

    bool imperial = unit == "imperial" || unit == "fahrenheit" || unit == "f";
    std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" + UrlEncode(latitude) +
                      "&longitude=" + UrlEncode(longitude) +
                      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
                      "&timezone=auto&forecast_days=1";
    if (imperial) {
        url += "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
    }

    std::string body;
    int status = 0;
    if (!HttpGetLimited(url, body, status)) {
        ESP_LOGW(TAG, "Weather HTTP failed: %s", body.c_str());
        return ErrorJson(body.c_str());
    }
    if (status != 200) {
        std::string error = "weather http status " + std::to_string(status);
        ESP_LOGW(TAG, "%s", error.c_str());
        return ErrorJson(error.c_str());
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return ErrorJson("failed to parse weather response");
    }

    cJSON* current = cJSON_GetObjectItem(root, "current");
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return ErrorJson("weather response missing current weather");
    }

    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddStringToObject(result, "provider", "Open-Meteo");
    cJSON_AddStringToObject(result, "unit", imperial ? "imperial" : "metric");
    cJSON_AddStringToObject(result, "latitude", latitude.c_str());
    cJSON_AddStringToObject(result, "longitude", longitude.c_str());
    if (!location.empty()) {
        cJSON_AddStringToObject(result, "requested_location", location.c_str());
    }
    if (!resolved_name.empty()) {
        cJSON_AddStringToObject(result, "resolved_location", resolved_name.c_str());
    }
    if (!country.empty()) {
        cJSON_AddStringToObject(result, "country", country.c_str());
    }
    if (!timezone.empty()) {
        cJSON_AddStringToObject(result, "timezone", timezone.c_str());
    } else {
        cJSON* tz = cJSON_GetObjectItem(root, "timezone");
        if (cJSON_IsString(tz)) {
            cJSON_AddStringToObject(result, "timezone", tz->valuestring);
        }
    }

    cJSON* weather = cJSON_CreateObject();
    cJSON* time = cJSON_GetObjectItem(current, "time");
    if (cJSON_IsString(time)) {
        cJSON_AddStringToObject(weather, "time", time->valuestring);
    }
    cJSON* code = cJSON_GetObjectItem(current, "weather_code");
    if (cJSON_IsNumber(code)) {
        int weather_code = code->valueint;
        cJSON_AddNumberToObject(weather, "weather_code", weather_code);
        cJSON_AddStringToObject(weather, "text", WeatherCodeText(weather_code, lang));
    }
    AddNumberIfPresent(weather, current, "temperature_2m", "temperature");
    AddNumberIfPresent(weather, current, "apparent_temperature", "feels_like");
    AddNumberIfPresent(weather, current, "relative_humidity_2m", "humidity_percent");
    AddNumberIfPresent(weather, current, "precipitation", "precipitation");
    AddNumberIfPresent(weather, current, "rain", "rain");
    AddNumberIfPresent(weather, current, "showers", "showers");
    AddNumberIfPresent(weather, current, "snowfall", "snowfall");
    AddNumberIfPresent(weather, current, "cloud_cover", "cloud_percent");
    AddNumberIfPresent(weather, current, "pressure_msl", "pressure_msl_hpa");
    AddNumberIfPresent(weather, current, "surface_pressure", "surface_pressure_hpa");
    AddNumberIfPresent(weather, current, "wind_speed_10m", "wind_speed");
    AddNumberIfPresent(weather, current, "wind_direction_10m", "wind_direction_degrees");
    AddNumberIfPresent(weather, current, "wind_gusts_10m", "wind_gusts");
    AddNumberIfPresent(weather, current, "is_day", "is_day");
    cJSON_AddItemToObject(result, "current", weather);

    cJSON* units = cJSON_GetObjectItem(root, "current_units");
    if (cJSON_IsObject(units)) {
        cJSON* compact_units = cJSON_CreateObject();
        cJSON* temp_unit = cJSON_GetObjectItem(units, "temperature_2m");
        cJSON* wind_unit = cJSON_GetObjectItem(units, "wind_speed_10m");
        cJSON* precip_unit = cJSON_GetObjectItem(units, "precipitation");
        if (cJSON_IsString(temp_unit)) {
            cJSON_AddStringToObject(compact_units, "temperature", temp_unit->valuestring);
        }
        if (cJSON_IsString(wind_unit)) {
            cJSON_AddStringToObject(compact_units, "wind_speed", wind_unit->valuestring);
        }
        if (cJSON_IsString(precip_unit)) {
            cJSON_AddStringToObject(compact_units, "precipitation", precip_unit->valuestring);
        }
        cJSON_AddItemToObject(result, "units", compact_units);
    }

    std::string output = JsonString(result);
    cJSON_Delete(result);
    cJSON_Delete(root);
    return output;
}
