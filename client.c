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


typedef enum{
    CSTART,                                  //开始状态
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
    RECV_BSD_STOP_SENT_CST,                 //已经收到BSD,停止发送CST
    CYCLE_SENT_CSD,                         //周期发送CSD
    LOOP_DISCONNECT,                        //低压辅助
    EXIT_CHARGING                           //退出状态
    
}CHARGING_STATE;

CHARGING_STATE cstate=CSTART;
pid_t cchildren[NUM_CCHILDREN]={0};
int cpnum=0;

static void charging_init(){
    printf("车辆接口已确认\n");
    printf("电子锁锁止\n");
    printf("低压辅助供电回路闭合\n");
}

static void self_check(){
    printf("充电自检成功(若失败应发送CST报文，表明自检故障)\n");
}

static void parameter_check(){
    printf("车辆参数合适(若不合适，则需发送CML及CST报文并退出充电)\n");
}

static void charge_check(){
    sleep(30);//模拟充电准备，睡30s
    printf("充电机准备就绪\n");
}

static void charging_disconnect(){
    printf("低压辅助供电回路断开\n");
    printf("电子锁解锁\n");
}

static void in_charging(int *p){
    pid_t pid=fork();
    if(pid==0){
        sleep(30);//睡30S，模拟充电
        *p=1;
        exit(0);
    }else if(pid>0){
        return;
    }   
}

