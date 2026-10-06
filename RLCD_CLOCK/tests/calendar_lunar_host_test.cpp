// Verifies calendar mode selection, Japanese Kyureki boundaries, and Off output.
#include "calendar_display_mode.h"
#include "calendar_lunar.h"
#include "ui_language_internal.h"

#include <assert.h>
#include <string.h>
#include <time.h>

namespace {
struct tm make_date(int year, int month, int day)
{
    struct tm local = {};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    return local;
}

void expect_lunar_festival(UiLanguage language, const char *expected)
{
    ui_language_store(language);
    calendar_display_mode_store(CalendarDisplayMode::ChineseLunar);
    CalendarDayInfo info = {};
    assert(calendar_day_info(make_date(2027, 2, 6), &info));
    assert(strcmp(info.subtext, expected) == 0);
}

void expect_gregorian_festival(UiLanguage language,
                               int year,
                               int month,
                               int day,
                               const char *expected)
{
    ui_language_store(language);
    CalendarDayInfo info = {};
    assert(!calendar_day_info(make_date(year, month, day), &info));
    assert(strcmp(info.subtext, expected) == 0);
}
} // namespace

int main()
{
    CalendarDayInfo info = {};

    calendar_display_mode_store(CalendarDisplayMode::ChineseLunar);
    assert(calendar_day_info(make_date(2027, 2, 6), &info));
    assert(info.lunar_available);
    assert(info.lunar_month == 1 && info.lunar_day == 1);
    assert(strcmp(info.subtext, "春節") == 0);

    expect_lunar_festival(UiLanguage::Traditional, "春節");
    expect_lunar_festival(UiLanguage::Simplified, "春节");
    expect_lunar_festival(UiLanguage::English, "Spring Festival");
    expect_lunar_festival(UiLanguage::Japanese, "春祭り");

    calendar_display_mode_store(CalendarDisplayMode::JapaneseKyureki);
    assert(calendar_day_info(make_date(2027, 2, 6), &info));
    assert(info.lunar_available);
    assert(info.lunar_month == 12 && info.lunar_day == 30);
    assert(calendar_day_info(make_date(2027, 2, 7), &info));
    assert(info.lunar_month == 1 && info.lunar_day == 1);

    // NAOJ's winter-solstice-priority option 1 places a leap 11th month in
    // December 2033 for this project's modern Japanese old-calendar table.
    assert(calendar_day_info(make_date(2033, 12, 22), &info));
    assert(info.lunar_year == 2033);
    assert(info.lunar_month == 11 && info.lunar_day == 1 && info.lunar_leap);
    ui_language_store(UiLanguage::Japanese);
    assert(calendar_day_info(make_date(2025, 7, 25), &info));
    assert(info.lunar_month == 6 && info.lunar_leap);
    assert(strcmp(info.subtext, "うるう6月") == 0);
    ui_language_store(UiLanguage::Traditional);

    calendar_display_mode_store(CalendarDisplayMode::ChineseLunar);
    assert(calendar_day_info(make_date(2027, 2, 4), &info));
    assert(strcmp(info.subtext, "立春") == 0);

    calendar_display_mode_store(CalendarDisplayMode::Off);
    assert(!calendar_day_info(make_date(2027, 2, 4), &info));
    assert(info.subtext[0] == '\0');
    assert(!calendar_day_info(make_date(2027, 2, 8), &info));
    assert(!info.lunar_available);
    assert(info.subtext[0] == '\0');
    assert(!calendar_day_info(make_date(2027, 1, 1), &info));
    assert(!info.lunar_available);
    assert(info.subtext[0] != '\0');

    expect_gregorian_festival(UiLanguage::Traditional, 2027, 2, 28, "和平紀念日");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 4, 4, "兒童節");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 9, 28, "教師節");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 10, 10, "國慶");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 10, 25, "光復節");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 12, 25, "行憲紀念日");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 10, 1, "");
    expect_gregorian_festival(UiLanguage::Traditional, 2027, 9, 10, "");

    expect_gregorian_festival(UiLanguage::Simplified, 2027, 3, 8, "妇女节");
    expect_gregorian_festival(UiLanguage::Simplified, 2027, 6, 1, "儿童节");
    expect_gregorian_festival(UiLanguage::Simplified, 2027, 9, 10, "教师节");
    expect_gregorian_festival(UiLanguage::Simplified, 2027, 10, 1, "国庆");
    expect_gregorian_festival(UiLanguage::Simplified, 2027, 2, 28, "");

    expect_gregorian_festival(UiLanguage::Japanese, 2027, 2, 11, "建国記念の日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 2, 23, "てんのう誕生日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 4, 29, "しょうわの日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 5, 1, "メーデー");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 5, 3, "けんぽう記念日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 5, 4, "みどりの日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 5, 5, "こどもの日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 8, 11, "山の日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 11, 3, "文化の日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 11, 23, "勤労感謝の日");
    expect_gregorian_festival(UiLanguage::Japanese, 2027, 10, 1, "");

    expect_gregorian_festival(UiLanguage::English, 2027, 1, 1, "New Year's Day");
    expect_gregorian_festival(UiLanguage::English, 2027, 5, 1, "Labour Day");
    expect_gregorian_festival(UiLanguage::English, 2027, 12, 25, "Christmas");
    expect_gregorian_festival(UiLanguage::English, 2027, 6, 1, "");
    expect_gregorian_festival(UiLanguage::English, 2027, 2, 11, "");

    return 0;
}
