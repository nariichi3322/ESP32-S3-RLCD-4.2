// 计算公历、农历、节日和节气等日历页面数据。
#include "calendar_lunar.h"

#include "app_constexpr.h"
#include "app_text_format.h"
#include "app_time_constants.h"
#include "calendar_display_mode.h"
#include "ui_i18n.h"
#include "ui_language.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct LunarYearInfo {
    int year;
    uint32_t data;
};

struct CalendarFestivalRule {
    int month;
    int day;
    UiTextId text_id;
};

static constexpr LunarYearInfo kLunarYears[] = {
    {2023, 0x05b52},
    {2024, 0x04b60},
    {2025, 0x0a6e6},
    {2026, 0x0a4e0},
    {2027, 0x0d260},
    {2028, 0x0ea65},
    {2029, 0x0d530},
    {2030, 0x05aa0},
    {2031, 0x076a3},
    {2032, 0x096d0},
    {2033, 0x04afb},
    {2034, 0x04ad0},
    {2035, 0x0a4d0},
};

// Japan's modern Kyureki is not an officially promulgated calendar. These
// 2023-35 records were calculated from Meeus new-moon instants and apparent
// solar principal terms, assigned by JST civil date, and checked against
// NAOJ's published 2024-27 astronomical data. The 2033-34 ambiguity follows
// NAOJ's winter-solstice-priority option 1.
// Source notes: https://www.nao.ac.jp/news/blog/2023/20230829-koyomi.html
// and https://eco.mtk.nao.ac.jp/koyomi/wiki/C2C0B1A2C2C0CDDBCEF12F2033C7AFCCE4C2EA.html
static constexpr LunarYearInfo kJapaneseKyurekiYears[] = {
    {2023, 0x06b52},
    {2024, 0x04b60},
    {2025, 0x0a6e6},
    {2026, 0x0a570},
    {2027, 0x05270},
    {2028, 0x06a65},
    {2029, 0x0d930},
    {2030, 0x05aa0},
    {2031, 0x0b6a3},
    {2032, 0x096d0},
    {2033, 0x04afb},
    {2034, 0x04ae0},
    {2035, 0x0a4d0},
};

constexpr bool lunar_year_table_contiguous()
{
    for (size_t i = 1; i < array_count(kLunarYears); ++i) {
        if (kLunarYears[i].year != kLunarYears[i - 1].year + 1) {
            return false;
        }
    }
    return true;
}

constexpr bool japanese_kyureki_year_table_contiguous()
{
    for (size_t i = 1; i < array_count(kJapaneseKyurekiYears); ++i) {
        if (kJapaneseKyurekiYears[i].year != kJapaneseKyurekiYears[i - 1].year + 1) {
            return false;
        }
    }
    return true;
}

static const char *const kSolarTermNames[] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分",
    "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
    "小暑", "大暑", "立秋", "处暑", "白露", "秋分",
    "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

