#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "cJSON.h"
char charging_addr[]="56";
char charging_name[]="充电机";
char bms_addr[]="F4";
char bms_name[]="BMS";
char error_addr[]="???";
char pgn[]="000000";
char mul_pgn[]="000000";
char info[256]={0};
char muldata[256]={0};
const char *mul_frame_id[] = {"1CEC56F4", "1CECF456", "1CEB56F4", "1CECF456"};

//定义CAN数据结构
typedef struct {
    char can_id[9];   // CAN ID
    char can_data[17];      // CAN数据，最多8个字节，以16进制字符串表示，每个字节占2个字符,加上终止符'\0'
} CANInfo;


char *dest_addr(char *message){
    char *dest;
    if(strncmp(message,charging_addr,2)==0)
        dest =charging_name;
    else if(strncmp(message,bms_addr,2)==0)
        dest=bms_name;
    else{
        dest=error_addr;
        // perror("dest addr adress is wrong!");
    }
    return dest;
}

char *source_addr(char *message){
    char *source;
    if(strncmp(message,charging_addr,2)==0)
        source =charging_name;
    else if(strncmp(message,bms_addr,2)==0)
        source=bms_name;
    else{
        source=error_addr;
        // perror("source adress is wrong!");
    }
    return source;
}

int get_pgn(char *message){
    memset(pgn,'0',sizeof(pgn)-1);
    char *endptr;
    char *p=message+2;
    pgn[2]=*p++;
    pgn[3]=*p;
    long decimal = strtol(pgn, &endptr, 16);
    if (*endptr != '\0') {
        fprintf(stderr, "Conversion failed: invalid hex string.\n");
        exit(1);
    }
    int size = snprintf(NULL, 0, "%ld", decimal) + 1;
    snprintf(pgn, size, "%ld", decimal);
    pgn[size]='\0';

    return 0;
}

// 提取can_id和can_data
CANInfo format_data(const char* line) {
    CANInfo result;
    const char* p_id = strrchr(line, ' '); // 查找最后一个空格
    const char* p_data = strchr(line, '#'); // 查找'#'字符

    if (p_id && p_data && p_data > p_id) {
        // 提取CAN ID
        int cid_len = p_data - p_id - 1;
        strncpy(result.can_id, p_id + 1, cid_len);
        result.can_id[cid_len] = '\0'; // 添加字符串终止符

        // 提取CAN数据
        int cdata_len = strlen(line) - (p_data - line + 1);
        strncpy(result.can_data, p_data + 1, cdata_len);
        result.can_data[cdata_len] = '\0'; // 添加字符串终止符
    } else {
        strcpy(result.can_id, "");
        strcpy(result.can_data, "");
    }

    return result;
}


char *pgn_message(char *pgn, cJSON *pgn_json) {
    memset(info,0,sizeof(info));
    char *ptr=info;
    cJSON *pgn_obj = cJSON_GetObjectItem(pgn_json, pgn);
    if (pgn_obj != NULL && cJSON_IsObject(pgn_obj)){
        cJSON *item = pgn_obj->child;
        while (item != NULL) {
            // 检查项的类型并格式化字符串
            if (item->type == cJSON_String) {
                ptr += sprintf(ptr, "%s,", item->valuestring);
            } else if (item->type == cJSON_Number) {
                ptr += sprintf(ptr, "%d,", item->valueint);
            }
            item = item->next;
        }
        *ptr='\0';
        ptr=info;
    }else{
        ptr=NULL;
    }
    return ptr;
}

int ismul_frame(char *id){
    int len=sizeof(mul_frame_id)/sizeof(mul_frame_id[0]);
    for(int i=0;i<len;i++){
        if(strncmp(id,mul_frame_id[i],8)==0){
            return 1;
        }
    }
    return 0;
}


