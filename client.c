#include <signal.h>
#include <pthread.h>
#include "gbt27930-2015.h"
#include "time.h"

#define SERVER_PORT		8888          	//服务器的端口号
#define SERVER_IP   	"172.16.41.204"	//服务器的IP地址
#define NUM_CCHILDREN    11


#define         CFRAME_CHM             " 1826F456#010100"                        //充电机握手报文
#define         CFRAME_CRM_00          " 1801F456#0001010101313233"              //充电机辨识报文
#define         CFRAME_CRM_AA          " 1801F456#AA01010101313233"              //充电机辨识报文
#define         CFRAME_CTS             " 1807F456#FFFFFFFFFFFFFF"                //充电机发送时间同步信息报文
#define         CFRAME_CML             " 1808F456#1C257C01DC05A00F"              //充电机最大输出能力报文
#define         CFRAME_CRO_00          " 100AF456#00"                            //充电机输出准备就绪状态报文
#define         CFRAME_CRO_AA          " 100AF456#AA"                            //充电机输出准备就绪状态报文
#define         CFRAME_CCS             " 1812F456#240BA00F0000FD"                //充电机充电状态报文
#define         CFRAME_CST             " 1101AF456#0400F0F0"                     //充电机中止充电报文
#define         CFRAME_CSD             " 2181DF456#0000010001010101"             //充电机统计数据报文

int sockfd;//套接字通信接口
int got_bst=0,got_bsd=0,can_over=0,got_bcs_times=0;
pthread_t ceventThread,csendThread1,csendThread2;
timer_t ctimerid1, ctimerid2,ctimerid3;

// 定义状态
typedef enum {
    CSTATE_INIT,
    CSTATE_CYCLE_SENT_CHM,                         //车辆接口已确认，电子锁锁止，低压辅助供电回路闭合，周期发送CHM报文
    CSTATE_SELFCHECK,                              //充电机自检
    CSTATE_CYCLE_SENT_CRM_00,                      //周期发送CRM报文(SPN2560=0x00)
    CSTATE_CYCLE_SENT_CRM_AA,                      //变更CRM报文(SPN2560=0xAA)并周期发送
    CSTATE_PARAMETET_ADAPT,                        //参数适配中
    CSTATE_CYCLE_SENT_CTS_CML,                     //周期发送CTS(可选)和CML报文
    CSTATE_RCV_BRO,                                //首次收到BRO
    CSTATE_CYCLE_SENT_CRO_00,                      //周期发送CRO(00)报文
    CSTATE_CHARGE_READY,                           //充电机准备就绪
    CSTATE_CYCLE_SENT_CRO_AA,                      //周期发送CRO(AA)报文
    CSTATE_CYCLE_SENT_CCS,                         //周期发送CCS报文
    CSTATE_TOCLOSE,                                //准备结束充电
    CSTATE_CYCLE_SENT_CST,                         //满足充电条件或者收到BST，周期发送CST
    CSTATE_CYCLE_SENT_CSD,                         //周期发送CSD
    CSTATE_EXIT                                    //退出
} Charging_State;

// 定义事件
typedef enum {
    EVENT_START,
    EVENT_RCV_BHM,
    EVENT_SELF_CHECK_SUCCESS,
    EVENT_RCV_BRM,                               //已经收到BRM报文，自首次发送CRM报文起5S，没收到BRM触发超时
    EVENT_RCV_BCP,                               //已经收到BCP报文，停止发送CRM报文，自首次发送CRM报文起5S，没收到BRM触发超时
    EVENT_PARAMETET_ADAPT,                        //参数合适
    EVENT_RCV_BRO,                               //已经收到BRO报文，自首次发送CML报文起5S，没收到BRO触发超时
    EVENT_RCV_BRO_AA,                            //已经收到BRO_AA报文，自首次发送CML报文起60S，没收到BRO触发超时
    EVENT_CHARGE_READY,                          //充电机准备就绪
    EVENT_RCV_BCL,                               //已收到BCL报文，停止发送CRO报文，自首次发送CRO报文起1S，没收到BCL触发超时
    EVENT_RCV_BCS,                               //收到BCS报文
    EVENT_RCV_BSM, 
    EVENT_CAN_OVER,
    EVENT_RCV_BST,
    EVENT_RCV_BSD,
    EVENT_EXIT // 退出
} Charging_Event;

