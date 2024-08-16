#ifndef COMMON__H
#define COMMON__H
#include <signal.h>
#include <pthread.h>
#include "gbt27930-2015.h"

typedef struct {
    int sockfd;
    const u_int8_t *frame;
    int cycletime_ms;
} thread_send_arg;

// extern void timer_handler(int signum);
extern void timer_handler(int signum, siginfo_t *info, void *context);

void timer_init(timer_t *timerid, int signum);
void set_timer(timer_t timerid, int sec);
void cancel_timer(timer_t timerid);
void *cycle_sent_frame(void *arg);
void kill_thread(pthread_t thread);

#endif 