static constexpr int kFirstGregorianMonth = 1;
static constexpr int kLastGregorianMonth = 12;
static constexpr int kFebruaryMonth = 2;
static constexpr int kCommonFebruaryDays = 28;
static constexpr int kLeapFebruaryDays = 29;
static constexpr int kFallbackGregorianMonthDays = 30;
static constexpr int kLeapYearCycle4 = 4;
static constexpr int kLeapYearCycle100 = 100;
static constexpr int kLeapYearCycle400 = 400;
static constexpr int kFirstLunarMonth = 1;
static constexpr int kLastLunarMonth = 12;
static constexpr int kFirstLunarDay = 1;
static constexpr int kLunarSmallMonthDays = 29;
static constexpr int kLunarLargeMonthDays = 30;
static constexpr int kLunarYearBaseDays = kLastLunarMonth * kLunarSmallMonthDays;
static constexpr uint32_t kLunarLeapMonthMask = 0x0f;
static constexpr uint32_t kLunarLeapMonthDaysMask = 0x10000;
static constexpr uint32_t kLunarMonthDaysBaseMask = 0x10000;
static constexpr int kLunarYearDaysFirstMask = 0x8000;
static constexpr int kLunarYearDaysLastMask = 0x8;
static constexpr int kLunarBaseYear = 2023;
static constexpr int kLunarBaseMonth = 1;
static constexpr int kLunarBaseDay = 22;
static constexpr int kSolarTermsPerMonth = 2;
static constexpr int kSolarTermsPerYear = kLastGregorianMonth * kSolarTermsPerMonth;
static constexpr UiTextId kLunarDayTextIds[] = {
    UiTextId::Count,
    UiTextId::CalendarLunarDay1,
    UiTextId::CalendarLunarDay2,
    UiTextId::CalendarLunarDay3,
    UiTextId::CalendarLunarDay4,
    UiTextId::CalendarLunarDay5,
    UiTextId::CalendarLunarDay6,
    UiTextId::CalendarLunarDay7,
    UiTextId::CalendarLunarDay8,
    UiTextId::CalendarLunarDay9,
    UiTextId::CalendarLunarDay10,
    UiTextId::CalendarLunarDay11,
    UiTextId::CalendarLunarDay12,
    UiTextId::CalendarLunarDay13,
    UiTextId::CalendarLunarDay14,
    UiTextId::CalendarLunarDay15,
    UiTextId::CalendarLunarDay16,
    UiTextId::CalendarLunarDay17,
    UiTextId::CalendarLunarDay18,
    UiTextId::CalendarLunarDay19,
    UiTextId::CalendarLunarDay20,
    UiTextId::CalendarLunarDay21,
    UiTextId::CalendarLunarDay22,
    UiTextId::CalendarLunarDay23,
    UiTextId::CalendarLunarDay24,
    UiTextId::CalendarLunarDay25,
    UiTextId::CalendarLunarDay26,
    UiTextId::CalendarLunarDay27,
    UiTextId::CalendarLunarDay28,
    UiTextId::CalendarLunarDay29,
    UiTextId::CalendarLunarDay30,
};

static constexpr UiTextId kLunarMonthTextIds[] = {
    UiTextId::Count,
    UiTextId::CalendarLunarMonth1,
    UiTextId::CalendarLunarMonth2,
    UiTextId::CalendarLunarMonth3,
    UiTextId::CalendarLunarMonth4,
    UiTextId::CalendarLunarMonth5,
    UiTextId::CalendarLunarMonth6,
    UiTextId::CalendarLunarMonth7,
    UiTextId::CalendarLunarMonth8,
    UiTextId::CalendarLunarMonth9,
    UiTextId::CalendarLunarMonth10,
    UiTextId::CalendarLunarMonth11,
    UiTextId::CalendarLunarMonth12,
};

static UiTextId calendar_text_id(const char *text)
{
    if (!text) return UiTextId::Count;
    struct NameId { const char *traditional; const char *simplified; UiTextId id; };
    static constexpr NameId kNames[] = {
        {"小寒", "小寒", UiTextId::CalendarMinorCold},
        {"大寒", "大寒", UiTextId::CalendarMajorCold},
        {"立春", "立春", UiTextId::CalendarStartOfSpring},
        {"雨水", "雨水", UiTextId::CalendarRainWater},
        {"驚蟄", "惊蛰", UiTextId::CalendarAwakeningOfInsects},
        {"春分", "春分", UiTextId::CalendarSpringEquinox},
        {"清明", "清明", UiTextId::CalendarPureBrightness},
        {"穀雨", "谷雨", UiTextId::CalendarGrainRain},
        {"立夏", "立夏", UiTextId::CalendarStartOfSummer},
        {"小滿", "小满", UiTextId::CalendarGrainFull},
        {"芒種", "芒种", UiTextId::CalendarGrainInEar},
        {"夏至", "夏至", UiTextId::CalendarSummerSolstice},
        {"小暑", "小暑", UiTextId::CalendarMinorHeat},
        {"大暑", "大暑", UiTextId::CalendarMajorHeat},
        {"立秋", "立秋", UiTextId::CalendarStartOfAutumn},
        {"處暑", "处暑", UiTextId::CalendarEndOfHeat},
        {"白露", "白露", UiTextId::CalendarWhiteDew},
        {"秋分", "秋分", UiTextId::CalendarAutumnEquinox},
        {"寒露", "寒露", UiTextId::CalendarColdDew},
        {"霜降", "霜降", UiTextId::CalendarFrostDescent},
        {"立冬", "立冬", UiTextId::CalendarStartOfWinter},
        {"小雪", "小雪", UiTextId::CalendarMinorSnow},
        {"大雪", "大雪", UiTextId::CalendarMajorSnow},
        {"冬至", "冬至", UiTextId::CalendarWinterSolstice},
    };
    for (const NameId &name : kNames) {
        if (strcmp(text, name.traditional) == 0 || strcmp(text, name.simplified) == 0) {
            return name.id;
        }
    }
    return UiTextId::Count;
}