//状态机结构
typedef struct {
    Charging_State currentState;
    Charging_Event currentEvent;
} StateMachine;

StateMachine cfsm;

typedef struct {
    int sockfd;
    char *frame;
    int cycletime_ms;
} thread_send_arg;

void timer_handler(int signum) {
    switch(cfsm.currentState){
        case CSTATE_CYCLE_SENT_CHM:
            printf("接受BHM超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CRM_00:
            printf("接受BRM超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CRM_AA:
            printf("接受BCP超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CTS_CML:
            printf("接受BRO超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_RCV_BRO:
            printf("接受BRO_AA超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CRO_00:
            printf("充电准备过程中，接受BRO_AA超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CRO_AA:
            printf("接受BCL超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CCS:
            printf("接受BCL BCS或BSM超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
            break;
        case CSTATE_CYCLE_SENT_CST:
            printf("接受BST或BSD超时!\n");
            cfsm.currentEvent=EVENT_EXIT;
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

//充电自检子线程
void* charge_check(void* arg){
    printf("正在进行充电自检\n");
    sleep(30);//模拟充电自检，睡30s
    printf("充电机准备就绪\n");
    cfsm.currentEvent=EVENT_CHARGE_READY;
}

static void handle_charging_init(){
    printf("车辆接口已确认\n");
    printf("电子锁锁止\n");
    printf("低压辅助供电回路闭合\n");
    printf("周期发送CHM报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CHM;
    thread_send_arg chm_frame1={sockfd,CFRAME_CHM,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CHM
        perror("pthread_create in cycle send CHM");
        exit(EXIT_FAILURE);
    }
    //设置10S的定时器
    set_timer(&ctimerid1,10);
}

static void handle_self_check(){
    cfsm.currentState = CSTATE_SELFCHECK;
    printf("充电自检成功(若失败应发送CST报文，表明自检故障,此处默认成功)\n");
    cfsm.currentEvent=EVENT_SELF_CHECK_SUCCESS;
}

static void handle_sent_crm_00(){
    printf("停止发送CHM报文\n");
    kill_thread(csendThread1);
    printf("周期发送CRM(00)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRM_00;
    thread_send_arg chm_frame1={sockfd,CFRAME_CRM_00,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CRM(00)
        perror("pthread_create in cycle send CRM(00)");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
}

static void handle_sent_crm_aa(){
    printf("停止发送CRM(00)报文\n");
    kill_thread(csendThread1);
    printf("周期发送CRM(AA)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRM_AA;
    thread_send_arg chm_frame1={sockfd,CFRAME_CRM_AA,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CRM(AA)
        perror("pthread_create in cycle send CRM(AA)");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
}

static void hadle_parameter_check(){
    printf("停止发送CRM(AA)报文\n");
    kill_thread(csendThread1);
    cfsm.currentState = CSTATE_PARAMETET_ADAPT;
    printf("车辆参数合适(若不合适，则需发送CML及CST报文并退出充电,此处默认合适)\n");
    cfsm.currentEvent=EVENT_PARAMETET_ADAPT;
}

static void handle_sent_cts_cml(){
    printf("周期发送CTS和CML报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CTS_CML;
    thread_send_arg chm_frame1={sockfd,CFRAME_CTS,500};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以500ms为周期发送CTS
        perror("pthread_create in cycle send CTS");
        exit(EXIT_FAILURE);
    }
    thread_send_arg chm_frame2={sockfd,CFRAME_CML,250};
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &chm_frame2) != 0) {//以250ms为周期发送CML
        perror("pthread_create in cycle send CML");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
    //设置60S的定时器
    set_timer(&ctimerid2,60);
}

static void handle_sent_cro_00(){
    printf("停止发送CTS和CML报文\n");
    kill_thread(csendThread1);
    kill_thread(csendThread2);
    printf("周期发送CRO(00)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRO_00;
    thread_send_arg chm_frame1={sockfd,CFRAME_CRO_00,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CRO(00)
        perror("pthread_create in cycle send CRO(00)");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
}

static void hadle_start_charge_check(){
    if (pthread_create(&csendThread2, NULL, charge_check, NULL) != 0) {//开始充电准备工作
        perror("pthread_create in cycle start charge check");
        exit(EXIT_FAILURE);
    }
}

static void handle_sent_cro_aa(){
    printf("停止发送CRO(00)报文\n");
    kill_thread(csendThread1);
    kill_thread(csendThread2);//回收充电准备子线程
    printf("周期发送CRO(AA)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRO_AA;
    thread_send_arg chm_frame1={sockfd,CFRAME_CRO_AA,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CRO(AA)
        perror("pthread_create in cycle send CRO(AA)");
        exit(EXIT_FAILURE);
    }
    //设置1S的定时器
    set_timer(&ctimerid1,1);
}

static void handle_sent_ccs(){
    printf("停止发送CRO(AA)报文\n");
    kill_thread(csendThread1);
    printf("周期发送CCS报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CCS;
    thread_send_arg chm_frame1={sockfd,CFRAME_CCS,250};
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CCS
        perror("pthread_create in cycle send CCS");
        exit(EXIT_FAILURE);
    }
    //设置1S的定时器
    set_timer(&ctimerid1,1);
    //设置5S的定时器
    set_timer(&ctimerid2,5);
    //设置5S的定时器
    set_timer(&ctimerid3,5);
}

static void handle_sent_cst(){
    printf("周期发送CST报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CST;
    thread_send_arg chm_frame1={sockfd,CFRAME_CST,250};
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CST
        perror("pthread_create in cycle send CST");
        exit(EXIT_FAILURE);
    }
    if(got_bst!=1){//若是主动结束还要接收BST
        //设置5S的定时器
        set_timer(&ctimerid1,5);
    }
    //设置10S的定时器
    set_timer(&ctimerid2,10);

}

static void handle_sent_csd(){
    printf("停止发送CST报文\n");
    kill_thread(csendThread2);
    printf("周期发送CSD报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CSD;
    thread_send_arg chm_frame1={sockfd,CFRAME_CSD,250};
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &chm_frame1) != 0) {//以250ms为周期发送CCS
        perror("pthread_create in cycle send CSD");
        exit(EXIT_FAILURE);
    }
    sleep(10);
    printf("低压辅助供电回路断开\n");
    printf("电子锁解锁\n");
    cfsm.currentEvent=EVENT_EXIT;
}


//状态切换函数
void switchState(Charging_Event event) {
    switch (event) {
        case EVENT_START:
            if(cfsm.currentState==CSTATE_INIT){
                handle_charging_init();
            }
            break;
        case EVENT_RCV_BHM:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CHM){
                cancel_timer(&ctimerid1);//10s内收到了BHM,所以关闭定时器
                handle_self_check();
            }
            break;
        case EVENT_SELF_CHECK_SUCCESS:
            if(cfsm.currentState==CSTATE_SELFCHECK){
                handle_sent_crm_00();
            }
            break;
        case EVENT_RCV_BRM:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CRM_00){
                cancel_timer(&ctimerid1);//5s内收到了BRM,所以关闭定时器
                handle_sent_crm_aa();
            }
            break;
        case EVENT_RCV_BCP:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CRM_AA){
                cancel_timer(&ctimerid1);//5s内收到了BCP,所以关闭定时器
                hadle_parameter_check();
            }
            break;
        case EVENT_PARAMETET_ADAPT:
            if(cfsm.currentState==CSTATE_PARAMETET_ADAPT){
                handle_sent_cts_cml();
            }
            break;
        case EVENT_RCV_BRO:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CTS_CML){
                cancel_timer(&ctimerid1);//5s内收到了BRO,所以关闭定时器
                cfsm.currentState=CSTATE_RCV_BRO;
            }else if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_00){
                cfsm.currentEvent=EVENT_EXIT;//周期发送CRO_00时收到BRO但不是BRO_AA应发送CST，此处直接退出
            }
            break;
        case EVENT_RCV_BRO_AA:
            if(cfsm.currentState==CSTATE_RCV_BRO){
                cancel_timer(&ctimerid2);//60s内收到了BRO_AA,所以关闭定时器
                handle_sent_cro_00();
                hadle_start_charge_check();
            }else if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_00){
                cancel_timer(&ctimerid1);//5s内收到了BRO_AA,重置定时器
                set_timer(&ctimerid1,5);
            }
            break;
        case EVENT_CHARGE_READY:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_00){
                cancel_timer(&ctimerid1);//已准备就绪，取消定时器
                handle_sent_cro_aa();
            }
            break;
        case EVENT_RCV_BCL:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_AA){
                handle_sent_ccs();
            }else if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                cancel_timer(&ctimerid1);//1s内收到了BCL,重置定时器
                set_timer(&ctimerid1,1);
            }
            break;
        case EVENT_RCV_BCS:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                cancel_timer(&ctimerid2);//5s内收到了BCS,重置定时器
                set_timer(&ctimerid2,5);
                got_bcs_times++;
                if(got_bcs_times==100){//判断是否已经收到100次BCS,以此来模拟充电结束
                    can_over=1;
                }
            }
            break;
        case EVENT_RCV_BSM:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                cancel_timer(&ctimerid3);//5s内收到了BSM,重置定时器
                set_timer(&ctimerid3,5);
                if(1){//判断是否为00，若是则判断是否继续充电，此处默认继续
                        //继续充电
                    }else{//若不是则判断是否可以结束充电
                        if(got_bst==1||can_over==1){
                            cancel_timer(&ctimerid1);//可以结束则取消定时器
                            cancel_timer(&ctimerid2);
                            cancel_timer(&ctimerid3);
                            cfsm.currentEvent=EVENT_CAN_OVER;
                        }
                    }
            }
            break;
        case EVENT_CAN_OVER:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                handle_sent_cst();
            }
            break;
        case EVENT_RCV_BST:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                got_bst=1;
            }else if(cfsm.currentState==CSTATE_CYCLE_SENT_CST&&got_bst==0){
                got_bst=1;
                cancel_timer(&ctimerid1);
                if(got_bsd==1){
                    handle_sent_csd();
                }
            }
            break;
        case EVENT_RCV_BSD:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CST && got_bsd==0){
                got_bsd=1;
                cancel_timer(&ctimerid2);
                if(got_bst==1){
                    handle_sent_csd();
                }
            }
            break;
        case EVENT_EXIT:
            kill_thread(csendThread2);
            kill_thread(csendThread1);
            kill_thread(ceventThread);
            cfsm.currentState=CSTATE_EXIT;
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
            case BHM:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CHM){
                    cfsm.currentEvent=EVENT_RCV_BHM;
                }
                break;
            case BRM:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CRM_00){
                    cfsm.currentEvent=EVENT_RCV_BRM;
                }
                break;
            case BCP:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CRM_AA){
                    cfsm.currentEvent=EVENT_RCV_BCP;
                }
                break;
            case BRO:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CTS_CML){
                    cfsm.currentEvent=EVENT_RCV_BRO;
                }else if(cfsm.currentState==CSTATE_RCV_BRO||cfsm.currentState==CSTATE_CYCLE_SENT_CRO_00){
                    if(1){//此处需要判断是否是AA
                        cfsm.currentEvent=EVENT_RCV_BRO_AA;
                    }else{
                        cfsm.currentEvent=EVENT_RCV_BRO;
                    }
                }
                break;
            case BCL:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_AA||cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                    cfsm.currentEvent=EVENT_RCV_BCL;
                }
                break;
            case BCS:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                    cfsm.currentEvent=EVENT_RCV_BCS;
                }
                break;
            case BSM:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                    cfsm.currentEvent=EVENT_RCV_BSM;
                }
                break;
            case BST:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS||cfsm.currentState==CSTATE_CYCLE_SENT_CST){
                    cfsm.currentEvent=EVENT_RCV_BST;
                }
                break;
            case BSD:
                if(cfsm.currentState==CSTATE_CYCLE_SENT_CST){
                    cfsm.currentEvent=EVENT_RCV_BSD;
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
    int ret;
 
    //打开套接字，得到套接字描述符
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > sockfd) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
 
    //调用connect连接远端服务器
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);  //端口号
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);//IP地址
    ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret<0) {
        perror("connect error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("服务器连接成功...\n\n");
    
    // 初始化3个定时器
    timer_init(&ctimerid1, SIGRTMIN);
    timer_init(&ctimerid2, SIGRTMIN+1);
    timer_init(&ctimerid3, SIGRTMIN+2);

    // 初始化状态机
    cfsm.currentState = CSTATE_INIT;
    cfsm.currentEvent = EVENT_START;
    //创建监听子线程
    pthread_create(&ceventThread, NULL, eventListener, NULL);

    while (1) {
        // 切换状态
        switchState(cfsm.currentEvent);
        if(cfsm.currentState==CSTATE_EXIT){
            break;
        }
    }
    close(sockfd);
    return 0;
}