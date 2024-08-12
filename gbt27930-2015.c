#include "gbt27930-2015.h"

char charging_addr[] = "56";
char charging_name[] = "充电机";
char bms_addr[] = "F4";
char bms_name[] = "BMS";
char error_addr[] = "???";
char pgn[] = "000000";
char mul_pgn[] = "000000";
char info[256] = {0};
char muldata[256] = {0},*mulptr=muldata;//存储多帧数据
const char *mul_frame_id[] = {"1CEC56F4", "1CECF456", "1CEB56F4", "1CECF456"};
char data_parse[1024] = {0};
uint8_t line_input[1024] = {0};
char line_output[1024] = {0};
CANInfo caninfo;
int line_num = 0;
char output_filename[256];
FILE *output_file;
cJSON *pgn_json;

// 反转字节数组
static void reverse_bytes(unsigned char *byte_array, int byte_array_len) {
    for (int i = 0; i < byte_array_len / 2; i++) {
        unsigned char temp = byte_array[i];
        byte_array[i] = byte_array[byte_array_len - 1 - i];
        byte_array[byte_array_len - 1 - i] = temp;
    }
}

// 将十六进制字符串转换为整数
int hex_string_to_int(const char* hex) {
    int value;
    sscanf(hex, "%x", &value);
    return value;
}

// 将十六进制字符串转换为字节数组
static unsigned char* hex_string_to_bytes(const char* hex) {
    size_t len = strlen(hex);
    unsigned char* bytes = (unsigned char*)malloc(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        sscanf(hex + i, "%2hhx", &bytes[i / 2]);
    }
    return bytes;
}

// 将字节数组转换为十六进制字符串
static void bytes_to_hex_string(const uint8_t *data, char *string,int data_len) {
    // 每个字节对应2个字符的十六进制表示，再加上1个终止符
    size_t str_len = data_len * 2 + 1;
    
    for (size_t i = 0; i < data_len; i++) {
        snprintf(&string[i * 2], 3, "%02X", data[i]);
    }

    return;
}


// 将十六进制字符转换为对应的二进制字符串
static void hexToBin(char hex, char* bin) {
    switch(hex) {
        case '0': strcpy(bin, "0000"); break;
        case '1': strcpy(bin, "0001"); break;
        case '2': strcpy(bin, "0010"); break;
        case '3': strcpy(bin, "0011"); break;
        case '4': strcpy(bin, "0100"); break;
        case '5': strcpy(bin, "0101"); break;
        case '6': strcpy(bin, "0110"); break;
        case '7': strcpy(bin, "0111"); break;
        case '8': strcpy(bin, "1000"); break;
        case '9': strcpy(bin, "1001"); break;
        case 'A': strcpy(bin, "1010"); break;
        case 'B': strcpy(bin, "1011"); break;
        case 'C': strcpy(bin, "1100"); break;
        case 'D': strcpy(bin, "1101"); break;
        case 'E': strcpy(bin, "1110"); break;
        case 'F': strcpy(bin, "1111"); break;
        default: strcpy(bin, ""); break; 
    }
}

// 将整个十六进制字符串转换为二进制字符串
static void hexStringToBinString(const char* hexStr, char* binStr) {
    char bin[256]; 
    binStr[0] = '\0'; 

    for (int i = 0; i < strlen(hexStr); i++) {
        hexToBin(hexStr[i], bin);
        strcat(binStr, bin);
    }
}

// 将二进制字符串左边补0到指定长度
static void padLeftWithZeros(char* binStr, int totalLength) {
    int len = strlen(binStr);
    if (len < totalLength) {
        // 创建一个新的字符串，包含足够的空间来存储填充后的字符串
        char* paddedStr = (char*)malloc(totalLength + 1);
        // 将原字符串移到新位置
        memmove(paddedStr + (totalLength - len), binStr, len + 1);
        // 用'0'填充左侧
        memset(paddedStr, '0', totalLength - len);
        // 将结果复制回原字符串
        strcpy(binStr, paddedStr);
        // 释放临时字符串的内存
        free(paddedStr);
    }
}