static_assert(kCalendarLunarSubtextSize >= sizeof("Dragon Boat Festival") &&
                  kCalendarLunarSubtextSize >= sizeof("Awakening of Insects") &&
                  kCalendarLunarSubtextSize >= sizeof("Leap lunar month 12"),
              "calendar English text must fit the subtext buffer");

static const int kGregorianMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static_assert(array_count(kGregorianMonthDays) == kLastGregorianMonth,
              "gregorian month days must cover January through December");

// 香港天文台公历与农历日期对照表中的节气日号，按小寒至冬至排列。
static constexpr uint8_t kSolarTermDays[][kSolarTermsPerYear] = {
    {6, 20, 4, 19, 5, 20, 4, 19, 5, 20, 5, 21, 6, 22, 7, 22, 7, 22, 8, 23, 7, 22, 6, 21}, // 2024
    {5, 20, 3, 18, 5, 20, 4, 20, 5, 21, 5, 21, 7, 22, 7, 23, 7, 23, 8, 23, 7, 22, 7, 21}, // 2025
    {5, 20, 4, 18, 5, 20, 5, 20, 5, 21, 5, 21, 7, 23, 7, 23, 7, 23, 8, 23, 7, 22, 7, 22}, // 2026
    {5, 20, 4, 19, 6, 21, 5, 20, 6, 21, 6, 21, 7, 23, 8, 23, 8, 23, 8, 23, 7, 22, 7, 22}, // 2027
    {6, 20, 4, 19, 5, 20, 4, 19, 5, 20, 5, 21, 6, 22, 7, 22, 7, 22, 8, 23, 7, 22, 6, 21}, // 2028
    {5, 20, 3, 18, 5, 20, 4, 20, 5, 21, 5, 21, 7, 22, 7, 23, 7, 23, 8, 23, 7, 22, 7, 21}, // 2029
    {5, 20, 4, 18, 5, 20, 5, 20, 5, 21, 5, 21, 7, 23, 7, 23, 7, 23, 8, 23, 7, 22, 7, 22}, // 2030
    {5, 20, 4, 19, 6, 21, 5, 20, 6, 21, 6, 21, 7, 23, 8, 23, 8, 23, 8, 23, 7, 22, 7, 22}, // 2031
    {6, 20, 4, 19, 5, 20, 4, 19, 5, 20, 5, 21, 6, 22, 7, 22, 7, 22, 8, 23, 7, 22, 6, 21}, // 2032
    {5, 20, 3, 18, 5, 20, 4, 20, 5, 21, 5, 21, 7, 22, 7, 23, 7, 23, 8, 23, 7, 22, 7, 21}, // 2033
    {5, 20, 4, 18, 5, 20, 5, 20, 5, 21, 5, 21, 7, 23, 7, 23, 7, 23, 8, 23, 7, 22, 7, 22}, // 2034
    {5, 20, 4, 19, 6, 21, 5, 20, 5, 21, 6, 21, 7, 23, 7, 23, 8, 23, 8, 23, 7, 22, 7, 22}, // 2035
};

static constexpr CalendarFestivalRule kGregorianFestivals[] = {
    {1, 1, UiTextId::CalendarNewYear},
    {2, 14, UiTextId::CalendarValentinesDay},
    {5, 1, UiTextId::CalendarLabourDay},
    {12, 25, UiTextId::CalendarChristmas},
};

