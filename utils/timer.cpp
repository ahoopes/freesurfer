#include <stdio.h>
#include <stdlib.h>

#include "proto.h"
#include "sys/timeb.h"
#include "timer.h"


struct timeb *TimerStart(struct timeb *then)
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  then->time = tv.tv_sec;
  then->millitm = (tv.tv_usec + 500) / 1000;
  then->dstflag = 0;
  return then;
}


int TimerStop(struct timeb *then)
{
  int msec;
  struct timeval tvnow, tvthen;
  gettimeofday(&tvnow, 0);
  tvthen.tv_sec = then->time;
  tvthen.tv_usec = ((long)(then->millitm)) * 1000;
  msec = 1000 * (tvnow.tv_sec - tvthen.tv_sec) + (tvnow.tv_usec - tvthen.tv_usec + 500) / 1000;
  return msec;
}


// Use this string for writing into files etc. which are not compared by the test system.
//
const char* current_date_time_noOverride() {
  time_t tt = time(&tt);
  const char* time_str = ctime(&tt);
  return time_str;
}

// Use this string for writing into files etc. where it might be compared by
// the test system.  The string is usually the current date and time but
// can be overridden with a string that can the same across runs.
//
const char* current_date_time() {
  const char* override_time_str = getenv("FREESURFER_REPLACEMENT_FOR_CREATION_TIME_STRING");
  const char* time_str = (!!override_time_str) ? override_time_str : current_date_time_noOverride();
  return time_str;
}


std::string currentDateTime(bool allowOverride) {
    const char* overrideString = getenv("FREESURFER_REPLACEMENT_FOR_CREATION_TIME_STRING");
    if (allowOverride && overrideString) {
        return std::string(overrideString);
    } else {
        std::time_t now = std::time(nullptr);
        return std::ctime(&now);
    }
}


double Timer::seconds() {
    return std::chrono::duration_cast<std::chrono::duration<double>>(clock::now() - begin).count();
}


long Timer::milliseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - begin).count();
}


long Timer::nanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - begin).count();
}


void Timer::reset() {
    begin = clock::now()
}
