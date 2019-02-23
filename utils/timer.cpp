#include "sys/timeb.h"
#include "timer.h"


double Timer::minutes() {
    return std::chrono::duration_cast<std::chrono::duration<double, std::ratio<60>>>(clock::now() - begin).count();
}


double Timer::seconds() {
    return std::chrono::duration_cast<std::chrono::duration<double>>(clock::now() - begin).count();
}


long Timer::milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - begin).count();
}


long Timer::nanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - begin).count();
}


void Timer::reset() {
    begin = clock::now();
}


// Use this string for writing into files etc. which are not compared by the test system.
// Use this string for writing into files etc. where it might be compared by
// the test system.  The string is usually the current date and time but
// can be overridden with a string that can the same across runs.

std::string currentDateTime(bool allowOverride) {
    const char* overrideString = getenv("FREESURFER_REPLACEMENT_FOR_CREATION_TIME_STRING");
    if (allowOverride && overrideString) {
        return overrideString;
    } else {
        char datetime[1000];
        time_t t = std::time(nullptr);
        struct tm *now = localtime(&t);
        std::strftime(datetime, 1000, "%c", now);
        return datetime;
    }
}
