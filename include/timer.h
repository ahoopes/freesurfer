#ifndef TIMER_H
#define TIMER_H

#include <iostream>
#include <string>
#include <chrono>


class Timer
{
public:
    Timer() : begin(clock::now()) {}

    double minutes();
    double seconds();
    long milliseconds();
    long nanoseconds();

    void reset();

private:
    typedef std::chrono::high_resolution_clock clock;
    std::chrono::time_point<clock> begin;
};


#define TIMER_INTERVAL_BEGIN(NAME) Timer NAME;
#define TIMER_INTERVAL_END(NAME) std::cout << __FILE__ << ":" << __LINE__ << " interval took " << NAME.milliseconds() << " msec" << std::endl; 

std::string currentDateTime(bool allowOverride = true);


#endif