static constexpr CalendarFestivalRule kTaiwanGregorianFestivals[] = {
    {2, 28, UiTextId::CalendarTaiwanPeaceMemorialDay},
    {4, 4, UiTextId::CalendarChildrensDay},
    {9, 28, UiTextId::CalendarTeachersDay},
    {10, 10, UiTextId::CalendarNationalDay},
    {10, 25, UiTextId::CalendarTaiwanRetrocessionDay},
    {12, 25, UiTextId::CalendarTaiwanConstitutionDay},
};

static constexpr CalendarFestivalRule kChinaGregorianFestivals[] = {
    {3, 8, UiTextId::CalendarWomensDay},
    {6, 1, UiTextId::CalendarChildrensDay},
    {9, 10, UiTextId::CalendarTeachersDay},
    {10, 1, UiTextId::CalendarNationalDay},
};

static constexpr CalendarFestivalRule kJapanGregorianFestivals[] = {
    {2, 11, UiTextId::CalendarJapaneseNationalFoundationDay},
    {2, 23, UiTextId::CalendarJapaneseEmperorsBirthday},
    {4, 29, UiTextId::CalendarJapaneseShowaDay},
    {5, 3, UiTextId::CalendarJapaneseConstitutionMemorialDay},
    {5, 4, UiTextId::CalendarJapaneseGreeneryDay},
    {5, 5, UiTextId::CalendarChildrensDay},
    {8, 11, UiTextId::CalendarJapaneseMountainDay},
    {11, 3, UiTextId::CalendarJapaneseCultureDay},
    {11, 23, UiTextId::CalendarJapaneseLaborThanksgivingDay},
};

static constexpr CalendarFestivalRule kLunarFestivals[] = {
    {1, 1, UiTextId::CalendarSpringFestival},
    {1, 15, UiTextId::CalendarLanternFestival},
    {5, 5, UiTextId::CalendarDragonBoatFestival},
    {7, 7, UiTextId::CalendarQixiFestival},
    {8, 15, UiTextId::CalendarMidAutumnFestival},
    {9, 9, UiTextId::CalendarDoubleNinthFestival},
    {12, 8, UiTextId::CalendarLabaFestival},
};
static_assert(array_count(kLunarYears) > 0, "lunar year table must not be empty");
static_assert(kLunarYears[0].year <= kMinValidYear &&
                  kLunarYears[array_count(kLunarYears) - 1].year >= kMaxValidYear,
              "lunar year table must cover the supported calendar range");
static_assert(lunar_year_table_contiguous(), "lunar year table must stay contiguous");
static_assert(kJapaneseKyurekiYears[0].year <= kMinValidYear &&
                  kJapaneseKyurekiYears[array_count(kJapaneseKyurekiYears) - 1].year >=
                      kMaxValidYear,
              "Japanese Kyureki year table must cover the supported calendar range");
static_assert(japanese_kyureki_year_table_contiguous(),
              "Japanese Kyureki year table must stay contiguous");
static_assert(array_count(kLunarDayTextIds) == static_cast<size_t>(kLunarLargeMonthDays + 1),
              "lunar day names must cover day zero through day thirty");
static_assert(array_count(kLunarMonthTextIds) == static_cast<size_t>(kLastLunarMonth + 1),
              "lunar month names must cover month zero through month twelve");
static_assert(array_count(kSolarTermNames) == kLastGregorianMonth * kSolarTermsPerMonth,
              "solar term names must cover two terms per month");
static_assert(array_count(kSolarTermDays) ==
                  static_cast<size_t>(kMaxValidYear - kMinValidYear + 1),
              "solar term table must cover the supported calendar range");
static_assert(array_count(kSolarTermDays[0]) == kSolarTermsPerYear,
              "solar term table must cover all terms in each year");

static int days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static size_t utf8_character_size(const unsigned char *text, size_t remaining)
{
    if (!text || remaining == 0) return 0;
    size_t size = 1;
    if (text[0] < 0x80) {
        return 1;
    }
    if ((text[0] & 0xe0) == 0xc0) {
        size = 2;
    } else if ((text[0] & 0xf0) == 0xe0) {
        size = 3;
    } else if ((text[0] & 0xf8) == 0xf0) {
        size = 4;
    } else {
        return 0;
    }
    if (size > remaining) return 0;
    for (size_t index = 1; index < size; ++index) {
        if ((text[index] & 0xc0) != 0x80) return 0;
    }
    return size;
}

