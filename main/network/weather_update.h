// 声明整批天气同步结果和高层更新入口。
#pragma once

enum class WeatherUpdateResult {
    kSuccess,
    kFailed,
    kResourceDeferred,
};

WeatherUpdateResult perform_weather_update();
