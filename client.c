#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "gbt27930-2015.h"

extern char line_output[1024];
extern char line_input[1024];
extern FILE *output_file;
extern cJSON *pgn_json ;
 
#define SERVER_PORT		8888          	//服务器的端口号
#define SERVER_IP   	"172.16.41.204"	//服务器的IP地址

void sent_frame(char *frame,int cycletime){
    if(cycletime==0){

    }else{

    }
}


typedef enum{
    START,                                  //开始状态
    LOOP_CLOSURE,                           //车辆接口确认，电子锁锁止，低压辅助供电回路闭合
    CYCLE_SENT_CHM,                         //周期发送CHM报文
    RECV_BHM,                               //已经收到BHM报文
    SELFCHECK_SUCCESS,                      //充电机自检成功
    CYCLE_SENT_CRM_00,                      //周期发送CRM报文(SPN2560=0x00)
    RECV_BRM,                               //已经收到BRM报文，自首次发送CRM报文起5S，没收到BRM触发超时
    CYCLE_SENT_CRM_AA,                      //变更CRM报文(SPN2560=0xAA)并周期发送
    RECV_BCP_STOP_SENT_CRM,                 //已经收到BCP报文，停止发送CRM报文，自首次发送CRM报文起5S，没收到BRM触发超时
    PARAMETET_ADAPT,                        //参数合适
    CYCLE_SENT_CTS_CML,                     //周期发送CTS(可选)和CML报文
    RECV_BRO_00,                            //已经收到BRO_00报文，自首次发送CML报文起5S，没收到BRO触发超时
    RECV_BRO_AA_STOP_SENT_CTS_SML,          //已经收到BRO_AA报文，自首次发送CML报文起60S，没收到BRO触发超时
    CYCLE_SENT_CRO_00,                      //周期发送CRO(00)报文
    CHARGE_READY,                           //充电机准备就绪
    CYCLE_SENT_CRO_AA,                      //周期发送CRO(AA)报文
    RECV_BCL_STOP_SENT_CRO,                 //已收到BCL报文，停止发送CRO报文，自首次发送CRO报文起1S，没收到BCL触发超时   
    CYCLE_SENT_CCS,                         //周期发送CCS报文
    TOCLOSE,                                //准备结束充电
    CYCLE_SENT_CST,                         //满足充电条件或者收到BST，周期发送CST
    RECV_BST,                               //已经收到BST
    RECV_BSD,                               //已经收到BSD
    CYCLE_SENT_CSD,                         //周期发送CSD
    LOOP_DISCONNECT,                        //低压辅助
    EXIT_CHARGING                           //退出状态
    
}CHARGING_STATE;

CHARGING_STATE state=START;

void charging_init(){
    printf("车辆接口已确认\n");
    printf("电子锁锁止\n");
    printf("低压辅助供电回路闭合\n");
}

void self_check(){
    printf("充电自检成功(若失败应发送CST报文，表明自检故障)\n");
}

void parameter_check(){
    printf("车辆参数合适(若不合适，则需发送CML及CST报文并退出充电)\n");
}

void charge_check(){
    printf("充电机准备就绪\n");
}

void charging_disconnect(){
    printf("低压辅助供电回路断开\n");
    printf("电子锁解锁\n");
}