static void utf8_safe_copy(char *out, size_t out_len, const char *text)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!text) return;

    const unsigned char *source = reinterpret_cast<const unsigned char *>(text);
    const size_t source_len = strlen(text);
    size_t source_offset = 0;
    size_t output_offset = 0;
    while (source_offset < source_len && output_offset + 1 < out_len) {
        const size_t character_size = utf8_character_size(source + source_offset,
                                                          source_len - source_offset);
        if (character_size == 0) {
            out[output_offset++] = '?';
            ++source_offset;
            continue;
        }
        if (output_offset + character_size >= out_len) break;
        memcpy(out + output_offset, source + source_offset, character_size);
        output_offset += character_size;
        source_offset += character_size;
    }
    out[output_offset] = '\0';
}

static const LunarYearInfo *find_lunar_year(const LunarYearInfo *years,
                                            size_t year_count,
                                            int year)
{
    for (size_t index = 0; index < year_count; ++index) {
        if (years[index].year == year) {
            return &years[index];
        }
    }
    return nullptr;
}

static bool is_gregorian_leap_year(int year)
{
    return (year % kLeapYearCycle4 == 0 && year % kLeapYearCycle100 != 0) ||
           (year % kLeapYearCycle400 == 0);
}

static int leap_month(uint32_t data)
{
    return (int)(data & kLunarLeapMonthMask);
}

static int leap_month_days(uint32_t data)
{
    if (leap_month(data) == 0) {
        return 0;
    }
    return (data & kLunarLeapMonthDaysMask) ? kLunarLargeMonthDays : kLunarSmallMonthDays;
}

static int lunar_month_days(uint32_t data, int month)
{
    return (data & (kLunarMonthDaysBaseMask >> month)) ? kLunarLargeMonthDays : kLunarSmallMonthDays;
}

static int lunar_year_days(uint32_t data)
{
    int days = kLunarYearBaseDays;
    for (int mask = kLunarYearDaysFirstMask; mask > kLunarYearDaysLastMask; mask >>= 1) {
        if (data & mask) {
            ++days;
        }
    }
    return days + leap_month_days(data);
}

int calendar_days_in_month(int year, int month)
{
    if (month < kFirstGregorianMonth || month > kLastGregorianMonth) {
        return kFallbackGregorianMonthDays;
    }
    if (month == kFebruaryMonth) {
        return is_gregorian_leap_year(year) ? kLeapFebruaryDays : kCommonFebruaryDays;
    }
    return kGregorianMonthDays[month - kFirstGregorianMonth];
}

int calendar_first_weekday(int year, int month)
{
    int days = days_from_civil(year, (unsigned)month, 1);
    int weekday = (days + 4) % 7;
    return weekday < 0 ? weekday + 7 : weekday;
}

static bool lunar_from_date(int year,
                            int month,
                            int day,
                            const LunarYearInfo *years,
                            size_t year_count,
                            CalendarDayInfo *info)
{
    int offset = days_from_civil(year, (unsigned)month, (unsigned)day) -
                 days_from_civil(kLunarBaseYear, kLunarBaseMonth, kLunarBaseDay);
    if (offset < 0) {
        return false;
    }

    int lunar_year = kLunarBaseYear;
    const LunarYearInfo *year_info = find_lunar_year(years, year_count, lunar_year);
    while (year_info) {
        int days = lunar_year_days(year_info->data);
        if (offset < days) {
            break;
        }
        offset -= days;
        ++lunar_year;
        year_info = find_lunar_year(years, year_count, lunar_year);
    }
    if (!year_info) {
        return false;
    }

    int lunar_month = kFirstLunarMonth;
    bool is_leap = false;
    int leap = leap_month(year_info->data);
    for (;;) {
        int days = is_leap ? leap_month_days(year_info->data) : lunar_month_days(year_info->data, lunar_month);
        if (offset < days) {
            break;
        }
        offset -= days;
        if (leap == lunar_month && !is_leap) {
            is_leap = true;
        } else {
            is_leap = false;
            ++lunar_month;
        }
        if (lunar_month > kLastLunarMonth) {
            return false;
        }
    }

    info->lunar_year = lunar_year;
    info->lunar_month = lunar_month;
    info->lunar_day = offset + 1;
    info->lunar_leap = is_leap;
    return true;
}