// 从浮点数中提取小数部分，转换为整数，然后减去 1
static int getDecimalPart(double number) {
    char value_str[50];  
    sprintf(value_str, "%.1f", number);  
    char *decimal_part_str = strchr(value_str, '.');  // 查找小数点
    if (decimal_part_str != NULL) {
        decimal_part_str++;  // 跳过小数点
        int decimal_part_int = atoi(decimal_part_str);  
        return decimal_part_int - 1;  
    }
    return -1;  
}

static char *dest_addr(char *message)
{
    char *dest;
    if (strncmp(message, charging_addr, 2) == 0)
        dest = charging_name;
    else if (strncmp(message, bms_addr, 2) == 0)
        dest = bms_name;
    else
    {
        dest = error_addr;
        // perror("dest addr adress is wrong!");
    }
    return dest;
}

static char *source_addr(char *message)
{
    char *source;
    if (strncmp(message, charging_addr, 2) == 0)
        source = charging_name;
    else if (strncmp(message, bms_addr, 2) == 0)
        source = bms_name;
    else
    {
        source = error_addr;
        // perror("source adress is wrong!");
    }
    return source;
}

static void errorjudge(FILE *file, char *filename)
{
    if (!file)
    {
        printf("something wrong with open or write file:%s\n", filename);
        exit(FILEOPEN_ERROR);
    }
}

static int get_pgn(char *message)
{
    memset(pgn, '0', sizeof(pgn) - 1);
    char *endptr;
    char *p = message + 2;
    pgn[2] = *p++;
    pgn[3] = *p;
    long decimal = strtol(pgn, &endptr, 16);
    if (*endptr != '\0')
    {
        fprintf(stderr, "16进制转长整型转换失败\n");
        exit(1);
    }
    int size = snprintf(NULL, 0, "%ld", decimal) + 1;
    snprintf(pgn, size, "%ld", decimal);
    pgn[size] = '\0';

    return 0;
}

// 提取can_id和can_data
static CANInfo format_data(const uint8_t *line,int len)
{
    CANInfo result;
    if(len!=12){
        printf("can数据长度出错，应为4字节帧头加8字节数据，实际输入%d字节数据\n",len);
    }
    // 提取CAN ID
    memset(result.can_id,0,sizeof(result.can_id));
    bytes_to_hex_string(line,result.can_id,4);
    // 提取CAN数据
    memset(result.can_data,0,sizeof(result.can_id));
    bytes_to_hex_string(line+4,result.can_data,8);

    return result;
}

static char *pgn_message(char *pgn, cJSON *pgn_json)
{
    memset(info, 0, sizeof(info));
    char *ptr = info;
    cJSON *pgn_obj = cJSON_GetObjectItem(pgn_json, pgn);
    if (pgn_obj != NULL && cJSON_IsObject(pgn_obj))
    {
        cJSON *item = pgn_obj->child;
        while (item != NULL)
        {
            // 检查项的类型并格式化字符串
            if (item->type == cJSON_String)
            {
                ptr += sprintf(ptr, "%s,", item->valuestring);
            }
            else if (item->type == cJSON_Number)
            {
                ptr += sprintf(ptr, "%d,", item->valueint);
            }
            item = item->next;
        }
        *ptr = '\0';
        ptr = info;
    }
    else
    {
        ptr = NULL;
    }
    return ptr;
}

static int ismul_frame(char *id)
{
    int len = sizeof(mul_frame_id) / sizeof(mul_frame_id[0]);
    for (int i = 0; i < len; i++)
    {
        if (strncmp(id, mul_frame_id[i], 8) == 0)
        {
            return 1;
        }
    }
    return 0;
}

//返回某键值在json对象中的序号，如果没有，则返回0,参数传入出错返回-1
static int get_key_index(cJSON *json, const char *key) {
    if (json == NULL || key == NULL) {
        return -1;
    }

    int index = 0;
    cJSON *current_element = NULL;
    cJSON_ArrayForEach(current_element, json) {
        if (current_element->string != NULL && strcmp(current_element->string, key) == 0) {
            return index+1;
        }
        index++;
    }

    return 0;
}


