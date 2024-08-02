#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "gbt27930-2015.h"


#define SERVER_PORT		8888          	//服务器的端口号
#define SERVER_IP   	"172.16.41.204"	//服务器的IP地址
#define NUM_CHILDREN    11


#define         CFRAME_CHM             "1"           //充电机握手报文
#define         CFRAME_CRM_00          "3"           //充电机辨识报文
#define         CFRAME_CRM_AA          "3"           //充电机辨识报文
#define         CFRAME_CTS             "6"           //充电机发送时间同步信息报文
#define         CFRAME_CML             "7"           //充电机最大输出能力报文
#define         CFRAME_CRO_00          "9"           //充电机输出准备就绪状态报文
#define         CFRAME_CRO_AA          "9"           //充电机输出准备就绪状态报文
#define         CFRAME_CCS             "12"         //充电机充电状态报文
#define         CFRAME_CST             "18"         //充电机中止充电报文
#define         CFRAME_CSD             "20"         //充电机统计数据报文


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
pid_t children[NUM_CHILDREN]={0};
int pnum=0;

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
    sleep(30);//模拟充电准备，睡30s
    printf("充电机准备就绪\n");
}

void charging_disconnect(){
    printf("低压辅助供电回路断开\n");
    printf("电子锁解锁\n");
}

void terminate_children(pid_t pid_to_kill) {
    // 向指定的进程发送 SIGTERM 信号
    if (kill(pid_to_kill, SIGKILL) == -1) {
        printf("kill %d failure\n",pid_to_kill);
        exit(EXIT_FAILURE);
    }
    // 等待指定的进程终止
    int status;
    if (waitpid(pid_to_kill, NULL, 0) == -1) {
        printf("waitpid %d failure\n",pid_to_kill);
        exit(EXIT_FAILURE);
    }
}

void terminate_allchildren() {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGKILL); 
        }
    }
    // 等待所有子进程终止
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (children[i] > 0) {
            waitpid(children[i], NULL, 0);
        }
    }
}

//周期发送帧数据函数，cycletime_ms为循环发送周期，单位为毫秒，传入0表示只发送一次
void cycle_sent_frame(int sockfd, char *frame,int cycletime_ms){
    int ret, franmelen=strlen(frame);
    unsigned int cycletime_us = cycletime_ms * 1000;
    if(cycletime_ms==0){
        ret = send(sockfd, frame, franmelen, 0);
        if(ret != franmelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节： ",franmelen, ret);
        }
    }else{
        while(1){
        ret = send(sockfd, frame, franmelen, 0);
        if(ret != franmelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节： ",franmelen, ret);
        }
        // 等待指定的周期时间（以微秒为单位）
        usleep(cycletime_us);
        }
    }
}



//阻塞接收帧数据函数，frame为要接收的帧类型，starttime_s为阻塞接收的时间，单位为秒,match表示是否需要匹配00还是AA，传入NULL表示不匹配
int block_recv(int sockfd,char *recvbuf,int buflen,int frame,int starttime_s,char *match){
    int recvbytes=0,frame_type;
    if(frame==UNKOWN){
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, NULL, 0) < 0) {
            perror("套接字超时时间设置出错\n");
            exit(EXIT_FAILURE);
        }
        recvbytes = recv(sockfd, recvbuf, buflen - 1, 0);
        if (recvbytes == -1) {
            perror("帧数据接收出错\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        frame_type=can_parse(recvbuf,pgn_json);
        return frame_type;
    }else{
        time_t recv_time;
        struct timeval timeout;
        timeout.tv_sec = starttime_s;
        timeout.tv_usec = 0;
        while(1){
            recv_time = time(NULL);
            memset(recvbuf,0,buflen);
            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                printf("套接字超时时间设置出错\n");
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            recvbytes = recv(sockfd, recvbuf, buflen - 1, 0);
            if (recvbytes == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("帧数据接收超时\n");
                    return -1;
                } else {
                    perror("帧数据接收出错\n");
                    close(sockfd);
                    exit(EXIT_FAILURE);
                }
            }else{
                frame_type=can_parse(recvbuf,pgn_json);
                if(frame_type==frame){
                    if(match==NULL){
                        fprintf(output_file, "%s\n", line_output);
                        return 0;
                    }else{
                        switch(frame){
                            case BRO:
                                if(strncmp(caninfo.can_data,match,2)==0){
                                    fprintf(output_file, "%s\n", line_output);
                                    return 0;
                                }
                                break;
                            default:
                                break;
                        }
                    }                 
                }
                timeout.tv_sec-=(time(NULL)-recv_time);
            }
        }
    }
    
}


