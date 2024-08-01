#include "gbt27930-2015.h"

// 定义CAN数据结构
typedef struct
{
    char can_id[9];    // CAN ID
    char can_data[17]; // CAN数据，最多8个字节，以16进制字符串表示，每个字节占2个字符,加上终止符'\0'
} CANInfo;

char charging_addr[] = "56";
char charging_name[] = "充电机";
char bms_addr[] = "F4";
char bms_name[] = "BMS";
char error_addr[] = "???";
char pgn[] = "000000";
char mul_pgn[] = "000000";
char info[256] = {0};
char muldata[256] = {0};
const char *mul_frame_id[] = {"1CEC56F4", "1CECF456", "1CEB56F4", "1CECF456"};
char data_parse[1024] = {0};
char line_input[1024] = {0};
char line_output[1024] = {0};
CANInfo caninfo;
int line_num = 0;
char output_filename[256];
FILE *output_file;
cJSON *pgn_json;

typedef enum {
    UNKNOWN = 0,  // 非法PGN报文

    // 低压辅助上电及充电握手阶段
    CHM = 1,      // 充电机握手报文
    BHM = 2,      // 车辆握手报文
    CRM = 3,      // 充电机辨识报文
    BRM = 4,      // BMS和车辆辨识报文

    // 充电参数配置阶段
    BCP = 5,      // 动力蓄电池充电参数报文
    CTS = 6,      // 充电机发送时间同步信息报文
    CML = 7,      // 充电机最大输出能力报文
    BRO = 8,      // 车辆充电准备就绪状态报文
    CRO = 9,      // 充电机输出准备就绪状态报文

    // 充电阶段
    BCL = 10,     // 电池充电需求报文
    BCS = 11,     // 电池充电总状态报文
    CCS = 12,     // 充电机充电状态报文
    BSM = 13,     // 动力蓄电池状态信息报文
    BMV = 14,     // 单体动力蓄电池电压报文
    BMT = 15,     // 动力蓄电池温度报文
    BSP = 16,     // 动力蓄电池预留报文
    BST = 17,     // 车辆中止充电报文
    CST = 18,     // 充电机中止充电报文

    // 充电结束阶段
    BSD = 19,     // 车辆统计数据报文
    CSD = 20,     // 充电机统计数据报文

    // 错误报文
    BEM = 21,     // BMS及车辆错误报文
    CEM = 22,     // 充电机错误报文

    MULPRE=23     //多帧的开头和中间部分
} PGNType;


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
        fprintf(stderr, "Conversion failed: invalid hex string.\n");
        exit(1);
    }
    int size = snprintf(NULL, 0, "%ld", decimal) + 1;
    snprintf(pgn, size, "%ld", decimal);
    pgn[size] = '\0';

    return 0;
}

// 提取can_id和can_data
static CANInfo format_data(const char *line)
{
    CANInfo result;
    const char *p_id = strrchr(line, ' ');  // 查找最后一个空格
    const char *p_data = strchr(line, '#'); // 查找'#'字符

    if (p_id && p_data && p_data > p_id)
    {
        // 提取CAN ID
        int cid_len = p_data - p_id - 1;
        strncpy(result.can_id, p_id + 1, cid_len);
        result.can_id[cid_len] = '\0'; // 添加字符串终止符

        // 提取CAN数据
        int cdata_len = strlen(line) - (p_data - line + 1);
        strncpy(result.can_data, p_data + 1, cdata_len);
        result.can_data[cdata_len] = '\0'; // 添加字符串终止符
    }
    else
    {
        strcpy(result.can_id, "");
        strcpy(result.can_data, "");
    }

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

//返回某键值在json对象中的序号，如果没有，则返回-1
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

    return -1;
}

