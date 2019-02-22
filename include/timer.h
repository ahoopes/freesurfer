#ifndef TIMER_H
#define TIMER_H

#include <chrono>


struct timeb *TimerStart(struct timeb *then) ;
int TimerStop(struct timeb *then) ;

#define PRINT_TIME_INTERVAL(NAME) std::cout << __FILE__ << ":" << __LINE__ << " interval took " << NAME.milliseconds() << " msec" << std::endl; 

class Timer
{
public:
    Timer() : begin(clock::now()) {}

    double seconds();
    long milliseconds();
    long nanoseconds();

    void reset();

private:
    typedef std::chrono::high_resolution_clock clock;
    std::chrono::time_point<clock> begin;
};


const char* current_date_time_noOverride();
const char* current_date_time();
std::string currentDateTime(bool allowOverride = true);


#endif