char *pgn_content(char *pgn, char *can_data){
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <CAN日志文件>\n", argv[0]);
        return 1;
    }

    // 定义CSV的头部字段
    const char* header_fields[] = {"帧编号", "帧id", "阶段", "PGN", "报文代号", "报文描述", "优先权", "源地址-目的地址", "帧长度", "帧数据", "帧数据含义"}; 
    const int num_header_fields = sizeof(header_fields) / sizeof(header_fields[0]);

    // 打开将要输出的目标csv文件
    char output_filename[256];
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    strftime(output_filename, sizeof(output_filename), "analysis-%Y-%m-%d_%H-%M-%S.csv", local_time);
    FILE* output_file = fopen(output_filename, "w");
    if (!output_file) {
        perror("Error opening output log file!");
        return 1;
    }

    // 写csv文件头
    for (int i = 0; i < num_header_fields; ++i) {
        fprintf(output_file, "%s%c", header_fields[i], (i < num_header_fields - 1) ? ',' : '\n');
    }

    FILE* pgn_file = fopen("PGN.json", "r");
    if (!pgn_file) {
        perror("Error opening PGN json file");
        fclose(output_file);
        return 1;
    }

     // 计算文件大小
    fseek(pgn_file, 0, SEEK_END);
    long file_size = ftell(pgn_file);
    fseek(pgn_file, 0, SEEK_SET);

    // 读取文件内容
    char *file_content = (char *)malloc(file_size + 1);
    if (file_content == NULL) {
        perror("Error allocating memory for file content");
        fclose(pgn_file);
        fclose(output_file);
        return 1;
    }

    size_t result = fread(file_content, 1, file_size, pgn_file);
    if (result != (size_t)file_size) {
        perror("Error reading file");
        free(file_content);
        fclose(pgn_file);
        fclose(output_file);
        return 1;
    }

    // 解析JSON
    cJSON *pgn_json = cJSON_Parse(file_content);
    if (pgn_json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        fprintf(stderr, "Error parsing PGN.json: %s\n", error_ptr ? error_ptr : "Unknown error");
        free(file_content);
        fclose(pgn_file);
        fclose(output_file);
        return 1;
    }

    //打开CAN日志文件
    FILE* input_file = fopen(argv[1], "r");
    if (!input_file) {
        perror("Error opening CAN log file");
        fclose(pgn_file);
        fclose(output_file);
        return 1;
    }

    // 使用pgn_json进行逻辑处理
    char line_input[1024];
    char line_output[1024];
    CANInfo caninfo;
    char data_len_str[50]; 
    int line_num = 0;
    while (fgets(line_input, sizeof(line_input), input_file)) {
        // 去除每行末尾的换行符
        line_input[strcspn(line_input, "\n")] = 0;

        caninfo=format_data(line_input);
        int can_data_len = ((strlen(caninfo.can_data))/2); // 获取字符串长度
        get_pgn(caninfo.can_id);
        char *dest =dest_addr(&caninfo.can_id[4]);
        char *source=source_addr(&caninfo.can_id[6]);
        memset(line_output,0,sizeof(line_output));
        char *ptr=line_output;
        char *mulptr=muldata,*mulpgn=mul_pgn;

        if(cJSON_HasObjectItem(pgn_json, pgn)){
            ptr+=sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
            char *message = pgn_message(pgn,pgn_json);
            ptr+=sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);

            char *content = pgn_content(pgn, caninfo.can_data);
            sprintf(ptr, "%s", content);
            fprintf(output_file, "%s\n", line_output);
        }
        else if(ismul_frame(caninfo.can_id)){//多帧处理
            int byte_num,frame_num,frame_first,receive_byte_num,receive_frame_num;
            static int mul_frame_num=0;
            char num[3];           
            char *pcandata=caninfo.can_data;
            get_pgn(pcandata+10);
            ptr+=sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
            char *message = pgn_message(pgn,pgn_json);
            ptr+=sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);
            
            if(strncmp(pcandata,"10",2)==0){
                memset(num,0,3);
                strncpy(num,(pcandata+2),2);
                byte_num=(int)(strtol(num,NULL,16));
                strncpy(num,(pcandata+6),2);
                frame_num=(int)(strtol(num,NULL,16));
                sprintf(ptr, "多帧发送请求帧 总字节数为%d 需要发送的总帧数为%d", byte_num, frame_num);
            }else if(strncmp(pcandata,"11",2)==0){
                memset(num,0,3);
                strncpy(num,(pcandata+2),2);
                frame_num=(int)(strtol(num,NULL,16));
                strncpy(num,(pcandata+4),2);
                frame_first=(int)(strtol(num,NULL,16));
                strncpy(mul_pgn,pgn,sizeof(pgn));
                sprintf(ptr, "多帧请求响应帧 可发送帧数为%d 多帧发送时首帧的帧号为%d", frame_num, frame_first);
            }else if(strncmp(pcandata,"13",2)==0){
                memset(num,0,3);
                strncpy(num,(pcandata+2),2);
                receive_byte_num=(int)(strtol(num,NULL,16));
                strncpy(num,(pcandata+6),2);
                receive_frame_num=(int)(strtol(num,NULL,16));
                ptr+=sprintf(ptr, "多帧接收完成帧 收到总字节数为%d 收到总帧数为%d \n多帧解析结果：【", receive_byte_num, receive_frame_num);
                
                char *content=pgn_content(pgn,muldata);
                sprintf(ptr, "%s】", content);        
                
                memset(muldata,0,sizeof(muldata));
                mulptr=muldata;
                mul_frame_num=0;
            }else{
                mulptr+=sprintf(mulptr, "%s", (pcandata+2));
                mul_frame_num++;

                memset(line_output,0,  sizeof(line_output));
                ptr=line_output;
                ptr+=sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
                message=pgn_message(mul_pgn,pgn_json);
                ptr+=sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);
                sprintf(ptr, "多帧发送的第%d帧", mul_frame_num);
            }
            fprintf(output_file, "%s\n", line_output);
        }else{
            sprintf(ptr, "%d,0x%s,-,-,-,-,-,%s-%s,%d,0x%s,-", line_num, caninfo.can_id, source, dest, can_data_len, caninfo.can_data);
            fprintf(output_file, "%s\n", line_output);        
        }  
        line_num++;
    }

    // 清理资源
    cJSON_Delete(pgn_json);
    free(file_content);
    fclose(pgn_file);
    fclose(input_file);
    fclose(output_file);

    return 0;
}
