#ifndef TIME__H
#define TIME__H
#include <signal.h>
#include <time.h>
#include <string.h>


extern void timer_handler(int signum);

void timer_init(timer_t *timerid, int signum);
void set_timer(timer_t timerid, int sec);
void cancel_timer(timer_t timerid);

#endif 