char *pgn_content(const char *p, const char *cd) {
    memset(data_parse, 0, sizeof(data_parse));
    FILE *f = fopen("SPN.json", "r");
    if (!f) {
        perror("Failed to open file");
        exit(FREAD_ERROR);
    }

    struct json_object *spn_json;
    struct json_object *parsed_json = json_object_from_file("SPN.json");
    if (!parsed_json) {
        perror("Failed to parse JSON");
        fclose(f);
        exit(JSONPARSE_ERROR);
    }

    spn_json = json_object_object_get(parsed_json, p);
    if (!spn_json) {
        perror("Failed to get JSON object");
        fclose(f);
        exit(JSONPARSE_ERROR);
    }

    int arraylen = json_object_array_length(spn_json);
    for (int i = 0; i < arraylen; i++) {
        struct json_object *sl = json_object_array_get_idx(spn_json, i);
        const char *process_mode = json_object_get_string(json_object_object_get(sl, "process_mode"));
        // 处理select模式
        if (strcmp(process_mode, "select") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));
            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];

                // 计算起始位置和结束位置
                int start_index = (start_byte_or_bit - 1) * 2;
                int end_index = (start_byte_or_bit + length - 1) * 2;
                // 计算要复制的字符数
                int num_chars_to_copy = end_index - start_index;
                // 复制子字符串并确保目标字符串以空字符结尾
                strncpy(d, cd + start_index, num_chars_to_copy);
                d[num_chars_to_copy] = '\0';
                if (strlen(d) > 0) {
                    // 将字节转换为16
                    for (int j = 0; j < strlen(d) / 2; j += 2) {
                        char temp = d[j];
                        d[j] = d[strlen(d) - j - 2];
                        d[strlen(d) - j - 2] = temp;
                        temp = d[j + 1];
                        d[j + 1] = d[strlen(d) - j - 1];
                        d[strlen(d) - j - 1] = temp;
                    }
                    for (int j = 0; j < strlen(d); j++) {
                        d[j] = toupper(d[j]);
                    }

                    struct json_object *content = json_object_object_get(sl, "content");
                    struct json_object *definition_data = json_object_object_get(sl, "definition_data");
                    int index = -1;
                    for (int j = 0; j < json_object_array_length(definition_data); j++) {
                        if (strcmp(json_object_get_string(json_object_array_get_idx(definition_data, j)), d) == 0) {
                            index = j;
                            break;
                        }
                    }
                    if (index != -1) {
                        strcat(data_parse, json_object_get_string(json_object_array_get_idx(content, index)));
                        strcat(data_parse, "；");
                    }
                }
            } else {
                // 处理非整数起始字节或位
                struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
                double start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
                int length = json_object_get_int(json_object_object_get(sl, "长度"));
                if (!json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                    double start_byte_or_bit = json_object_get_double(start_byte_or_bit_obj);
                    int n;
                    if (length >= 8) {
                        if (length % 8 == 0) {
                            n = (length / 8) + (int)start_byte_or_bit;
                        } else {
                            n = (length / 8) + (int)start_byte_or_bit + 1;
                        }
                    } else {
                        n = (int)start_byte_or_bit;
                    }

                    int start_index = ((int)start_byte_or_bit - 1) * 2;
                    int end_index = n * 2;
                    char d[256];
                    strncpy(d, cd + start_index, end_index - start_index);
                    d[end_index - start_index] = '\0';

                    if (strlen(d) > 0) {
                        // 转换字节为16进制
                        for (int j = 0; j < strlen(d) / 2; j += 2) {
                        char temp = d[j];
                        d[j] = d[strlen(d) - j - 2];
                        d[strlen(d) - j - 2] = temp;
                        temp = d[j + 1];
                        d[j + 1] = d[strlen(d) - j - 1];
                        d[strlen(d) - j - 1] = temp;
                    }

                        for (int j = 0; j < strlen(d); j++) {
                            d[j] = toupper(d[j]);
                        }
                        
                        char binStr[256]; 
                        hexStringToBinString(d, binStr);
                        int fill_zero = 8 * (n - (int)start_byte_or_bit + 1);
                        // 调用左侧补0方法
                        padLeftWithZeros(binStr, fill_zero);

                        int sb = getDecimalPart(start_byte_or_bit);

                        char db[1024] = "";
                        strcat(db, binStr); 

                        //char sb_str[16];
                        //snprintf(sb_str, sizeof(sb_str), "%f", start_byte_or_bit);
                        int eb = sb + length + 1;
                        if (sb == 0) {
                            eb--;
                        }
                        char section[256];
                        strncpy(section, db + sb, eb - sb);
                        section[eb - sb] = '\0';

                        struct json_object *content = json_object_object_get(sl, "content");
                        struct json_object *definition_data = json_object_object_get(sl, "definition_data");
                        int index = -1;
                        for (int j = 0; j < json_object_array_length(definition_data); j++) {
                            if (strcmp(json_object_get_string(json_object_array_get_idx(definition_data, j)), section) == 0) {
                                index = j;
                                break;
                            }
                        }
                        if (index != -1) {
                            strcat(data_parse, json_object_get_string(json_object_array_get_idx(content, index)));
                            strcat(data_parse, "；");
                        } else {
                            strcat(data_parse, "解析出错标准无");
                            strcat(data_parse, section);
                            strcat(data_parse, "所代表的含义；");
                        }
                    }
                }
            }
        } else if (strcmp(process_mode, "ascii") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));
            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];

                // 计算起始位置和结束位置
                int start_index = (start_byte_or_bit - 1) * 2;
                int end_index = (start_byte_or_bit + length - 1) * 2;
                // 计算要复制的字符数
                int num_chars_to_copy = end_index - start_index;
                // 复制子字符串并确保目标字符串以空字符结尾
                strncpy(d, cd + start_index, num_chars_to_copy);
                d[num_chars_to_copy] = '\0';

                if (strlen(d) > 0) {
                    char ascii_string[128] = "";
                    for (int j = 0; j < strlen(d); j += 2) {
                        char hex_byte[3] = {d[j], d[j + 1], '\0'};
                        char ascii_char = (char)strtol(hex_byte, NULL, 16);
                        strncat(ascii_string, &ascii_char, 1);
                    }
                    strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(data_parse, ascii_string);
                    strcat(data_parse, "；");
                }
            }
        // 处理caculate模式
        } else if (strcmp(process_mode, "calculate") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
                int length = json_object_get_int(json_object_object_get(sl, "长度"));
                char d[512];
                // 计算起始位置和结束位置
                int start_index = (start_byte_or_bit - 1) * 2;
                int end_index = (start_byte_or_bit + length - 1) * 2;
                // 计算要复制的字符数
                int num_chars_to_copy = end_index - start_index;
                // 复制子字符串并确保目标字符串以空字符结尾
                strncpy(d, cd + start_index, num_chars_to_copy);
                d[num_chars_to_copy] = '\0';

                if (strlen(d) > 0) {
                    // 反转字节转为16进制
                    for (int j = 0; j < strlen(d) / 2; j += 2) {
                        char temp = d[j];
                        d[j] = d[strlen(d) - j - 2];
                        d[strlen(d) - j - 2] = temp;
                        temp = d[j + 1];
                        d[j + 1] = d[strlen(d) - j - 1];
                        d[strlen(d) - j - 1] = temp;
                    }

                    for (int j = 0; j < strlen(d); j++) {
                        d[j] = toupper(d[j]);
                    }
                    double data_resolution = json_object_get_double(json_object_object_get(sl, "data_resolution"));
                    double offset = json_object_get_double(json_object_object_get(sl, "offset"));
                    double result = data_resolution * strtol(d, NULL, 16) + offset;
      
                    if (result < 0) {
                        result = fabs(result);
                    }
                    
                    char result_str[128];
                    snprintf(result_str, sizeof(result_str), "%.2f", result);
                    strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(data_parse, result_str);
                    strcat(data_parse, json_object_get_string(json_object_object_get(sl, "units")));
                    strcat(data_parse, "；");
                }
            } else {
                struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
                double start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
                int length = json_object_get_int(json_object_object_get(sl, "长度"));
                if (!json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                    double start_byte_or_bit = json_object_get_double(start_byte_or_bit_obj);
                    int n;
                    if (length >= 8) {
                        if (length % 8 == 0) {
                            n = (length / 8) + (int)start_byte_or_bit;
                        } else {
                            n = (length / 8) + (int)start_byte_or_bit;
                        }
                    } else {
                        n = (int)start_byte_or_bit;
                    }

                    int start_index = ((int)start_byte_or_bit - 1) * 2;
                    int end_index = n * 2;
                    char d[256];
                    
                    int num_chars_to_copy = end_index - start_index;
                    strncpy(d, cd + start_index, num_chars_to_copy);
                    d[num_chars_to_copy] = '\0';
                    if (strlen(d) > 0) {
                        char binStr[256]; 
                        hexStringToBinString(d, binStr);
                        int fill_zero = 8 * (n - (int)start_byte_or_bit + 1);
                        // 调用左侧补0方法
                        padLeftWithZeros(binStr, fill_zero);
                        int sb = getDecimalPart(start_byte_or_bit);
                        char db[1024] = "";
                        strcat(db, binStr);
                        int eb = sb + length + 1;
                        if (sb == 0) {
                            eb--; // eb = 12
                        }

                        char section[256];
                        strncpy(section, db + sb, eb - sb);
                        section[eb - sb] = '\0';
                        // 计算偏移量
                        double data_resolution = json_object_get_double(json_object_object_get(sl, "data_resolution"));
                        double offset = json_object_get_double(json_object_object_get(sl, "offset"));
                        double result = data_resolution * strtol(section, NULL, 2) + offset;
                        if (result < 0) {
                            result = fabs(result);
                        }
                        char result_str[128];
                        snprintf(result_str, sizeof(result_str), "%.2f", result);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(data_parse, result_str);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "units")));
                        strcat(data_parse, "；");
                    }
                }
            }
        } else if (strcmp(process_mode, "date") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));
            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                // 计算起始位置和结束位置
                int start_index = (start_byte_or_bit - 1) * 2;
                int end_index = (start_byte_or_bit + length - 1) * 2;
                // 计算要复制的字符数
                int num_chars_to_copy = end_index - start_index;
                // 复制子字符串并确保目标字符串以空字符结尾
                strncpy(d, cd + start_index, num_chars_to_copy);
                d[num_chars_to_copy] = '\0';

                if (strlen(d) > 0) {
                    if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2571") == 0) {
                        char year_hex[3];
                        strncpy(year_hex, d, 2);
                        year_hex[2] = '\0';  // 添加字符串结束符
                        int year = hex_string_to_int(year_hex) + 1985;
                        
                        // 提取月份
                        char month_hex[3];
                        strncpy(month_hex, d + 2, 2);
                        month_hex[2] = '\0';  
                        int month = hex_string_to_int(month_hex);

                        // 提取日期
                        char day_hex[3];
                        strncpy(day_hex, d + 4, 2);
                        day_hex[2] = '\0';  
                        int day = hex_string_to_int(day_hex);
                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(data_parse, date_str);
                        strcat(data_parse, "；");
                    } else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2576") == 0) {
                        // 提取第4到第7个字符（从索引3到索引7）
                        char hex_substr[5];
                        strncpy(hex_substr, d + 4, 4);
                        hex_substr[4] = '\0';
                        unsigned char* bytes = hex_string_to_bytes(hex_substr);    
                        reverse_bytes(bytes, 2);
                        char reversed_hex[5];
                        snprintf(reversed_hex, 5, "%02X%02X", bytes[0], bytes[1]);
                        int year = hex_string_to_int(reversed_hex);

                        // 提取年份
                        char month_hex[3];
                        strncpy(month_hex, d + 2, 2);
                        month_hex[2] = '\0';  
                        int month = hex_string_to_int(month_hex);
                        
                        // 提取日期
                        char day_hex[3];
                        strncpy(day_hex, d, 2);
                        day_hex[2] = '\0';  
                        int day = hex_string_to_int(day_hex);

                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(data_parse, date_str);
                        strcat(data_parse, "；");
                    } else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2823") == 0) {
                        // 提取第10到第13个字符（从索引9到索引13）
                        char hex_substr[5];
                        strncpy(hex_substr, d + 10, 4);
                        hex_substr[4] = '\0';
                        unsigned char* bytes = hex_string_to_bytes(hex_substr);    
                        reverse_bytes(bytes, 2);
                        char reversed_hex[5];
                        snprintf(reversed_hex, 5, "%02X%02X", bytes[0], bytes[1]);

                        int year = hex_string_to_int(reversed_hex);

                        char month_hex[3], day_hex[3], hour_hex[3], minute_hex[3], second_hex[3];

                        strncpy(month_hex, d + 8, 2);
                        month_hex[2] = '\0';
                        int month = hex_string_to_int(month_hex);

                        strncpy(day_hex, d + 6, 2);
                        day_hex[2] = '\0';
                        int day = hex_string_to_int(day_hex);

                        strncpy(hour_hex, d + 4, 2);
                        hour_hex[2] = '\0';
                        int hour = hex_string_to_int(hour_hex);

                        strncpy(minute_hex, d + 2, 2);
                        minute_hex[2] = '\0';
                        int minute = hex_string_to_int(minute_hex);

                        strncpy(second_hex, d, 2);
                        second_hex[2] = '\0';
                        int second = hex_string_to_int(second_hex);

                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日%d时%d分%d秒", year, month, day, hour, minute, second);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(data_parse, date_str);
                        strcat(data_parse, "；");
                    }
                }
            }
        } else if (strcmp(process_mode, "null") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));
            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                // 计算起始位置和结束位置
                int start_index = (start_byte_or_bit - 1) * 2;
                int end_index = (start_byte_or_bit + length - 1) * 2;
                // 计算要复制的字符数
                int num_chars_to_copy = end_index - start_index;
                // 复制子字符串并确保目标字符串以空字符结尾
                strncpy(d, cd + start_index, num_chars_to_copy);
                d[num_chars_to_copy] = '\0';

                if (strlen(d) > 0) {
                    strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(data_parse, "；");
                    strcat(data_parse, "帧数据为");
                    strcat(data_parse, d);
                    strcat(data_parse, "；");
                }
            }
        }        
    }
    fclose(f);
    return data_parse;
}