int main(void){
    pid_t pid;
    while(1){
        switch(state){
            case START:
                charging_init();
                state=LOOP_CLOSURE;
                break;
            case LOOP_CLOSURE:
                state=CYCLE_SENT_CHM;
                pid =fork();
                if(pid<0){
                    printf("fork1 error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程1周期发送CHM
                }
                break;
            case CYCLE_SENT_CHM:
                //阻塞接收，超时(大于10S)应进入超时处理，此处直接转退出状态
                state=RECV_BHM;
                break;
            case RECV_BHM:
                self_check();
                state=SELFCHECK_SUCCESS;
                break;
            case SELFCHECK_SUCCESS:
                state=CYCLE_SENT_CRM_00;
                //杀死子进程1
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程2周期发送CRM（00）
                }
                break;
            case CYCLE_SENT_CRM_00:
                //阻塞接收，超时(大于5S)应进入超时处理，此处直接转退出状态
                state=RECV_BRM;
                break;
            case RECV_BRM:
                state=CYCLE_SENT_CRM_AA;
                //杀死子进程2
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程3周期发送CRM(AA)
                }
                break;
            case CYCLE_SENT_CRM_AA:
                //阻塞接收，超时(大于5S)应进入超时处理，此处直接转退出状态
                //杀死子进程3
                state=RECV_BCP_STOP_SENT_CRM;
                break;
            case RECV_BCP_STOP_SENT_CRM:
                parameter_check();
                state=PARAMETET_ADAPT;
                break;
            case PARAMETET_ADAPT:
                state=CYCLE_SENT_CTS_CML;
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程4周期发送CTS CML
                }
                break;
            case CYCLE_SENT_CTS_CML:
                //阻塞接收，超时(大于5S)应进入超时处理，此处直接转退出状态
                state=RECV_BRO_00;
                break;
            case RECV_BRO_00:
                //阻塞接收，超时(大于55S)应进入超时处理，此处直接转退出状态      (是否要改动？？？)
                //杀死子进程4
                state=RECV_BRO_AA_STOP_SENT_CTS_SML;
                break;
            case RECV_BRO_AA_STOP_SENT_CTS_SML:
                state=CYCLE_SENT_CRO_00;
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程5周期发送CRO（00）
                }
                break;
            case CYCLE_SENT_CRO_00:
                //创建子进程进行充电机准备
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    charge_check();
                    //子进程6进行充电机准备
                    //准备完之后就exit退出
                    //可以考虑用全局变量通知父进程？
                }
                //循环接收BRO，以等待充电机准备就绪，超时(每次距离上次接收大于5S)应进入超时处理，此处直接转退出状态，若收到的报文不是AA，应发送CTS报文，此处转退出状态
                state=CHARGE_READY;
                break;
            case CHARGE_READY:
                state=CYCLE_SENT_CRO_AA;
                //杀死子进程5
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程7周期发送CRO（AA）
                }
                break;
            case CYCLE_SENT_CRO_AA:
                //阻塞接收BCL,超时(1S)应进入超时处理，此处直接转退出状态
                state=RECV_BCL_STOP_SENT_CRO;
                //杀死进程7
                break;
            case RECV_BCL_STOP_SENT_CRO:
                state=CYCLE_SENT_CCS;
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程8周期发送CCS
                }
                break;
            case CYCLE_SENT_CCS:
                //循环接收BCL BCS BSM（根据这个报文判断充电继续还是暂停充电，此处直接默认继续，若SPN3096不为00,需判断是否收到BST报文或满足结束充电要求，考虑全局变量实现？） ，并判断是否超时，进行相应超时处理，此处转退出状态
                break;
            case TOCLOSE:
                state=CYCLE_SENT_CST;
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程9周期发送CST
                }
                break;
            case CYCLE_SENT_CST:
                //判断是不是充电机主动中止充电
                //若是则要阻塞等待接收BST,超时(5S)应进入超时处理，此处直接转退出状态
                //等待接收到BST或者不是充电机主动发起结束充电(这也说明已经收到BST)，检测BSD
                state=RECV_BST;
                break;
            case RECV_BST:
                //阻塞等待接收BSD,超时(5S)应进入超时处理，此处直接转退出状态
                break;
            case RECV_BSD:
                //杀死进程9,结束发送CST
                state=CYCLE_SENT_CSD;
                pid =fork();
                if(pid<0){
                    printf("fork error!\n");
                    exit(EXIT_FAILURE);
                }else if(pid==0){
                    //子进程10周期发送CSD
                }
                break;
            case CYCLE_SENT_CSD:
                charging_disconnect();
                state=LOOP_CLOSURE;
                break;
            case LOOP_DISCONNECT:
                //睡1OS,然后杀死进程？
                break;
            case EXIT_CHARGING:
                //杀死所有子进程
                break;
            default:
                printf("不合法的状态，程序退出\n");
                exit(EXIT_FAILURE);

        }
    }



    struct sockaddr_in server_addr = {0};
    char buf[512];
    int sockfd;
    int ret;
 
    /* 打开套接字，得到套接字描述符 */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > sockfd) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
 
    /* 调用connect连接远端服务器 */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);  //端口号
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);//IP地址
    ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (0 > ret) {
        perror("connect error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("服务器连接成功...\n\n");
 
    /* 向服务器发送数据 */
    for ( ; ; ) {
        // 清理缓冲区
        memset(buf, 0x0, sizeof(buf));
 
        // 接收用户输入的字符串数据
        printf("Please enter a string: ");
        fgets(buf, sizeof(buf), stdin);
 
        // 将用户输入的数据发送给服务器
        ret = send(sockfd, buf, strlen(buf), 0);
        if(0 > ret){
            perror("send error");
            break;
        }
 
        //输入了"exit"，退出循环
        if(0 == strncmp(buf, "exit", 4))
            break;
    }
    close(sockfd);
    exit(EXIT_SUCCESS);
}