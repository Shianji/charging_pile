#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <json-c/json.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

// 反转字节数组
void reverse_bytes(unsigned char *byte_array, int byte_array_len) {
    for (int i = 0; i < byte_array_len / 2; i++) {
        unsigned char temp = byte_array[i];
        byte_array[i] = byte_array[byte_array_len - 1 - i];
        byte_array[byte_array_len - 1 - i] = temp;
    }
}

// 将十六进制字符串转换为整数
int hex_to_int(const char* hex) {
    int value;
    sscanf(hex, "%x", &value);
    return value;
}

// 将十六进制字符串转换为字节数组
unsigned char* hex_to_bytes(const char* hex) {
    size_t len = strlen(hex);
    unsigned char* bytes = (unsigned char*)malloc(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        sscanf(hex + i, "%2hhx", &bytes[i / 2]);
    }
    return bytes;
}

// 将十六进制字符转换为对应的二进制字符串
void hexToBin(char hex, char* bin) {
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
void hexStringToBinString(const char* hexStr, char* binStr) {
    char bin[256]; 
    binStr[0] = '\0'; 

    for (int i = 0; i < strlen(hexStr); i++) {
        hexToBin(hexStr[i], bin);
        strcat(binStr, bin);
    }
}

// 将二进制字符串左边补0到指定长度
void padLeftWithZeros(char* binStr, int totalLength) {
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
int getDecimalPart(double number) {
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

void pgn_content(const char *p, const char *cd) {
    char c[1024] = "";
    FILE *f = fopen("SPN.json", "r");
    if (!f) {
        perror("Failed to open file");
        return;
    }

    struct json_object *spn_json;
    struct json_object *parsed_json = json_object_from_file("SPN.json");
    if (!parsed_json) {
        perror("Failed to parse JSON");
        fclose(f);
        return;
    }

    spn_json = json_object_object_get(parsed_json, p);
    if (!spn_json) {
        perror("Failed to get JSON object");
        fclose(f);
        return;
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

                //strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                //d[(start_byte_or_bit + length - 1) * 2] = '\0';

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
                        strcat(c, json_object_get_string(json_object_array_get_idx(content, index)));
                        strcat(c, "；");
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
                            strcat(c, json_object_get_string(json_object_array_get_idx(content, index)));
                            strcat(c, "；");
                        } else {
                            strcat(c, "解析出错标准无");
                            strcat(c, section);
                            strcat(c, "所代表的含义；");
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
                //strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                //d[(start_byte_or_bit + length - 1) * 2] = '\0';
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
                    strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(c, ascii_string);
                    strcat(c, "；");
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
                //printf("%d\n", num_chars_to_copy);
                //strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2 - 1);
                //d[(start_byte_or_bit + length - 1) * 2] = '\0';
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
                    strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(c, result_str);
                    strcat(c, json_object_get_string(json_object_object_get(sl, "units")));
                    strcat(c, "；");
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
                    
                    // strncpy(d, cd + start_index, end_index - start_index);
                    // d[end_index - start_index] = '\0';
                    int num_chars_to_copy = end_index - start_index;
                    strncpy(d, cd + start_index, num_chars_to_copy);
                    d[num_chars_to_copy] = '\0';
                    if (strlen(d) > 0) {
                        // 反转字节转为16进制
                        // for (int j = 0; j < strlen(d) / 2; j += 2) {
                        // char temp = d[j];
                        // d[j] = d[strlen(d) - j - 2];
                        // d[strlen(d) - j - 2] = temp;
                        // temp = d[j + 1];
                        // d[j + 1] = d[strlen(d) - j - 1];
                        // d[strlen(d) - j - 1] = temp;
                        // }

                        // for (int j = 0; j < strlen(d); j++) {
                        //     d[j] = toupper(d[j]);
                        // }
                        
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
                        strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(c, result_str);
                        strcat(c, json_object_get_string(json_object_object_get(sl, "units")));
                        strcat(c, "；");
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
                        int year = hex_to_int(year_hex) + 1985;
                        
                        // 提取月份
                        char month_hex[3];
                        strncpy(month_hex, d + 2, 2);
                        month_hex[2] = '\0';  
                        int month = hex_to_int(month_hex);

                        // 提取日期
                        char day_hex[3];
                        strncpy(day_hex, d + 4, 2);
                        day_hex[2] = '\0';  
                        int day = hex_to_int(day_hex);
                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(c, date_str);
                        strcat(c, "；");
                    } else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2576") == 0) {
                        // 提取第4到第7个字符（从索引3到索引7）
                        char hex_substr[5];
                        strncpy(hex_substr, d + 4, 4);
                        hex_substr[4] = '\0';
                        unsigned char* bytes = hex_to_bytes(hex_substr);    
                        reverse_bytes(bytes, 2);
                        char reversed_hex[5];
                        snprintf(reversed_hex, 5, "%02X%02X", bytes[0], bytes[1]);
                        int year = hex_to_int(reversed_hex);

                        // 提取年份
                        char month_hex[3];
                        strncpy(month_hex, d + 2, 2);
                        month_hex[2] = '\0';  
                        int month = hex_to_int(month_hex);
                        
                        // 提取日期
                        char day_hex[3];
                        strncpy(day_hex, d, 2);
                        day_hex[2] = '\0';  
                        int day = hex_to_int(day_hex);

                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(c, date_str);
                        strcat(c, "；");
                    } else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2823") == 0) {
                        // 提取第10到第13个字符（从索引9到索引13）
                        char hex_substr[5];
                        strncpy(hex_substr, d + 10, 4);
                        hex_substr[4] = '\0';
                        unsigned char* bytes = hex_to_bytes(hex_substr);    
                        reverse_bytes(bytes, 2);
                        char reversed_hex[5];
                        snprintf(reversed_hex, 5, "%02X%02X", bytes[0], bytes[1]);

                        int year = hex_to_int(reversed_hex);

                        char month_hex[3], day_hex[3], hour_hex[3], minute_hex[3], second_hex[3];

                        strncpy(month_hex, d + 8, 2);
                        month_hex[2] = '\0';
                        int month = hex_to_int(month_hex);

                        strncpy(day_hex, d + 6, 2);
                        day_hex[2] = '\0';
                        int day = hex_to_int(day_hex);

                        strncpy(hour_hex, d + 4, 2);
                        hour_hex[2] = '\0';
                        int hour = hex_to_int(hour_hex);

                        strncpy(minute_hex, d + 2, 2);
                        minute_hex[2] = '\0';
                        int minute = hex_to_int(minute_hex);

                        strncpy(second_hex, d, 2);
                        second_hex[2] = '\0';
                        int second = hex_to_int(second_hex);

                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日%d时%d分%d秒", year, month, day, hour, minute, second);
                        strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(c, date_str);
                        strcat(c, "；");
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
                    strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(c, "；");
                    strcat(c, "帧数据为");
                    strcat(c, d);
                    strcat(c, "；");
                }
            }
        }        
    }
    printf("%s\n", c);
    fclose(f);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <JSON key> <data string>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *json_key = argv[1];
    const char *data_string = argv[2];

    // 调用 pgn_content 函数处理数据
    pgn_content(json_key, data_string);
 
    return EXIT_SUCCESS;
}