int can_parse(uint8_t *can_input,cJSON *pgn_json, size_t can_input_len)
{
    line_num++;
    int pgn_type = 0;

    caninfo = format_data(can_input,can_input_len);

    // printf("帧id是：%s    ",caninfo.can_id);
    // printf("帧data是：%s\n",caninfo.can_data);

    int can_data_len = ((strlen(caninfo.can_data)) / 2); // 获取字符串长度
    get_pgn(caninfo.can_id);
    char *dest = dest_addr(&caninfo.can_id[4]);
    char *source = source_addr(&caninfo.can_id[6]);
    memset(line_output, 0, sizeof(line_output));
    char *ptr = line_output;

    if (cJSON_HasObjectItem(pgn_json, pgn))
    {
        pgn_type=get_key_index(pgn_json, pgn);
        ptr += sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
        char *message = pgn_message(pgn, pgn_json);
        ptr += sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);

        char *content = pgn_content(pgn, caninfo.can_data);
        sprintf(ptr, "%s", content);
    }
    else if (ismul_frame(caninfo.can_id))
    { // 多帧处理
        int byte_num, frame_num, frame_first, receive_byte_num, receive_frame_num;
        static int mul_frame_num = 0;
        char num[3];
        char *pcandata = caninfo.can_data;
        get_pgn(pcandata + 10);
        ptr += sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
        char *message = pgn_message(pgn, pgn_json);
        ptr += sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);

        if (strncmp(pcandata, "10", 2) == 0)
        {
            memset(num, 0, 3);
            strncpy(num, (pcandata + 2), 2);
            byte_num = (int)(strtol(num, NULL, 16));
            strncpy(num, (pcandata + 6), 2);
            frame_num = (int)(strtol(num, NULL, 16));
            strncpy(mul_pgn, pgn, sizeof(pgn));
            sprintf(ptr, "多帧发送请求帧 总字节数为%d 需要发送的总帧数为%d", byte_num, frame_num);
            pgn_type=get_key_index(pgn_json, pgn);
        }
        else if (strncmp(pcandata, "11", 2) == 0)
        {
            memset(num, 0, 3);
            strncpy(num, (pcandata + 2), 2);
            frame_num = (int)(strtol(num, NULL, 16));
            strncpy(num, (pcandata + 4), 2);
            frame_first = (int)(strtol(num, NULL, 16));
            sprintf(ptr, "多帧请求响应帧 可发送帧数为%d 多帧发送时首帧的帧号为%d", frame_num, frame_first);
            pgn_type=MULPRE;
        }
        else if (strncmp(pcandata, "13", 2) == 0)
        {
            memset(num, 0, 3);
            strncpy(num, (pcandata + 2), 2);
            receive_byte_num = (int)(strtol(num, NULL, 16));
            strncpy(num, (pcandata + 6), 2);
            receive_frame_num = (int)(strtol(num, NULL, 16));
            ptr += sprintf(ptr, "多帧接收完成帧 收到总字节数为%d 收到总帧数为%d \n多帧解析结果：【", receive_byte_num, receive_frame_num);

            char *content = pgn_content(pgn, muldata);
            sprintf(ptr, "%s】", content);
            memset(muldata, 0, sizeof(muldata));
            mulptr = muldata;
            mul_frame_num = 0;
            pgn_type=MULPRE;
        }
        else
        {
            mulptr += sprintf(mulptr, "%s", (pcandata + 2));
            mul_frame_num++;

            memset(line_output, 0, sizeof(line_output));
            ptr = line_output;
            ptr += sprintf(ptr, "%d,0x%s,", line_num, caninfo.can_id);
            message = pgn_message(mul_pgn, pgn_json);
            ptr += sprintf(ptr, "%s%s-%s,%d,0x%s,", message, source, dest, can_data_len, caninfo.can_data);
            sprintf(ptr, "多帧发送的第%d帧", mul_frame_num);
            pgn_type=MULPRE;
        }
    }
    else
    {
        sprintf(ptr, "%d,0x%s,-,-,-,-,-,%s-%s,%d,0x%s,-", line_num, caninfo.can_id, source, dest, can_data_len, caninfo.can_data);
    }
    return pgn_type;
}

