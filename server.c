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


typedef enum{
    BSTART,                                   // 开始状态
    CONNECT_CONFIRM,                          // 通信开始，车辆接口确认
    RECV_CHM,                                 // 已经收到CHM报文
    CYCLE_SENT_BHM,                           // 周期发送BHM报文
    RECV_CRM_00_STOP_SENT_BHM,                // 收到BRM报文(00)后停止发送CHM报文,自首次发送BHM报文起30S，没收到CRM(00)触发超时
    CYCLE_SENT_BRM,                           // 周期发送BRM报文
    RECV_CRM_AA_STOP_SENT_BRM,                // 收到CRM报文后停止发送BRM报文,自首次发送BHM报文起5S，没收到CRM(AA)触发超时
    CYCLE_SENT_BCP,                           // 周期发送BCP报文
    RECV_CML,                                 // 已经收到CML报文,自首次发送BCP报文起5S，没收到CML触发超时
    TIME_SYNC,                                // 时间同步处理
    PARAMETER_ADAPT,                          // 充电机参数适合
    STOP_SENT_BCP_CYCLE_SENT_BRO_00,          // 停止发送BCP报文,周期发送BRM报文(SPN2829=0X00)
    CYCLE_SENT_BRO_AA,                        // 变更BRO报文(SPN2829=0XAA)
    RECV_CRO_00,                              // 已经收到CRO报文，自首次发送BRO报文起5S，没收到CRO触发超时
    RECV_CRO_AA_STOP_SENT_BRO,                // 已经收到CRO报文(SPN2829=0XAA),停止发送BRO报文,自首次发送BRO报文起60S，没收到CRO(AA)触发超时
    CYCLE_SENT_BCL_BCS,                       // 周期发送BCL/BCS报文
    RECV_CCS,                                 // 已经收到CCS报文 ,自首次收到CRO报文起1S，没收到CCS触发超时
    CYCLE_SENT_BSM,                           // 周期发送BSM报文
    // CYCLE_SENT_BMV_BMT_BSP,                   // 周期发送BMV/BMT/BSP报文(可选)
    TOCLOSE,                                  // 充电结束或者收到CST
    CYCLE_SENT_BST,                           // 周期发送BST报文
    RECV_CST_STOP_SENT_BST,                   // 收到CST报文后停止发送BST报文,自首次发送BST报文起5S，没收到CST触发超时
    CYCLE_SENT_BSD,                           // 周期发送BSD报文
    EXIT_CHARGING,                            // 退出充电状态
    
}BMS_STATUS;

BMS_STATUS bstate = BSTART;
pid_t bchildren[NUM_BCHILDREN]={0};
int bpnum=0;

static void charging_init(){
    printf("车辆接口连接已确认\n");
}

static void time_sync(){
    printf("时间同步处理\n");
}

