#ifndef GBT27930_2015__H
#define GBT27930_2015__H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <cjson/cJSON.h>
#include <json-c/json.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include "cJSON.h"

#define FILEOPEN_ERROR      1
#define MALLOC_ERROR        2
#define FREAD_ERROR         3
#define JSONPARSE_ERROR     4


#define         UNKOWN       0           //非法PGN报文
//低压辅助上电及充电握手阶段
#define         CHM          1           //充电机握手报文
#define         BHM          2           //车辆握手报文
#define         CRM          3           //充电机辨识报文
#define         BRM          4           //BMS和车辆辨识报文    //多帧1
//充电参数配置阶段
#define         BCP          5           //动力蓄电池充电参数报文    //多帧1
#define         CTS          6           //充电机发送时间同步信息报文
#define         CML          7           //充电机最大输出能力报文
#define         BRO          8           //车辆充电准备就绪状态报文
#define         CRO          9           //充电机输出准备就绪状态报文
//充电阶段
#define         BCL          10          //电池充电需求报文
#define         BCS          11          //电池充电总状态报文    //多帧1
#define         CCS          12          //充电机充电状态报文
#define         BSM          13          //动力蓄电池状态信息报文
#define         BMV          14          //单体动力蓄电池电压报文    //多帧0
#define         BMT          15          //动力蓄电池温度报文    //多帧0
#define         BSP          16          //动力蓄电池预留报文    //多帧0
#define         BST          17          //车辆中止充电报文
#define         CST          18          //充电机中止充电报文
//充电结束阶段
#define         BSD          19          //车辆统计数据报文
#define         CSD          20          //充电机统计数据报文
//错误报文
#define         BEM          21          //BMS及车辆错误报文
#define         CEM          22          //充电机错误报文

#define         MULPRE       23          //多帧报文前面的部分

// 定义CAN解析完的数据结构
typedef struct
{
    char can_id[9];    // CAN ID
    char can_data[17]; // CAN数据，最多8个字节，以16进制字符串表示，每个字节占2个字符,加上终止符'\0'
} CANInfo;



extern char line_output[1024];
extern char line_input[1024];
extern FILE *output_file;
extern cJSON *pgn_json;
extern CANInfo caninfo;



void init();
void deinit();
char *pgn_content(const char *pgn, const char *can_data);
int can_parse(char *can_input,cJSON *pgn_json);



#endif