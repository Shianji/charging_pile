#include "time.h"

// 初始化定时器
void timer_init(timer_t *timerid, int signum) {
    struct sigevent sev;
    struct sigaction sa;

    // 设置信号处理函数
    sa.sa_flags = SA_SIGINFO;
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);

    // 创建定时器
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = signum;
    sev.sigev_value.sival_ptr = timerid;
    timer_create(CLOCK_REALTIME, &sev, timerid);
}

// 设置定时器
void set_timer(timer_t timerid, int sec) {
    struct itimerspec its;

    its.it_value.tv_sec = sec;         // 定时时间 sec 秒
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;        // 不重复
    its.it_interval.tv_nsec = 0;
    timer_settime(timerid, 0, &its, NULL);
}

// 取消定时器
void cancel_timer(timer_t timerid) {
    struct itimerspec its;

    its.it_value.tv_sec = 0;           // 取消定时器
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    timer_settime(timerid, 0, &its, NULL);
}