static void parameter_check(){
    printf("车辆参数合适(若不合适，则需发送BST报文并退出充电)\n");
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
            fprintf(stderr, "帧数据接收出错1: %s\n", strerror(errno));
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

    while(1){
        switch(bstate){
            case BSTART:
                charging_init();
                bstate=CONNECT_CONFIRM;
                break;
            case CONNECT_CONFIRM:
                while(1){
                    type = block_recv(sockfd,line_input,sizeof(line_input),UNKOWN,0,NULL);
                    if(type==CHM){
                        fprintf(output_file, "%s\n", line_output);
                        break;
                    }
                }
                bstate=RECV_CHM;    
                break;
            case RECV_CHM:
                bstate=CYCLE_SENT_BHM;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BHM, 250);//子进程1周期发送BHM
                }
                bpnum++;
                break;
            case CYCLE_SENT_BHM:
                ret = block_recv(sockfd,line_input,sizeof(line_input),CRM,30,"00");//阻塞接收CRM(00)，超时(大于30S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                terminate_children(bchildren[bpnum-1]);//杀死子进程1,停止发送BHM
                bstate=RECV_CRM_00_STOP_SENT_BHM;
                break;
            case RECV_CRM_00_STOP_SENT_BHM:
                bstate=CYCLE_SENT_BRM;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BRM, 250);//子进程2周期发送BRM
                }
                bpnum++;
                break;
            case CYCLE_SENT_BRM:
                ret = block_recv(sockfd,line_input,sizeof(line_input),CRM,5,"AA");//阻塞接收CRM(AA)，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                terminate_children(bchildren[bpnum-1]);//杀死子进程2,停止发送BRM
                bstate=RECV_CRM_AA_STOP_SENT_BRM;
                break;
            case RECV_CRM_AA_STOP_SENT_BRM:
                bstate=CYCLE_SENT_BCP;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BCP, 500);//子进程3周期发送BCP
                }
                bpnum++;
                break;
            case CYCLE_SENT_BCP:
                ret=block_recv(sockfd,line_input,sizeof(line_input),CML,5,NULL);//阻塞接收CML，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                bstate=RECV_CML;
                break;
            case RECV_CML:
                time_sync();
                bstate = TIME_SYNC;
                break;
            case TIME_SYNC:
                parameter_check();
                bstate = PARAMETER_ADAPT;
                break;
            case PARAMETER_ADAPT:
                terminate_children(bchildren[bpnum-1]);//杀死子进程3,停止发送BCP
                bstate=STOP_SENT_BCP_CYCLE_SENT_BRO_00;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BRO_00, 250);//子进程4周期发送BRO(00)
                }
                bpnum++;
                break;
            case STOP_SENT_BCP_CYCLE_SENT_BRO_00:
                sleep(2);//睡两秒，模拟充电准备过程
                terminate_children(bchildren[bpnum-1]);//杀死子进程4,停止发送BRO_00
                bstate=CYCLE_SENT_BRO_AA;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    first_sent_BRO_AA_time=time(NULL);
                    cycle_sent_frame(sockfd, BFRAME_BRO_AA, 250);//子进程5周期发送BRO(AA)
                }
                bpnum++;
                break;
            case CYCLE_SENT_BRO_AA:
                ret=block_recv(sockfd,line_input,sizeof(line_input),CRO,5,"00");//阻塞接收CRO_00，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                bstate = RECV_CRO_00;
                break;
            case RECV_CRO_00:
                ret=block_recv(sockfd,line_input,sizeof(line_input),CRO,(time(NULL)-first_sent_BRO_AA_time),"AA");//阻塞接收CRO_00，超时(大于5S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                terminate_children(bchildren[bpnum-1]);//杀死子进程5,停止发送BRO_AA
                bstate = RECV_CRO_AA_STOP_SENT_BRO;
                break;
            case RECV_CRO_AA_STOP_SENT_BRO:
                bstate=CYCLE_SENT_BCL_BCS;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BCL, 50);//子进程6周期发送BCL
                }
                bpnum++;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BCS, 250);//子进程7周期发送BCS
                }
                bpnum++;
                break;
            case CYCLE_SENT_BCL_BCS:
                ret=block_recv(sockfd,line_input,sizeof(line_input),CCS,1,NULL);//阻塞接收CCS,超时(1S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                bstate=RECV_CCS;
                break;
            case RECV_CCS:
                bstate=CYCLE_SENT_BSM;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BSM, 250);//子进程8周期发送BSM
                }
                bpnum++;
                break;
            case CYCLE_SENT_BSM:
                bover=0;
                int get_CST=0;
                in_charging(&charge_over);
                while(1){
                    type=block_recv(sockfd,line_input,sizeof(line_input),UNKOWN,0,NULL);
                    if(charge_over==1){
                        fprintf(output_file, "%s\n", line_output);
                        bover=1;
                        break;
                    }
                    if(type == CST){
                        get_CST=1;
                        fprintf(output_file, "%s\n", line_output);
                        break;
                    }
                }
                bstate = TOCLOSE;
                break;
            case TOCLOSE:
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BST, 250);//子进程9周期发送BST
                }
                bpnum++;
                break;
            case CYCLE_SENT_BST:
                if(bover==1){//判断是不是BMS主动中止充电
                    if(get_CST!=1){
                        //等待接收到CST或者不是BMS主动发起结束充电(这也说明已经收到CST)
                        ret=block_recv(sockfd,line_input,sizeof(line_input),CST,5,NULL);//阻塞等待接收CST,超时(5S)应进入超时处理，此处直接转退出状态
                        if(ret<0){
                            bstate=EXIT_CHARGING;
                        }
                    }
                }
                terminate_children(bchildren[bpnum-1]);//杀死子进程9,停止发送BST
                bstate=RECV_CST_STOP_SENT_BST;
                break;
            case RECV_CST_STOP_SENT_BST:
                bstate=CYCLE_SENT_BSD;
                bchildren[bpnum] =fork();
                if(bchildren[bpnum]<0){
                    printf("fork p%d error!\n",bpnum);
                    exit(EXIT_FAILURE);
                }else if(bchildren[bpnum]==0){
                    cycle_sent_frame(sockfd, BFRAME_BSD, 250);//子进程10周期发送BSD
                }
                bpnum++;
                break;
            case CYCLE_SENT_BSD:
                ret=block_recv(sockfd,line_input,sizeof(line_input),CSD,10,NULL);//阻塞接收CSD,超时(10S)应进入超时处理，此处直接转退出状态
                if(ret<0){
                    bstate=EXIT_CHARGING;
                }
                bstate=EXIT_CHARGING;
                break;
            case EXIT_CHARGING:
                terminate_allchildren(bchildren,NUM_BCHILDREN);
                over=1;
                break;
            default:
                printf("不合法的状态，程序退出\n");
                terminate_allchildren(bchildren,NUM_BCHILDREN);
                exit(EXIT_FAILURE);

        }
        if(over==1){
            break;
        }
    }

    /* 关闭套接字 */
    close(sockfd);
    return 0;
}

