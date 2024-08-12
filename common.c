#include "common.h"

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

//周期发送帧数据函数，cycletime_ms为循环发送周期，单位为毫秒，传入0表示只发送一次int sockfd, char *frame,int cycletime_ms
void *cycle_sent_frame(void *arg){
    thread_send_arg *parameter=(thread_send_arg *)arg;
    int ret, framelen=CAN_DATA_LEN;
    unsigned int cycletime_us = parameter->cycletime_ms * 1000;
    if(parameter->cycletime_ms==0){
        ret = send(parameter->sockfd, parameter->frame, framelen, 0);
        if(ret != framelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",framelen, ret);
        }
    }else{
        while(1){
        ret = send(parameter->sockfd, parameter->frame, framelen, 0);
        if(ret != framelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",framelen, ret);
        }
        // 等待指定的周期时间（以微秒为单位）
        usleep(cycletime_us);
        }
    }
}

//回收子线程函数
void kill_thread(pthread_t thread){
    int ret;
    ret = pthread_cancel(thread);
    if(ret!=0 && ret!=ESRCH){//判断线程是否存在,或是否成功杀死
        perror("pthread_cancel");
        exit(EXIT_FAILURE);
    }
    // 等待线程结束
    ret = pthread_join(thread, NULL);
    if (ret!=0 && ret!=ESRCH) {
        perror("pthread_join");
        exit(EXIT_FAILURE);
    }
}