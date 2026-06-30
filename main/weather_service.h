#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include <string>

class WeatherService {
public:
    static std::string GetCurrentWeather(const std::string& location,
                                         const std::string& latitude,
                                         const std::string& longitude,
                                         const std::string& unit,
                                         const std::string& lang);
};

#endif // WEATHER_SERVICE_H