// 初始化函数，打开相关文件并初始化写入
void init(char *name)
{
    char timestring[100]={0};
    // 定义CSV的头部字段
    const char *header_fields[] = {"帧编号", "帧id", "阶段", "PGN", "报文代号", "报文描述", "优先权", "源地址-目的地址", "帧长度", "帧数据", "帧数据含义"};
    const int num_header_fields = sizeof(header_fields) / sizeof(header_fields[0]);

    // 打开将要输出的目标csv文件
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    strftime(timestring, sizeof(timestring), "analysis-%Y-%m-%d_%H-%M-%S-%s.csv", local_time);
    snprintf(output_filename, sizeof(output_filename), "%s-%s",name ,timestring);
    output_file = fopen(output_filename, "w");
    errorjudge(output_file, output_filename);

    // 写csv文件头
    for (int i = 0; i < num_header_fields; ++i)
    {
        fprintf(output_file, "%s%c", header_fields[i], (i < num_header_fields - 1) ? ',' : '\n');
    }

    // 打开PGN.json文件并读取json信息
    FILE *pgn_file = fopen("PGN.json", "r");
    errorjudge(pgn_file, "PGN.json");
    fseek(pgn_file, 0, SEEK_END);
    long file_size = ftell(pgn_file);
    fseek(pgn_file, 0, SEEK_SET);
    char *file_content = (char *)malloc(file_size + 1);
    if (file_content == NULL)
    {
        perror("Error allocating memory for file content");
        exit(MALLOC_ERROR);
    }

    size_t result = fread(file_content, 1, file_size, pgn_file);
    if (result != (size_t)file_size)
    {
        perror("Error reading file");
        exit(FREAD_ERROR);
    }

    // 解析JSON
    pgn_json = cJSON_Parse(file_content);
    if (pgn_json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        fprintf(stderr, "Error parsing PGN.json: %s\n", error_ptr ? error_ptr : "Unknown error");
        exit(JSONPARSE_ERROR);
    }

    free(file_content);
    fclose(pgn_file);
}

void deinit()
{
    // 清理资源
    cJSON_Delete(pgn_json);
    fclose(output_file);
}
