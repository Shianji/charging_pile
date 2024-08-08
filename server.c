#include <signal.h>
#include <pthread.h>
#include "gbt27930-2015.h"
#include "time.h"


#define SERVER_PORT		8888          	//服务器的端口号
#define NUM_BCHILDREN    11

#define         BFRAME_BHM             " 1826F456#010100"                        //充电机握手报文
#define         BFRAME_BRM             " 1CECF456#110701FFFF000200"              //BMS和车辆辨识报文    //多帧1
#define         BFRAME_BCP             " 1CEC56F4#100D0002FF000600"              //动力蓄电池充电参数报文    //多帧1
#define         BFRAME_BRO_00          " 100956F4#00"                            //车辆充电准备就绪状态报文
#define         BFRAME_BRO_AA          " 100956F4#AA"                            //车辆充电准备就绪状态报文
#define         BFRAME_BCL             " 181056F4#A510D80E02"                    //电池充电需求报文
#define         BFRAME_BCS             " 1CEC56F4#10090002FF001100"              //电池充电总状态报文    //多帧1
#define         BFRAME_BSM             " 181356F4#1E3606350F0010"                //动力蓄电池状态信息报文
#define         BFRAME_BST             " 101956F4#40000000"                      //车辆中止充电报文
#define         BFRAME_BSD             " 181C56F4#4D4D014D013536"                //车辆统计数据报文

int sockfd;//套接字通信接口
int got_cst=0,got_csd=0,bcan_over=0;
pthread_t beventThread,bsendThread1,bsendThread2,bsendThread3,bsendThread4;
timer_t btimerid1, btimerid2,btimerid3;

// 定义状态
typedef enum{
    BSTATE_INIT,                                        // 开始状态
    BSTATE_CONNECT_CONFIRM,                             // 车辆接口连接确认完毕
    BSTATE_CYCLE_SENT_BHM,                              // 周期发送BHM报文
    BSTATE_CYCLE_SENT_BRM,                              // 周期发送BRM报文   
    BSTATE_CYCLE_SENT_BCP,                              // 周期发送BCP报文
    BSTATE_PARAMETER_ADAPT,                             // 充电机参数适配
    BSTATE_CYCLE_SENT_BRO_00,                           // 周期发送BCP_00报文      
    BSTATE_CYCLE_SENT_BRO_AA,                           // 周期发送BCP_AA报文 
    BSTATE_RCV_CRO,                                     //收到CRO
    BSTATE_CYCLE_SENT_BCL_BCS,                          // 周期发送BCL BCS报文 
    BSTATE_CYCLE_SENT_BSM,                              // 周期发送BSM报文
    BSTATE_CYCLE_SENT_BST,                              // 周期发送BST报文
    BSTATE_CYCLE_SENT_BSD,                              // 周期发送BSD报文
    BSTATE_EXIT                                         //退出    
} BMS_STATE;

// 定义事件
typedef enum {
    BEVENT_START,                                        // 准备开始充电
    BEVENT_RECV_CHM,                                     // 已经收到CHM
    BEVENT_RECV_CRM_00,                                  //已经收到CRM_00
    BEVENT_RECV_CRM_AA,                                  //已经收到CRM_AA
    BEVENT_RECV_CML,                                     // 已经收到CML报文,自首次发送BCP报文起5S，没收到CML触发超时
    BEVENT_PARAMETER_ADAPT,                              // 充电机参数适合
    BEVENT_CHARGE_READY,                                 // 充电准备就绪
    BEVENT_RECV_CRO,                                     // 已经收到CRO报文，自首次发送BRO报文起5S，没收到CRO触发超时
    BEVENT_RECV_CRO_AA,                                  // 已经收到CRO报文(AA)，自首次发送BRO报文起60S，没收到CRO(AA)触发超时
    BEVENT_RECV_CCS,                                     // 已经收到CCS报文 ,自首次收到CRO报文起1S，没收到CCS触发超时
    BEVENT_RECV_CST,                                     // 已经收到CST报文,自首次发送BST报文起5S，没收到CST触发超时
    BEVENT_RECV_CSD,                                     // 已经收到CSD报文,自首次发送BST报文起10S，没收到CSD触发超时
    BEVENT_EXIT,                                         // 退出事件
} Event;

typedef struct {
    BMS_STATE currentState;
    Event currentEvent;
} StateMachine;

StateMachine bfsm;

