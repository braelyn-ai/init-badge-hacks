#include "../firmware/factory_badge/main/schedule.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

int main() {
    using namespace badge_schedule;
    constexpr int starts[] = {480, 570, 630, 660, 750, 825, 900, 930, 990};
    assert(Items.size() == 9);
    assert(current(-1) == -1 && current(0) == -1 && current(479) == -1);
    assert(current(1440) == -1);
    assert(state(Items.size(), 600) == State::Unknown);
    for (size_t i = 0; i < Items.size(); ++i) {
        assert(Items[i].minute == starts[i]);
        // The public 12-hour label must describe the same start as the daily
        // scheduling value, with a two-digit minute and no leading hour zero.
        int hour = 0, minute = 0, consumed = 0;
        char meridiem[3] = {};
        assert(std::sscanf(Items[i].time, "%d:%d %2s%n", &hour, &minute, meridiem, &consumed) == 3);
        assert(hour >= 1 && hour <= 12 && minute >= 0 && minute < 60);
        assert(std::strcmp(meridiem, "AM") == 0 || std::strcmp(meridiem, "PM") == 0);
        assert(Items[i].time[0] != '0' && Items[i].time[consumed] == '\0');
        const char* colon = std::strchr(Items[i].time, ':');
        assert(colon && colon[1] >= '0' && colon[1] <= '9' &&
               colon[2] >= '0' && colon[2] <= '9' && colon[3] == ' ');
        assert((hour % 12 + (meridiem[0] == 'P' ? 12 : 0)) * 60 + minute == starts[i]);
        assert(current(starts[i]) == int(i));
        assert(current(endMinute(i) - 1) == int(i));
        assert(state(i, -1) == State::Unknown);
        assert(state(i, starts[i] - 1) == State::Upcoming);
        assert(state(i, starts[i]) == State::OnNow);
        if (i + 1 < Items.size()) assert(state(i, starts[i + 1]) == State::Passed);
    }
    // Every minute has exactly the expected state: the daily reset clears all
    // passed rows, and the last published start remains current until midnight.
    for (int minute = 0; minute < 1440; ++minute) {
        int onNow = 0;
        for (size_t i = 0; i < Items.size(); ++i) {
            const auto value = state(i, minute);
            if (value == State::OnNow) ++onNow;
            if (int(i) < current(minute)) assert(value == State::Passed);
            if (int(i) > current(minute)) assert(value == State::Upcoming);
        }
        assert(onNow == (minute >= 480 ? 1 : 0));
    }
    constexpr int64_t midnight = 1704067200; // 2024-01-01 00:00 UTC.
    assert(localMinute(midnight + 16 * 3600, -420, true) == 540);
    assert(localMinute(midnight + 4 * 3600, 330, true) == 570);
    assert(localMinute(midnight + 30 * 60, 60, true) == 90);
    assert(localMinute(midnight + 4 * 3600 + 30 * 60, -420, true) == 1290);
    assert(current(localMinute(midnight + 18 * 3600, -420, true)) == 3);
    assert(current(localMinute(midnight + 18 * 3600, -480, true)) == 1);
    // Same local times repeat on different dates, including the year boundary.
    for (int day : {0, 1, 31, 365, 366, 730})
        assert(current(localMinute(midnight + int64_t(day) * 86400 + 16 * 3600 + 30 * 60, -420, true)) == 1);
    assert(current(localMinute(midnight + 7 * 3600 - 1, -420, true)) == 8);
    assert(current(localMinute(midnight + 7 * 3600, -420, true)) == -1);
    assert(localMinute(midnight, 0, false) == -1);
    assert(localMinute(0, 0, true) == -1);
    assert(localMinute(-1, 0, true) == -1);
    assert(localMinute(midnight, 841, true) == -1);
    assert(localMinute(midnight, -841, true) == -1);
    const auto large = localMinute(std::numeric_limits<int64_t>::max(), 840, true);
    assert(large >= 0 && large < 1440);
    std::puts("Daily agenda: 12-hour labels, published starts, exact transitions, passed/upcoming states, midnight reset, date repetition, local offsets and invalid clocks passed");
}
