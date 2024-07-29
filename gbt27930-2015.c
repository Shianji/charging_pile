#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "cJSON.h"
char charging_addr[]="56";
char charging_name[]="charging_pile";
char bms_addr[]="F4";
char bms_name[]="BMS";
char pgn[]="000000\0";
char message[256]={0};

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
        dest=NULL;
        perror("dest addr adress is wrong!");
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
        source=NULL;
        perror("source adress is wrong!");
    }
    return source;
}

int get_pgn(char *message){
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

        // 将CAN ID从十六进制字符串转换为整数
        // result.can_id = strtoul(result.can_data, NULL, 16);

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
    char *ptr=message;
    cJSON *pgn_obj = cJSON_GetObjectItem(pgn_json, pgn);
    if (pgn_obj != NULL && cJSON_IsObject(pgn_obj)){
         cJSON *item = pgn_obj->child;
        while (item != NULL) {
            // 检查项的类型并格式化字符串
            if (item->type == cJSON_String) {
                ptr += sprintf(ptr, "%s\t", item->valuestring);
            } else if (item->type == cJSON_Number) {
                ptr += sprintf(ptr, "%d\t", item->valueint);
            }
            item = item->next;
        }
    }
    *ptr='\0';
    return ptr;
}


int main(int argc, char* argv[]) {
    // 定义CSV的头部字段
    const char* header_fields[] = {"帧编号", "帧id", "阶段", "PGN", "报文代号", "报文描述", "优先权", "源地址-目的地址", "帧长度", "帧数据", "帧数据含义"}; 
    const int num_header_fields = sizeof(header_fields) / sizeof(header_fields[0]);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <CAN log file>\n", argv[0]);
        return 1;
    }

    // Open the output CSV file
    char output_filename[256];
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    strftime(output_filename, sizeof(output_filename), "analysis-%Y-%m-%d_%H-%M-%S.csv", local_time);
    FILE* output_file = fopen(output_filename, "w");
    if (!output_file) {
        perror("Error opening output log file!");
        return 1;
    }

    // Write CSV header
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
        return EXIT_FAILURE;
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
    char line[1024];
    char line_input[1024]={0};
    CANInfo caninfo;
    char data_len_str[50]; 
    while (fgets(line, sizeof(line), input_file)) {
        int line_num = 0;
        // 去除每行末尾的换行符
        line[strcspn(line, "\n")] = 0;

        caninfo=format_data(line);
        char can_data_len = '0'+((strlen(caninfo.can_data))/2); // 获取字符串长度
        get_pgn(&caninfo.can_id);
        char *dest =dest_addr(caninfo.can_id);
        char *source=source_addr(caninfo.can_id);

        if(cJSON_HasObjectItem(pgn_json, pgn)){
            sprintf(line_input, "%d\t0x%s\t", line_num, caninfo.can_id);
            char *message = pgn_message(pgn,pgn_json);
        }else{
            //多帧处理
        }

        
    }

    // 清理资源
    cJSON_Delete(pgn_json);
    free(file_content);
    fclose(pgn_file);

    return 0;
}