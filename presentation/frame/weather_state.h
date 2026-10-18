/*!
 * @file weather_state.h
 * @brief 天候（雨・風・気温）を画面へ運ぶ形
 *
 * ## 何を運ぶか
 * TOband の原作は天候を `s16b weather[3]`（雨・風・気温。0〜15）で持ち、`weather_level(v) = v / 4` で
 * 4 段に丸めて見せている（`xtra1.c` の `prt_weather()`）。ここにはその**生の値**を運び、段への丸めは
 * 画面側が同じ式（`weather_level()`）でやる。
 *
 * - 雨：0 快晴・1 曇り・2 雨（気温の段が 2 以上なら雪）・3 豪雨（同じく豪雪）
 * - 風：0 凪・1 微風・2 強風・3 暴風
 * - 気温：**値が大きいほど寒い**（0 酷暑・1 暑い・2 寒い・3 極寒）
 *
 * ## 申告
 * `declared` が偽（既定）なら「送っていない」。画面側は天候に関わる表示・粒・光を一切変えない。
 * 追加だけの欄なのでプロトコルの版は上げない（v1 §3.2）。
 */
#pragma once

#include <algorithm>
#include <cstdint>

//! 値の上限（原作の `MAX_WEATHER_VAL`）。
constexpr int kWeatherMaxValue = 15;

//! 4 段への丸め（原作の `weather_level(val)` と同じ式）。範囲外は丸める。
constexpr int weather_level_of(int value)
{
    return std::clamp(value, 0, kWeatherMaxValue) / 4;
}

struct WeatherState {
    //! コアが天候を申告したか。偽なら以下は読まない。
    bool declared{ false };
    //! 雨の値（0〜15）。
    uint8_t rain{ 0 };
    //! 風の値（0〜15）。
    uint8_t wind{ 0 };
    //! 気温の値（0〜15。大きいほど寒い）。
    uint8_t temp{ 0 };

    bool operator==(const WeatherState &o) const
    {
        return declared == o.declared && rain == o.rain && wind == o.wind && temp == o.temp;
    }
    bool operator!=(const WeatherState &o) const { return !(*this == o); }

    int rain_level() const { return weather_level_of(rain); }
    int wind_level() const { return weather_level_of(wind); }
    int temp_level() const { return weather_level_of(temp); }
};

//! 降るものの種類。
enum class Precipitation : uint8_t {
    None = 0, //!< 降っていない（快晴・曇り）
    Rain, //!< 雨（雨の段 2 以上・気温の段 1 以下）
    Snow, //!< 雪（雨の段 2 以上・気温の段 2 以上）
};

//! 降り方。`heavy` は雨の段（2 = 雨・雪、3 = 豪雨・豪雪）。降っていなければ 0。
struct PrecipitationLook {
    Precipitation kind{ Precipitation::None };
    int heavy{ 0 };
};

inline PrecipitationLook weather_precipitation(const WeatherState &w)
{
    PrecipitationLook out;
    if (!w.declared || (w.rain_level() < 2)) {
        return out;
    }
    out.heavy = w.rain_level();
    out.kind = (w.temp_level() >= 2) ? Precipitation::Snow : Precipitation::Rain;
    return out;
}

//! 暴風雨・暴風雪か（雨と風がどちらも 3 段。原作の `prt_weather()` と同じ条件）。
inline bool weather_is_storm(const WeatherState &w)
{
    return w.declared && (w.rain_level() == 3) && (w.wind_level() == 3);
}

/*!
 * @brief アイコンの番号（`assets/ui/hud/weather_icons.png` の左から。`tools/hud/gen_weather_icons.py`）。
 * @details 0 快晴・1 曇り・2 雨・3 豪雨・4 雪・5 豪雪・6 暴風雨・7 暴風雪。段の決め方は原作の `prt_weather()`
 * （雨の段 2 以上で、気温の段が 2 以上なら雪。雨と風がどちらも段 3 なら暴風雨・暴風雪）と同じ。
 */
constexpr int kWeatherIconCount = 8;
inline int weather_icon_index(const WeatherState &w)
{
    const bool snow = (w.temp_level() >= 2);
    if (weather_is_storm(w)) {
        return snow ? 7 : 6;
    }
    switch (w.rain_level()) {
    case 3:
        return snow ? 5 : 3;
    case 2:
        return snow ? 4 : 2;
    case 1:
        return 1;
    default:
        return 0;
    }
}