template <size_t N>
static UiTextId find_gregorian_festival(const CalendarFestivalRule (&festivals)[N],
                                        int month,
                                        int day)
{
    for (const auto &festival : festivals) {
        if (month == festival.month && day == festival.day) {
            return festival.text_id;
        }
    }
    return UiTextId::Count;
}

static UiTextId gregorian_festival(int month, int day)
{
    UiTextId festival = UiTextId::Count;
    // Firmware locale selects the matching regional list. English has no
    // implied country, so it receives only the common fixed-date entries.
    switch (ui_language_load()) {
    case UiLanguage::Traditional:
        festival = find_gregorian_festival(kTaiwanGregorianFestivals, month, day);
        break;
    case UiLanguage::Simplified:
        festival = find_gregorian_festival(kChinaGregorianFestivals, month, day);
        break;
    case UiLanguage::Japanese:
        festival = find_gregorian_festival(kJapanGregorianFestivals, month, day);
        break;
    case UiLanguage::English:
    default:
        break;
    }
    return festival != UiTextId::Count
               ? festival
               : find_gregorian_festival(kGregorianFestivals, month, day);
}

static UiTextId lunar_festival(int lunar_month, int lunar_day)
{
    for (const auto &festival : kLunarFestivals) {
        if (lunar_month == festival.month && lunar_day == festival.day) {
            return festival.text_id;
        }
    }
    return UiTextId::Count;
}

static void set_calendar_subtext(CalendarDayInfo *info, const char *text)
{
    const UiTextId id = calendar_text_id(text);
    if (id != UiTextId::Count) {
        utf8_safe_copy(info->subtext, sizeof(info->subtext), ui_text(id));
        return;
    }
    utf8_safe_copy(info->subtext,
                   sizeof(info->subtext),
                   text ? ui_language_localize(text)
                        : ui_text(UiTextId::UiPlaceholder));
}

static void set_calendar_subtext_id(CalendarDayInfo *info, UiTextId id)
{
    utf8_safe_copy(info->subtext, sizeof(info->subtext), ui_text(id));
}

static void set_calendar_lunar_month_subtext(CalendarDayInfo *info)
{
    if (ui_language_is_japanese()) {
        char month_text[sizeof(info->subtext)];
        const int month_written = snprintf(month_text,
                                           sizeof(month_text),
                                           ui_format(info->lunar_leap
                                                         ? UiTextId::CalendarJapaneseLeapLunarMonthFormat
                                                         : UiTextId::CalendarJapaneseLunarMonthFormat),
                                           info->lunar_month);
        const int written = app_text::format_failed(month_written, sizeof(month_text))
                                 ? -1
                                 : snprintf(info->subtext,
                                             sizeof(info->subtext),
                                             "%s",
                                             month_text);
        if (!app_text::format_failed(written, sizeof(info->subtext))) return;
    }
    if (ui_language_is_english()) {
        const int written = snprintf(info->subtext, sizeof(info->subtext),
                                     ui_format(info->lunar_leap
                                                   ? UiTextId::CalendarEnglishLeapLunarMonthFormat
                                                   : UiTextId::CalendarLunarMonth),
                                     info->lunar_month);
        if (!app_text::format_failed(written, sizeof(info->subtext))) return;
        set_calendar_subtext(info, nullptr);
        return;
    }
    int written = snprintf(info->subtext, sizeof(info->subtext),
                           ui_format(UiTextId::CalendarLunarMonthJoinFormat),
                           info->lunar_leap ? ui_text(UiTextId::CalendarLeap) : "",
                           ui_text(kLunarMonthTextIds[info->lunar_month]));
    if (app_text::format_failed(written, sizeof(info->subtext))) {
        set_calendar_subtext(info, nullptr);
    }
}