static void terminate_children(pid_t pid_to_kill) {
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

static void terminate_allchildren(int *child, int num) {
    for (int i = 0; i < num; i++) {
        if (child[i] > 0) {
            kill(child[i], SIGKILL); 
        }
    }
    // 等待所有子进程终止
    for (int i = 0; i < num; i++) {
        if (child[i] > 0) {
            waitpid(child[i], NULL, 0);
        }
    }
}

//周期发送帧数据函数，cycletime_ms为循环发送周期，单位为毫秒，传入0表示只发送一次
static void cycle_sent_frame(int sockfd, char *frame,int cycletime_ms){
    int ret, franmelen=strlen(frame);
    unsigned int cycletime_us = cycletime_ms * 1000;
    if(cycletime_ms==0){
        ret = send(sockfd, frame, franmelen, 0);
        if(ret != franmelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",franmelen, ret);
        }
    }else{
        while(1){
        ret = send(sockfd, frame, franmelen, 0);
        if(ret != franmelen){
            printf("数据发送错误， 需发送字节：%d,实际发送字节：%d ",franmelen, ret);
        }
        // 等待指定的周期时间（以微秒为单位）
        usleep(cycletime_us);
        }
    }
}

//阻塞接收帧数据函数，frame为要接收的帧类型，blocktime_s为阻塞接收的时间，单位为秒,match表示是否需要匹配00还是AA，传入NULL表示不匹配
static int block_recv(int sockfd,char *recvbuf,int buflen,int frame,int blocktime_s,char *match){
    int recvbytes=0,frame_type;
    struct timeval timeout;
    timeout.tv_usec = 0;
    if(frame==UNKOWN){
        timeout.tv_sec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("套接字超时时间设置出错1\n");
            exit(EXIT_FAILURE);
        }
        memset(recvbuf,0,buflen);
        recvbytes = recv(sockfd, recvbuf, buflen - 1, 0);
        if (recvbytes == -1) {
            perror("帧数据接收出错1\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        frame_type=can_parse(recvbuf,pgn_json);
        return frame_type;
    }else{
        time_t recv_time;
        timeout.tv_sec = blocktime_s;
        while(1){
            recv_time = time(NULL);
            memset(recvbuf,0,buflen);
            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                printf("套接字超时时间设置出错2\n");
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            recvbytes = recv(sockfd, recvbuf, buflen - 1, 0);
            if (recvbytes == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("帧数据接收超时\n");
                    return -1;
                } else {
                    perror("帧数据接收出错2\n");
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
    int ret,type,over=0,cover;
    time_t first_sent_CML_time,first_sent_CST_time;
 
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
        cpnum=0;
        memset(cchildren,0,sizeof(cchildren));
        switch(cstate){
            case CSTART:
                charging_init();
                cstate=LOOP_CLOSURE;
                break;
            case LOOP_CLOSURE:
                cstate=CYCLE_SENT_CHM;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CHM, 250);//子进程1周期发送CHM
                }
                cpnum++;
                break;
            case CYCLE_SENT_CHM:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BHM,10,NULL);//阻塞接收BHM，超时(大于10S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                cstate=RECV_BHM;
                break;
            case RECV_BHM:
                self_check();
                cstate=SELFCHECK_SUCCESS;
                break;
            case SELFCHECK_SUCCESS:
                cstate=CYCLE_SENT_CRM_00;
                terminate_children(cchildren[cpnum-1]);//杀死子进程1
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRM_00, 250);//子进程2周期发送CRM（00）
                }
                cpnum++;
                break;
            case CYCLE_SENT_CRM_00:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRM,5,NULL);//阻塞接收BRM，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                cstate=RECV_BRM;
                break;
            case RECV_BRM:
                cstate=CYCLE_SENT_CRM_AA;
                terminate_children(cchildren[cpnum-1]);//杀死子进程2
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRM_AA, 250);//子进程3周期发送CRM(AA)
                }
                cpnum++;
                break;
            case CYCLE_SENT_CRM_AA:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BCP,5,NULL);//阻塞接收BCP，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                terminate_children(cchildren[cpnum-1]);//杀死子进程3
                cstate=RECV_BCP_STOP_SENT_CRM;
                break;
            case RECV_BCP_STOP_SENT_CRM:
                parameter_check();
                cstate=PARAMETET_ADAPT;
                break;
            case PARAMETET_ADAPT:
                cstate=CYCLE_SENT_CTS_CML;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CTS, 500);//子进程4周期发送CTS
                }
                cpnum++;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    first_sent_CML_time=time(NULL);
                    cycle_sent_frame(sockfd, CFRAME_CML, 250);//子进程5周期发送CML
                }
                cpnum++;
                break;
            case CYCLE_SENT_CTS_CML:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,5,"00");//阻塞接收BRO_00，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                cstate=RECV_BRO_00;
                break;
            case RECV_BRO_00:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,(60-(time(NULL)-first_sent_CML_time)),"AA");//阻塞接收BRO_AA，超时(大于60S)应进入超时处理，此处直接转退出状态      (是否要改动？？？)
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                terminate_children(cchildren[cpnum-2]);//杀死子进程4
                terminate_children(cchildren[cpnum-1]);//杀死子进程5
                cstate=RECV_BRO_AA_STOP_SENT_CTS_SML;
                break;
            case RECV_BRO_AA_STOP_SENT_CTS_SML:
                cstate=CYCLE_SENT_CRO_00;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRO_00, 250);//子进程6周期发送CRO（00）
                }
                cpnum++;
                break;
            case CYCLE_SENT_CRO_00:
                cchildren[cpnum] =fork();//创建子进程进行充电机准备
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    charge_check();//子进程7进行充电机准备
                    exit(0);//准备完之后就exit退出
                }
                cpnum++;
                //循环接收BRO，以等待充电机准备就绪，超时(每次距离上次接收大于5S)应进入超时处理，此处直接转退出状态，若收到的报文不是AA，应发送CTS报文，此处转退出状态
                while(1){
                    ret=block_recv(sockfd,line_input,sizeof(line_input),BRO,5,"AA");
                    if(ret<0){
                        cstate=EXIT_CHARGING;
                    }
                    if(waitpid(cchildren[cpnum-1],NULL,WNOHANG)==cchildren[cpnum-1]){
                        break;
                    }
                }
                cstate=CHARGE_READY;
                break;
            case CHARGE_READY:
                cstate=CYCLE_SENT_CRO_AA;
                terminate_children(cchildren[cpnum-2]);//杀死子进程6
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CRO_AA, 250);//子进程8周期发送CRO（AA）
                }
                cpnum++;
                break;
            case CYCLE_SENT_CRO_AA:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BCL,1,NULL);//阻塞接收BCL,超时(1S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }
                cstate=RECV_BCL_STOP_SENT_CRO;
                terminate_children(cchildren[cpnum-1]);//杀死子进程8
                break;
            case RECV_BCL_STOP_SENT_CRO:
                cstate=CYCLE_SENT_CCS;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CCS, 250);//子进程9周期发送CCS
                }
                cpnum++;
                break;
            case CYCLE_SENT_CCS:
                //循环接收BCL BCS BSM（根据这个报文判断充电继续还是暂停充电，此处直接默认继续，若SPN3096不为00,需判断是否收到BST报文或满足结束充电要求，考虑全局变量实现？） ，并判断是否超时，进行相应超时处理，此处转退出状态
                cover=0;
                time_t t1=time(NULL),t2;
                int BCL_time=t1,BCS_time=t1,BSM_time=t1,get_BST=0,charge_over=0;
                in_charging(&charge_over);
                while(1){
                    type=block_recv(sockfd,line_input,sizeof(line_input),UNKOWN,0,NULL);
                    t2=time(NULL);
                    switch(type){
                        case BCL:
                            if(t2-BCL_time>1){
                                cstate=EXIT_CHARGING;
                            }else{
                                BCL_time=t2;
                                fprintf(output_file, "%s\n", line_output);
                            }
                            break;
                        case BCS:
                            if(t2-BCS_time>5){
                                cstate=EXIT_CHARGING;
                            }else{
                                BCS_time=t2;
                                fprintf(output_file, "%s\n", line_output);
                            }
                            break;
                        case BSM:
                            if(t2-BSM_time>5){
                                cstate=EXIT_CHARGING;
                            }else{
                                fprintf(output_file, "%s\n", line_output);
                                char byte3096 = caninfo.can_data[6];
                                if((!((byte3096>>12)&1))&&(!((byte3096>>11)&1))){
                                    cstate=CYCLE_SENT_CCS;//若SPN3096=0x00则根据实际情况选择充电暂停还是继续充电，此处选择继续充电
                                    BSM_time=t2;
                                }else{
                                //判断是否收到BST报文或者是否满足充电结束条件，此处默认满足结束条件
                                    if(charge_over==1){
                                        cover=1;
                                        cstate=TOCLOSE;
                                    }else if(get_BST==1){

                                    }else{
                                        BSM_time=t2;
                                    }
                                }
                            }
                            break;
                        case BST:
                            fprintf(output_file, "%s\n", line_output);
                            get_BST=1;//标记收到BST帧
                            break;
                        default:
                            break;
                    }
                    if(cstate==EXIT_CHARGING || cstate==TOCLOSE){
                        break;
                    }
                }           
                break;
            case TOCLOSE:
                cstate=CYCLE_SENT_CST;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    first_sent_CST_time=time(NULL);
                    cycle_sent_frame(sockfd, CFRAME_CST, 250);//子进程10周期发送CST
                }
                cpnum++;
                break;
            case CYCLE_SENT_CST:
                if(cover==1){//判断是不是充电机主动中止充电
                    if(get_BST!=1){
                        //等待接收到BST或者不是充电机主动发起结束充电(这也说明已经收到BST)
                        ret=block_recv(sockfd,line_input,sizeof(line_input),BST,5,NULL);//阻塞等待接收BST,超时(5S)应进入超时处理，此处直接转退出状态
                        if(ret<0){
                            cstate=EXIT_CHARGING;
                        }
                    }
                }
                cstate=RECV_BST;
                break;
            case RECV_BST:
                ret=block_recv(sockfd,line_input,sizeof(line_input),BSD,(time(NULL)-first_sent_CST_time),NULL);//阻塞等待接收BSD,超时(5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    cstate=EXIT_CHARGING;
                }else{
                    cstate=RECV_BSD_STOP_SENT_CST;
                }
                break;
            case RECV_BSD_STOP_SENT_CST:
                terminate_children(cchildren[cpnum-1]);//杀死进程10,结束发送CST
                cstate=CYCLE_SENT_CSD;
                cchildren[cpnum] =fork();
                if(cchildren[cpnum]<0){
                    printf("fork p%d error!\n",cpnum);
                    exit(EXIT_FAILURE);
                }else if(cchildren[cpnum]==0){
                    cycle_sent_frame(sockfd, CFRAME_CSD, 250);//子进程11周期发送CSD
                }
                cpnum++;
                break;
            case CYCLE_SENT_CSD:
                charging_disconnect();
                cstate=LOOP_DISCONNECT;
                break;
            case LOOP_DISCONNECT:
                sleep(10);//睡1OS,然后退出
                cstate=EXIT_CHARGING;
                break;
            case EXIT_CHARGING:
                terminate_allchildren(cchildren,NUM_CCHILDREN);
                over=1;
                break;
            default:
                printf("不合法的状态，程序退出\n");
                terminate_allchildren(cchildren,NUM_CCHILDREN);
                exit(EXIT_FAILURE);

        }
        if(over==1){
            break;
        }
    }
    close(sockfd);
    return 0;
}