// can报文解析函数，将解析完的待输出数据存储在全局数组outline中;can_input为输入的can报文数据，pgn_json为PGN的json信息
int can_parse(char *can_input, cJSON *pgn_json)
{
    line_num++;
    PGNType pgn_type = 0;

    // 去除每行末尾的换行符
    can_input[strcspn(can_input, "\n")] = 0;

    caninfo = format_data(can_input);
    int can_data_len = ((strlen(caninfo.can_data)) / 2); // 获取字符串长度
    get_pgn(caninfo.can_id);
    char *dest = dest_addr(&caninfo.can_id[4]);
    char *source = source_addr(&caninfo.can_id[6]);
    memset(line_output, 0, sizeof(line_output));
    char *ptr = line_output;
    char *mulptr = muldata, *mulpgn = mul_pgn;

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
            pgn_type=MULPRE;
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
            pgn_type=get_key_index(pgn_json, pgn);
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

char *pgn_content(const char *pgn, const char *can_data)
{
    memset(data_parse, 0, sizeof(data_parse));
    FILE *f = fopen("SPN.json", "r");
    if (!f)
    {
        perror("Failed to open file");
        return NULL;
    }

    struct json_object *spn_json;
    struct json_object *parsed_json = json_object_from_file("SPN.json");
    if (!parsed_json)
    {
        perror("Failed to parse JSON");
        fclose(f);
        return NULL;
    }

    spn_json = json_object_object_get(parsed_json, pgn);
    if (!spn_json)
    {
        perror("Failed to get JSON object");
        fclose(f);
        return NULL;
    }

    int arraylen = json_object_array_length(spn_json);
    for (int i = 0; i < arraylen; i++)
    {
        struct json_object *sl = json_object_array_get_idx(spn_json, i);
        const char *process_mode = json_object_get_string(json_object_object_get(sl, "process_mode"));

        if (strcmp(process_mode, "select") == 0)
        {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int))
            {
                char d[256];
                strncpy(d, can_data + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0)
                {
                    // Reverse bytes and convert to uppercase hex
                    for (int j = 0; j < strlen(d) / 2; j += 2)
                    {
                        char temp = d[j];
                        d[j] = d[strlen(d) - j - 2];
                        d[strlen(d) - j - 2] = temp;
                        temp = d[j + 1];
                        d[j + 1] = d[strlen(d) - j - 1];
                        d[strlen(d) - j - 1] = temp;
                    }
                    for (int j = 0; j < strlen(d); j++)
                    {
                        d[j] = toupper(d[j]);
                    }

                    struct json_object *content = json_object_object_get(sl, "content");
                    struct json_object *definition_data = json_object_object_get(sl, "definition_data");
                    int index = -1;
                    for (int j = 0; j < json_object_array_length(definition_data); j++)
                    {
                        if (strcmp(json_object_get_string(json_object_array_get_idx(definition_data, j)), d) == 0)
                        {
                            index = j;
                            break;
                        }
                    }
                    if (index != -1)
                    {
                        strcat(data_parse, json_object_get_string(json_object_array_get_idx(content, index)));
                        strcat(data_parse, "；");
                    }
                }
            }
            else
            {
                // Handle non-integer start_byte_or_bit
                // Similar logic as above but with bit manipulation
            }
        }
        else if (strcmp(process_mode, "ascii") == 0)
        {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int))
            {
                char d[256];
                strncpy(d, can_data + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0)
                {
                    char ascii_string[128] = "";
                    for (int j = 0; j < strlen(d); j += 2)
                    {
                        char hex_byte[3] = {d[j], d[j + 1], '\0'};
                        char ascii_char = (char)strtol(hex_byte, NULL, 16);
                        strncat(ascii_string, &ascii_char, 1);
                    }
                    strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                    strcat(data_parse, ascii_string);
                    strcat(data_parse, "；");
                }
            }
        }
        else if (strcmp(process_mode, "calculate") == 0)
        {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int))
            {
                char d[256];
                strncpy(d, can_data + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0)
                {
                    // Reverse bytes and convert to uppercase hex
                    for (int j = 0; j < strlen(d) / 2; j += 2)
                    {
                        char temp = d[j];
                        d[j] = d[strlen(d) - j - 2];
                        d[strlen(d) - j - 2] = temp;
                        temp = d[j + 1];
                        d[j + 1] = d[strlen(d) - j - 1];
                        d[strlen(d) - j - 1] = temp;
                    }
                    for (int j = 0; j < strlen(d); j++)
                    {
                        d[j] = toupper(d[j]);
                    }

                    double data_resolution = json_object_get_double(json_object_object_get(sl, "data_resolution"));
                    double offset = json_object_get_double(json_object_object_get(sl, "offset"));
                    double result = data_resolution * strtol(d, NULL, 16) + offset;
                    if (result < 0)
                    {
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
            else
            {
                // Handle non-integer start_byte_or_bit
                // Similar logic as above but with bit manipulation
            }
        }
        else if (strcmp(process_mode, "date") == 0)
        {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int))
            {
                char d[256];
                strncpy(d, can_data + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0)
                {
                    if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2571") == 0)
                    {
                        int year = strtol(d, NULL, 16) + 1985;
                        int month = strtol(d + 2, NULL, 16);
                        int day = strtol(d + 4, NULL, 16);
                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(data_parse, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(data_parse, date_str);
                        strcat(data_parse, "；");
                    }
                    else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2576") == 0)
                    {
                        // Handle other date formats
                    }
                }
            }
        }
    }
    fclose(f);
    return data_parse;
}

// 初始化函数，打开相关文件并初始化写入
void init()
{
    // 定义CSV的头部字段
    const char *header_fields[] = {"帧编号", "帧id", "阶段", "PGN", "报文代号", "报文描述", "优先权", "源地址-目的地址", "帧长度", "帧数据", "帧数据含义"};
    const int num_header_fields = sizeof(header_fields) / sizeof(header_fields[0]);

    // 打开将要输出的目标csv文件
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    strftime(output_filename, sizeof(output_filename), "analysis-%Y-%m-%d_%H-%M-%S.csv", local_time);
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