typedef struct {
    int sockfd;
    char *frame;
    int cycletime_ms;
} thread_send_arg;

void timer_handler(int signum) {
    switch(bfsm.currentState){
        case BSTATE_CYCLE_SENT_BHM:
            printf("接收CRM(00)超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BRM:
            printf("接收CRM(AA)超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BCP:
            printf("接收CML超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BRO_AA:
            printf("接收CRO超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_RCV_CRO:
            printf("接收CRO(AA)超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BCL_BCS:
            printf("接收CCS超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BST:
            printf("接收CST超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        case BSTATE_CYCLE_SENT_BSD:
            printf("接收CSD超时!\n");
            bfsm.currentEvent=BEVENT_EXIT;
            break;
        default:
            break;  
    }
}

//周期发送帧数据函数，cycletime_ms为循环发送周期，单位为毫秒，传入0表示只发送一次int sockfd, char *frame,int cycletime_ms
static void *cycle_sent_frame(void *arg){
    thread_send_arg *parameter=(thread_send_arg *)arg;
    int ret, framelen=strlen(parameter->frame);
    unsigned int cycletime_us = parameter->cycletime_ms * 1000;
    if(parameter->cycletime_ms==0){
        ret = send(sockfd, parameter->frame, framelen, 0);
        if(ret != framelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",framelen, ret);
        }
    }else{
        while(1){
        ret = send(sockfd, parameter->frame, framelen, 0);
        if(ret != framelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",framelen, ret);
        }
        // 等待指定的周期时间（以微秒为单位）
        usleep(cycletime_us);
        }
    }
}

static void kill_thread(pthread_t thread){
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

//充电准备子线程
void* charge_prepare(void* arg){
    printf("正在进行充电准备工作\n");
    sleep(30);//模拟充电准备，睡30s
    printf("充电机准备就绪\n");
    bfsm.currentEvent=BEVENT_CHARGE_READY;
}

static void handle_charging_init(){
    printf("通信开始，车辆接口连接确认成功\n");
    bfsm.currentState = BSTATE_CONNECT_CONFIRM;
}

static void handle_sent_bhm(){
    printf("周期发送BHM报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BHM;
    thread_send_arg chm_frame1 = {sockfd,BFRAME_BHM,250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BHM报文
        perror("pthread_create in cycle send BHM");
        exit(EXIT_FAILURE);
    }
    //设置30S的定时器
    set_timer(&btimerid1,30);
}  

static void handle_sent_brm(){
    printf("停止发送BHM报文\n");
    kill_thread(bsendThread1);
    printf("周期发送BRM报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BRM;
    thread_send_arg chm_frame1={sockfd,BFRAME_BRM,250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BRM报文
        perror("pthread_create in cycle send BRM");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&btimerid1,5);
}

static void  handle_sent_bcp(){
    printf("停止发送BRM报文\n");
    kill_thread(bsendThread1);
    printf("周期发送BCP报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BCP;
    thread_send_arg chm_frame1={sockfd,BFRAME_BCP,250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CRM(00)
        perror("pthread_create in cycle send BCP");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&btimerid1,5);
}

static void hadle_parameter_check(){
    printf("时间同步处理完成\n");
    bfsm.currentState = BSTATE_PARAMETER_ADAPT;
    printf("充电机参数适配\n");
    bfsm.currentEvent = BEVENT_PARAMETER_ADAPT;
}

static void handle_sent_bro_00(){
    printf("停止发送BCP报文\n");
    kill_thread(bsendThread1);
    printf("周期发送BRO(00)报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BRO_00;
    thread_send_arg chm_frame1={sockfd,BFRAME_BRO_00,250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BRO_00报文
        perror("pthread_create in cycle send BRO_00");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&bsendThread2, NULL, charge_prepare, NULL) != 0) {//开始充电准备工作
        perror("pthread_create in cycle start charge check");
        exit(EXIT_FAILURE);
    }
}

static void handle_sent_bro_aa(){
    printf("停止发送BRO(AA)报文\n");
    kill_thread(bsendThread1);
    kill_thread(bsendThread2);//回收充电准备子线程
    printf("周期发送BRO(00)报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BRO_AA;
    thread_send_arg chm_frame1={sockfd,BFRAME_BRO_AA,250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BRO_AA报文
        perror("pthread_create in cycle send BRO_AA");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&btimerid1,5);
    //设置60S的定时器
    set_timer(&btimerid2,60);
}

static void handle_sent_bcl_bcs(){
    printf("停止发送BRO报文\n");
    kill_thread(bsendThread1);
    printf("周期发送BCL报文、BCS报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BCL_BCS;
    thread_send_arg chm_frame1={sockfd, BFRAME_BCL, 250};
    if (pthread_create(&bsendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BCL报文
        perror("pthread_create in cycle send BCL");
        exit(EXIT_FAILURE);
    }
    thread_send_arg chm_frame2={sockfd, BFRAME_BCS, 250};
    if (pthread_create(&bsendThread2, NULL, cycle_sent_frame, &chm_frame2) != 0) {//以250ms为周期发送BCS
        perror("pthread_create in cycle send BCS");
        exit(EXIT_FAILURE);
    }
}

static void handle_sent_bsm(){
    printf("周期发送BSM报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BSM;
    thread_send_arg chm_frame1={sockfd,BFRAME_BSM,250};
    if (pthread_create(&bsendThread3, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BSM报文
        perror("pthread_create in cycle send BSM");
        exit(EXIT_FAILURE);
    }
}

static void handle_sent_bst(){
    printf("周期发送BST报文\n");
    bfsm.currentState = BSTATE_CYCLE_SENT_BST;
    thread_send_arg chm_frame1={sockfd,BFRAME_BST,250};
    if (pthread_create(&bsendThread4, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BSM报文
        perror("pthread_create in cycle send BST");
        exit(EXIT_FAILURE);
    }
    if(got_cst!=1){//若是主动结束还要接收BST
        //设置5S的定时器
        set_timer(&btimerid1,5);
    }
    //设置10S的定时器
    set_timer(&btimerid2,10);
}

static void handle_sent_bsd(){
    printf("停止发送BST报文\n");
    kill_thread(bsendThread4);
    printf("周期发送BSD报文\n");
    thread_send_arg chm_frame1={sockfd,BFRAME_BSD,250};
    if (pthread_create(&bsendThread4, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送BSM报文
        perror("pthread_create in cycle send BSD");
        exit(EXIT_FAILURE);
    }
}

void switchState(Event event) {
    switch (event) {
        case BEVENT_START:
            if(bfsm.currentState == BSTATE_INIT){
                handle_charging_init();
            };
            break;
        case BEVENT_RECV_CHM:
            if(bfsm.currentState == BSTATE_CONNECT_CONFIRM){
                handle_sent_bhm();
            };
            break;
        case BEVENT_RECV_CRM_00:
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BHM){
                cancel_timer(&btimerid1);//30s内收到了CRM_00,所以关闭定时器
                handle_sent_brm();
            }
        case BEVENT_RECV_CRM_AA:    
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BRM){
                cancel_timer(&btimerid1);//5s内收到了CRM_AA,所以关闭定时器
                handle_sent_bcp();
            }
            break;
        case BEVENT_RECV_CML:
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BCP){
                cancel_timer(&btimerid1);//5s内收到了CML,所以关闭定时器
                hadle_parameter_check();
            };
            break;
        case BEVENT_PARAMETER_ADAPT:
            if(bfsm.currentState == BSTATE_PARAMETER_ADAPT){
                handle_sent_bro_00();
            };
            break;
        case BEVENT_CHARGE_READY:
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BRO_00){
                handle_sent_bro_aa();
            };
            break;
        case BEVENT_RECV_CRO:
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BRO_AA){
                cancel_timer(&btimerid1);//5s内收到了CRO,所以关闭定时器
                bfsm.currentState == BSTATE_RCV_CRO;
            }
            break;
        case BEVENT_RECV_CRO_AA:
            if(bfsm.currentState == BSTATE_RCV_CRO){
                cancel_timer(&btimerid2);//60s内收到了CRO_AA,所以关闭定时器
                //设置1S的定时器
                set_timer(&btimerid1,1);
                handle_sent_bcl_bcs();
            }
            break;
        case BEVENT_RECV_CCS:
            if(bfsm.currentState == BSTATE_CYCLE_SENT_BCL_BCS){
                cancel_timer(&btimerid1);//1s内收到了CCS,所以关闭定时器
                handle_sent_bsm();
            }else if(bfsm.currentState == BSTATE_CYCLE_SENT_BSM){
                bcan_over++;
                if(bcan_over==100){
                    handle_sent_bst();
                }
            }
            break;
        case BEVENT_RECV_CST:
            if(bfsm.currentState==BSTATE_CYCLE_SENT_BSM){
                got_cst=1;
                handle_sent_bst();
            }else if(bfsm.currentState==BSTATE_CYCLE_SENT_BST && got_cst==0){
                got_cst=1;
                cancel_timer(&btimerid1);
                handle_sent_bsd();
            }
            break;
        case BEVENT_RECV_CSD:
            if(bfsm.currentState==BSTATE_CYCLE_SENT_BSD){
                cancel_timer(&btimerid2);
                bfsm.currentEvent==BEVENT_EXIT;
            }
            break;

        case BEVENT_EXIT:
            kill_thread(bsendThread4);
            kill_thread(bsendThread3);
            kill_thread(bsendThread2);
            kill_thread(bsendThread1);
            kill_thread(beventThread);
            bfsm.currentState = BSTATE_EXIT;
            break;
        default:
            break;
}
}

//监听数据帧的子线程
void* eventListener(void* arg) {
    while (1) {
    int recvbytes=0,frame_type;
    memset(line_input,0,sizeof(line_input));
    recvbytes = recv(sockfd, line_input, sizeof(line_input) - 1, 0);
    if (recvbytes == -1) {
        perror("帧数据接收出错\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }else{
        frame_type=can_parse(line_input,pgn_json);
        fprintf(output_file, "%s\n", line_output);
        switch(frame_type){
            case CHM:
                if(bfsm.currentState == BSTATE_CONNECT_CONFIRM){
                    bfsm.currentEvent = BEVENT_RECV_CHM;
                }
                break;
            case CRM:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BHM){
                    if(1){//判断报文是否是00
                        bfsm.currentEvent= BEVENT_RECV_CRM_00;
                    }
                }else if(bfsm.currentState == BSTATE_CYCLE_SENT_BRM){
                    if(1){//判断报文是否是AA
                        bfsm.currentEvent= BEVENT_RECV_CRM_AA;
                    }
                }
                break;
            case CML:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BCP){
                    bfsm.currentEvent = BEVENT_RECV_CML;
                }
                break;
            case CRO:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BRO_AA){
                    bfsm.currentEvent = BEVENT_RECV_CRO;
                }else if(bfsm.currentState == BSTATE_RCV_CRO){
                    if(1){//判断是不是AA
                        bfsm.currentEvent = BEVENT_RECV_CRO_AA;
                    }
                }
                break;
            case CCS:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BCL_BCS||bfsm.currentState == BSTATE_CYCLE_SENT_BSM){
                    bfsm.currentEvent = BEVENT_RECV_CCS;
                }
                break;
            case CST:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BSM||bfsm.currentState == BSTATE_CYCLE_SENT_BST){
                    bfsm.currentEvent = BEVENT_RECV_CST;
                }
                break;
            case CSD:
                if(bfsm.currentState == BSTATE_CYCLE_SENT_BSD){
                    bfsm.currentEvent = BEVENT_RECV_CSD;
                }
                break;
            default:
                printf("接收到未知帧数据\n");
                break;            
        }
    }
    }
} 


int main(void){
    struct sockaddr_in server_addr = {0};
    struct sockaddr_in client_addr = {0};
    char ip_str[20] = {0};
    int sockfd, connfd;
    int addrlen = sizeof(client_addr);
    char recvbuf[512];
    int ret,type,over=0,charge_over=0,bover;
    time_t first_sent_BRO_AA_time;
 
    // 打开套接字，得到套接字描述符
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > sockfd) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
 
    // 将套接字与指定端口号进行绑定
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);
 
    ret = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (0 > ret) {
        perror("bind error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
 
    // 使服务器进入监听状态
    ret = listen(sockfd, 50);
    if (0 > ret) {
        perror("listen error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
 
    // 阻塞等待客户端连接 
    connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
    if (0 > connfd) {
        perror("accept error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("有客户端接入...\n");
    inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ip_str, sizeof(ip_str));
    printf("客户端主机的IP地址: %s\n", ip_str);
    printf("客户端进程的端口号: %d\n", client_addr.sin_port);

    // 初始化状态机
    bfsm.currentState = BSTATE_INIT;
    bfsm.currentEvent = BEVENT_START;
    //创建监听子线程
    pthread_create(&beventThread, NULL, eventListener, NULL);

    while (1) {
        // 切换状态
        switchState(bfsm.currentEvent);
        if(bfsm.currentState==BSTATE_EXIT){
            break;
        }
    }
    close(sockfd);
    return 0;
}