int main(void){
    struct sockaddr_in server_addr = {0};
    int sockfd;
    int ret,over=0;
    time_t first_sent_CML_time;
 
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
    if (ret<0) {
        perror("connect error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("服务器连接成功...\n\n");
 
    /* 向服务器发送数据 */
    while(1){
        pnum=0;
        memset(children,0,sizeof(children));
        switch(state){
            case START:
                charging_init();
                state=LOOP_CLOSURE;
                break;
            case LOOP_CLOSURE:
                state=CYCLE_SENT_CHM;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CHM, 250);//子进程1周期发送CHM
                }
                pnum++;
                break;
            case CYCLE_SENT_CHM:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BHM,10,NULL);//阻塞接收BHM，超时(大于10S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                state=RECV_BHM;
                break;
            case RECV_BHM:
                self_check();
                state=SELFCHECK_SUCCESS;
                break;
            case SELFCHECK_SUCCESS:
                state=CYCLE_SENT_CRM_00;
                terminate_children(children[pnum-1]);//杀死子进程1
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRM_00, 250);//子进程2周期发送CRM（00）
                }
                pnum++;
                break;
            case CYCLE_SENT_CRM_00:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRM,5,NULL);//阻塞接收BRM，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                state=RECV_BRM;
                break;
            case RECV_BRM:
                state=CYCLE_SENT_CRM_AA;
                terminate_children(children[pnum-1]);//杀死子进程2
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRM_AA, 250);//子进程3周期发送CRM(AA)
                }
                pnum++;
                break;
            case CYCLE_SENT_CRM_AA:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BCP,5,NULL);//阻塞接收BCP，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                terminate_children(children[pnum-1]);//杀死子进程3
                state=RECV_BCP_STOP_SENT_CRM;
                break;
            case RECV_BCP_STOP_SENT_CRM:
                parameter_check();
                state=PARAMETET_ADAPT;
                break;
            case PARAMETET_ADAPT:
                state=CYCLE_SENT_CTS_CML;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CTS, 500);//子进程4周期发送CTS
                }
                pnum++;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    first_sent_CML_time=time(NULL);
                    cycle_sent_frame(sockfd, CFRAME_CML, 250);//子进程5周期发送CML
                }
                pnum++;
                break;
            case CYCLE_SENT_CTS_CML:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,5,"00");//阻塞接收BRO_00，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                state=RECV_BRO_00;
                break;
            case RECV_BRO_00:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,(60-(time(NULL)-first_sent_CML_time)),"AA");//阻塞接收BRO_AA，超时(大于60S)应进入超时处理，此处直接转退出状态      (是否要改动？？？)
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                terminate_children(children[pnum-2]);//杀死子进程4
                terminate_children(children[pnum-1]);//杀死子进程5
                state=RECV_BRO_AA_STOP_SENT_CTS_SML;
                break;
            case RECV_BRO_AA_STOP_SENT_CTS_SML:
                state=CYCLE_SENT_CRO_00;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRO_00, 250);//子进程6周期发送CRO（00）
                }
                pnum++;
                break;
            case CYCLE_SENT_CRO_00:
                children[pnum] =fork();//创建子进程进行充电机准备
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    charge_check();//子进程7进行充电机准备
                    exit(0);//准备完之后就exit退出
                }
                pnum++;
                //循环接收BRO，以等待充电机准备就绪，超时(每次距离上次接收大于5S)应进入超时处理，此处直接转退出状态，若收到的报文不是AA，应发送CTS报文，此处转退出状态
                while(1){
                    ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,5,"AA");
                    if(ret<0){
                        state=EXIT_CHARGING;
                    }
                    if(waitpid(children[pnum-1],NULL,WNOHANG)==children[pnum-1]){
                        break;
                    }
                }
                state=CHARGE_READY;
                break;
            case CHARGE_READY:
                state=CYCLE_SENT_CRO_AA;
                terminate_children(children[pnum-2]);//杀死子进程6
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRO_AA, 250);//子进程8周期发送CRO（AA）
                }
                pnum++;
                break;
            case CYCLE_SENT_CRO_AA:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BCL,1,NULL);//阻塞接收BCL,超时(1S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    state=EXIT_CHARGING;
                }
                state=RECV_BCL_STOP_SENT_CRO;
                terminate_children(children[pnum-1]);//杀死子进程8
                break;
            case RECV_BCL_STOP_SENT_CRO:
                state=CYCLE_SENT_CCS;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CCS, 250);//子进程9周期发送CCS
                }
                pnum++;
                break;
            case CYCLE_SENT_CCS:
                //循环接收BCL BCS BSM（根据这个报文判断充电继续还是暂停充电，此处直接默认继续，若SPN3096不为00,需判断是否收到BST报文或满足结束充电要求，考虑全局变量实现？） ，并判断是否超时，进行相应超时处理，此处转退出状态
                time_t t=time(NULL);
                int type,BCL_time=t,BCS_time=t,BSM_time=t,get_BST=0;
                while(1){
                    //循环判断收到的帧是哪几种帧，在判断超时
                    type=block_recv(sockfd,line_input,sizeof(line_input),UNKOWN,0,NULL);
                    switch(type){

                    }
                }
                
                
                break;
            case TOCLOSE:
                state=CYCLE_SENT_CST;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CST, 250);//子进程10周期发送CST
                }
                pnum++;
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
                terminate_children(children[pnum-1]);//杀死进程10,结束发送CST
                state=CYCLE_SENT_CSD;
                children[pnum] =fork();
                if(children[pnum]<0){
                    printf("fork p%d error!\n",pnum);
                    exit(EXIT_FAILURE);
                }else if(children[pnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CSD, 250);//子进程11周期发送CSD
                }
                pnum++;
                break;
            case CYCLE_SENT_CSD:
                charging_disconnect();
                state=LOOP_CLOSURE;
                break;
            case LOOP_DISCONNECT:
                sleep(10);//睡1OS,然后退出
                state=EXIT_CHARGING;
                break;
            case EXIT_CHARGING:
                terminate_allchildren();
                over=1;
                break;
            default:
                printf("不合法的状态，程序退出\n");
                terminate_allchildren();
                exit(EXIT_FAILURE);

        }
        if(over==1){
            break;
        }
    }
    close(sockfd);
    return 0;
}