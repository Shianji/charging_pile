#include <signal.h>
#include "common.h"

#define SERVER_PORT		8888          	//服务器的端口号
#define SERVER_IP   	"172.16.41.204 "	//服务器的IP地址
#define NUM_CCHILDREN    11


const uint8_t CFRAME_CHM[12]={0X18, 0X26, 0XF4, 0X56, 0X01, 0X01, 0X00, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF};              //充电机握手报文
const uint8_t CFRAME_CRM_00[12]={0X18, 0X01, 0XF4, 0X56, 0X00, 0X01, 0X01, 0X01, 0X01, 0X31, 0X32, 0X33};           //充电机辨识报文


const uint8_t CFRAME_CRM_AA[12]={0X18, 0X01, 0XF4, 0X56, 0XAA, 0X01, 0X01, 0X01, 0X01, 0X31, 0X32, 0X33};           //充电机辨识报文
const uint8_t CFRAME_CTS[12]={0X18, 0X07, 0XF4, 0X56, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF};              //充电机发送时间同步信息报文
const uint8_t CFRAME_CML[12]={0X18, 0X08, 0XF4, 0X56, 0X1C, 0X25, 0X7C, 0X01, 0XDC, 0X05, 0XA0, 0X0F};              //充电机最大输出能力报文
const uint8_t CFRAME_CRO_00[12]={0X10, 0X0A, 0XF4, 0X56, 0X00, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF};           //充电机输出准备就绪状态报文
const uint8_t CFRAME_CRO_AA[12]={0X10, 0X0A, 0XF4, 0X56, 0XAA, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF};           //充电机输出准备就绪状态报文
const uint8_t CFRAME_CCS[12]={0X18, 0X12, 0XF4, 0X56, 0X24, 0X0B, 0XA0, 0X0F, 0X00, 0X00, 0XFD, 0XFF};              //充电机充电状态报文
const uint8_t CFRAME_CST[12]={0X10, 0X1A, 0XF4, 0X56, 0X04, 0X00, 0XF0, 0XF0, 0XFF, 0XFF, 0XFF, 0XFF};              //充电机中止充电报文
const uint8_t CFRAME_CSD[12]={0X18, 0X1D, 0XF4, 0X56, 0X00, 0X00, 0X01, 0X00, 0X01, 0X01, 0X01, 0X01};              //充电机统计数据报文

int sockfd;//套接字通信接口
int got_bst=0,got_bsd=0,can_over=0,got_bcs_times=0;
pthread_t ceventThread,csendThread1,csendThread2;
timer_t ctimerid1, ctimerid2,ctimerid3;
thread_send_arg cframe1,cframe2,cframe3,cframe4;

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
    EVENT_PARAMETET_ADAPT,                       //参数合适
    EVENT_RCV_BRO,                               //已经收到BRO报文，自首次发送CML报文起5S，没收到BRO触发超时
    EVENT_RCV_BRO_AA,                            //已经收到BRO_AA报文，自首次发送CML报文起60S，没收到BRO触发超时
    EVENT_CHARGE_READY,                          //充电机准备就绪
    EVENT_RCV_BCL,                               //已收到BCL报文，停止发送CRO报文，自首次发送CRO报文起1S，没收到BCL触发超时
    EVENT_RCV_BCS,                               //收到BCS报文
    EVENT_RCV_BSM_00,                            //收到BSM(00)报文
    EVENT_RCV_BSM_N00,                           //收到BSM报文,但不是BSM(00)
    EVENT_CAN_OVER,                              //充电可以结束
    EVENT_RCV_BST,                               //收到BST报文
    EVENT_RCV_BSD,                               //收到BSD报文
    EVENT_EXIT                                   //退出
} Charging_Event;

//充电机状态机结构
typedef struct {
    Charging_State currentState;
    Charging_Event currentEvent;
} CStateMachine;

CStateMachine cfsm;

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

//充电自检子线程
static void* charge_check(void* arg){
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
    cframe1.frame=CFRAME_CHM;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CHM
        perror("pthread_create in cycle send CHM");
        exit(EXIT_FAILURE);
    }
    //设置10S的定时器
    set_timer(&ctimerid1,10);
}

static void handle_self_check(){
    printf("收到BHM报文,开始充电自检\n");
    cfsm.currentState = CSTATE_SELFCHECK;
    printf("充电自检成功(若失败应发送CST报文，表明自检故障,此处默认成功)\n");
    cfsm.currentEvent=EVENT_SELF_CHECK_SUCCESS;
}

static void handle_sent_crm_00(){
    printf("停止发送CHM报文\n");
    kill_thread(csendThread1);
    printf("周期发送CRM(00)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRM_00;
    cframe1.frame=CFRAME_CRM_00;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CRM(00)
        perror("pthread_create in cycle send CRM(00)");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
}

static void handle_sent_crm_aa(){
    printf("收到BRM报文\n");
    printf("停止发送CRM(00)报文\n");
    kill_thread(csendThread1);
    printf("周期发送CRM(AA)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRM_AA;
    cframe1.frame=CFRAME_CRM_AA;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CRM(AA)
        perror("pthread_create in cycle send CRM(AA)");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
}

static void hadle_parameter_check(){
    printf("收到BCP报文\n");
    printf("停止发送CRM(AA)报文\n");
    kill_thread(csendThread1);
    cfsm.currentState = CSTATE_PARAMETET_ADAPT;
    printf("车辆参数合适(若不合适，则需发送CML及CST报文并退出充电,此处默认合适)\n");
    cfsm.currentEvent=EVENT_PARAMETET_ADAPT;
}

static void handle_sent_cts_cml(){
    printf("周期发送CTS和CML报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CTS_CML;
    cframe1.frame=CFRAME_CTS;
    cframe1.cycletime_ms=500;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以500ms为周期发送CTS
        perror("pthread_create in cycle send CTS");
        exit(EXIT_FAILURE);
    }
    cframe2.frame=CFRAME_CML;
    cframe2.cycletime_ms=250;
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &cframe2) != 0) {//以250ms为周期发送CML
        perror("pthread_create in cycle send CML");
        exit(EXIT_FAILURE);
    }
    //设置5S的定时器
    set_timer(&ctimerid1,5);
    //设置60S的定时器
    set_timer(&ctimerid2,60);
}