static const char *solar_term(int year, int month, int day)
{
    if (year < kMinValidYear || year > kMaxValidYear ||
        month < kFirstGregorianMonth || month > kLastGregorianMonth) {
        return nullptr;
    }
    int first = (month - kFirstGregorianMonth) * kSolarTermsPerMonth;
    const uint8_t *year_days = kSolarTermDays[year - kMinValidYear];
    for (int i = 0; i < kSolarTermsPerMonth; ++i) {
        int term = first + i;
        if (year_days[term] == day) {
            return kSolarTermNames[term];
        }
    }
    return nullptr;
}

const char *calendar_lunar_day_text(const CalendarDayInfo &info)
{
    return info.lunar_day >= 1 && info.lunar_day <= 30
               ? ui_text(kLunarDayTextIds[info.lunar_day])
               : ui_text(UiTextId::UiPlaceholder);
}

const char *calendar_lunar_month_text(const CalendarDayInfo &info)
{
    return info.lunar_month >= 1 && info.lunar_month <= 12
               ? ui_text(kLunarMonthTextIds[info.lunar_month])
               : ui_text(UiTextId::UiPlaceholder);
}

bool calendar_day_info(const struct tm &local, CalendarDayInfo *info)
{
    if (!info) {
        return false;
    }
    *info = {};
    info->year = local.tm_year + kTmYearOffset;
    info->month = local.tm_mon + kTmMonthOffset;
    info->day = local.tm_mday;
    if (info->year < kMinValidYear || info->year > kMaxValidYear) {
        set_calendar_subtext(info, nullptr);
        return false;
    }

    const CalendarDisplayMode display_mode = calendar_display_mode_load();
    const LunarYearInfo *lunar_years = nullptr;
    size_t lunar_year_count = 0;
    if (display_mode == CalendarDisplayMode::ChineseLunar) {
        lunar_years = kLunarYears;
        lunar_year_count = array_count(kLunarYears);
    } else if (display_mode == CalendarDisplayMode::JapaneseKyureki) {
        lunar_years = kJapaneseKyurekiYears;
        lunar_year_count = array_count(kJapaneseKyurekiYears);
    }
    const bool lunar_ok = lunar_years &&
                          lunar_from_date(info->year,
                                          info->month,
                                          info->day,
                                          lunar_years,
                                          lunar_year_count,
                                          info);
    info->lunar_available = lunar_ok;
    const UiTextId festival_id = gregorian_festival(info->month, info->day);
    if (festival_id != UiTextId::Count) {
        set_calendar_subtext_id(info, festival_id);
        return lunar_ok;
    }
    const char *text = display_mode == CalendarDisplayMode::Off
                           ? nullptr
                           : solar_term(info->year, info->month, info->day);
    if (!text && display_mode == CalendarDisplayMode::ChineseLunar &&
        lunar_ok && !info->lunar_leap) {
        const UiTextId lunar_festival_id =
            lunar_festival(info->lunar_month, info->lunar_day);
        if (lunar_festival_id != UiTextId::Count) {
            set_calendar_subtext_id(info, lunar_festival_id);
            return lunar_ok;
        }
    }
    if (!text && lunar_ok) {
        if (info->lunar_day == kFirstLunarDay &&
            info->lunar_month >= kFirstLunarMonth &&
            info->lunar_month <= kLastLunarMonth) {
            set_calendar_lunar_month_subtext(info);
            return true;
        }
        if (info->lunar_day >= kFirstLunarDay && info->lunar_day <= kLunarLargeMonthDays) {
            if (ui_language_is_japanese()) {
                const int written = snprintf(info->subtext,
                                             sizeof(info->subtext),
                                             ui_format(UiTextId::CalendarJapaneseLunarDayFormat),
                                             info->lunar_day);
                if (!app_text::format_failed(written, sizeof(info->subtext))) return lunar_ok;
            }
            if (ui_language_is_english()) {
                const int written = snprintf(info->subtext, sizeof(info->subtext),
                                             ui_format(UiTextId::CalendarLunarDay), info->lunar_day);
                if (!app_text::format_failed(written, sizeof(info->subtext))) return lunar_ok;
            }
            text = ui_text(kLunarDayTextIds[info->lunar_day]);
        }
    }
    if (display_mode == CalendarDisplayMode::Off && !text) {
        info->subtext[0] = '\0';
    } else {
        set_calendar_subtext(info, text);
    }
    return lunar_ok;
}