static void handle_sent_cro_00(){
    printf("收到BRO(AA)报文\n");
    printf("停止发送CTS和CML报文\n");
    kill_thread(csendThread1);
    kill_thread(csendThread2);
    printf("周期发送CRO(00)报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CRO_00;
    cframe1.frame=CFRAME_CRO_00;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CRO(00)
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
    cframe1.frame=CFRAME_CRO_AA;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CRO(AA)
        perror("pthread_create in cycle send CRO(AA)");
        exit(EXIT_FAILURE);
    }
    //设置1S的定时器
    set_timer(&ctimerid1,1);
}

static void handle_sent_ccs(){
    printf("收到BCL报文\n");
    printf("停止发送CRO(AA)报文\n");
    kill_thread(csendThread1);
    printf("周期发送CCS报文\n");
    cfsm.currentState = CSTATE_CYCLE_SENT_CCS;
    cframe1.frame=CFRAME_CCS;
    cframe1.cycletime_ms=250;
    if (pthread_create(&csendThread1, NULL, cycle_sent_frame, &cframe1) != 0) {//以250ms为周期发送CCS
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
    cframe2.frame=CFRAME_CST;
    cframe2.cycletime_ms=250;
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &cframe2) != 0) {//以250ms为周期发送CST
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
    cframe2.frame=CFRAME_CSD;
    cframe2.cycletime_ms=250;
    if (pthread_create(&csendThread2, NULL, cycle_sent_frame, &cframe2) != 0) {//以250ms为周期发送CCS
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
                printf("BRM1\n");
                cancel_timer(&ctimerid1);//5s内收到了BRM,所以关闭定时器
                handle_sent_crm_aa();
            }
            break;
        case EVENT_RCV_BCP:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CRM_AA){
                printf("BCP1\n");
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
                printf("第一次收到BRO报文\n");
                cancel_timer(&ctimerid1);//5s内收到了BRO,所以关闭定时器
                cfsm.currentState=CSTATE_RCV_BRO;
            }else if(cfsm.currentState==CSTATE_CYCLE_SENT_CRO_00){
                printf("充电机准备期间收到BRO(00)报文\n");
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
                cancel_timer(&ctimerid1);//1s内收到了BCL
                handle_sent_ccs();
                set_timer(&ctimerid1,1);//重置定时器
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
        case EVENT_RCV_BSM_00:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                cancel_timer(&ctimerid3);//5s内收到了BSM,重置定时器
                set_timer(&ctimerid3,5);
                //收到BSM(00)，判断是否继续充电，此处默认继续充电
            }
            break;
        case EVENT_RCV_BSM_N00:
            if(cfsm.currentState==CSTATE_CYCLE_SENT_CCS){
                cancel_timer(&ctimerid3);//5s内收到了BSM,重置定时器
                set_timer(&ctimerid3,5);    
                if(got_bst==1||can_over==1){//判断是否可以结束充电
                    cancel_timer(&ctimerid1);//可以结束则取消定时器
                    cancel_timer(&ctimerid2);
                    cancel_timer(&ctimerid3);
                    cfsm.currentEvent=EVENT_CAN_OVER;
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
                handle_sent_cst();
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
    int recvbytes=0,frame_type;
    while (1) {
    memset(line_input,0,sizeof(line_input));
    recvbytes = recv(sockfd, line_input, sizeof(line_input) - 1, 0);
    if (recvbytes == -1) {
        perror("帧数据接收出错\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }else if(recvbytes==0){
        printf("BMS端断开连接，通信结束\n");
        exit(0);
    }else{
        //调试用
        // printf("接受到%d字节\n",recvbytes);
        // for(int i=0;i<recvbytes;i++){
        //     printf("%02x",line_input[i]);
        // }
        // printf("\n");

        frame_type=can_parse(line_input,pgn_json,CAN_DATA_LEN);
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
                    if(strncmp(caninfo.can_data,"AA",2)==0){//此处需要判断是否是AA
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
                    char temparry[2]={0};
                    strncpy(temparry,caninfo.can_data+7,1);
                    int temp= hex_string_to_int(temparry);
                    if(((temp>>1)&0x01==0)&&((temp>>2)&0x01==0)){//此处需要判断是否是AA     需要进一步判断是哪几位，文档并没有明确
                        cfsm.currentEvent=EVENT_RCV_BSM_00;
                    }else{
                        cfsm.currentEvent=EVENT_RCV_BSM_N00;
                    }
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
            case MULPRE:
                break;      
            default:
                printf("接收到预期之外的帧数据,帧代号为：%d\n",frame_type);
                break;            
        }
    }
    }
} 



int main(void){
    struct sockaddr_in server_addr = {0};
    int ret;
    
    //初始化
    init("charging");
    
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
    
    cframe1.sockfd=sockfd;
    cframe2.sockfd=sockfd;
    cframe3.sockfd=sockfd;
    cframe4.sockfd=sockfd;

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
    deinit();
    return 0;
}