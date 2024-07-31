#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <json-c/json.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

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

        if (strcmp(process_mode, "select") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0) {
                    // Reverse bytes and convert to uppercase hex
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
                // Handle non-integer start_byte_or_bit
                // Similar logic as above but with bit manipulation
            }
        } else if (strcmp(process_mode, "ascii") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

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
        } else if (strcmp(process_mode, "calculate") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0) {
                    // Reverse bytes and convert to uppercase hex
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
                // Handle non-integer start_byte_or_bit
                // Similar logic as above but with bit manipulation
            }
        } else if (strcmp(process_mode, "date") == 0) {
            struct json_object *start_byte_or_bit_obj = json_object_object_get(sl, "起始字节或位");
            int start_byte_or_bit = json_object_get_int(start_byte_or_bit_obj);
            int length = json_object_get_int(json_object_object_get(sl, "长度"));

            if (json_object_is_type(start_byte_or_bit_obj, json_type_int)) {
                char d[256];
                strncpy(d, cd + (start_byte_or_bit - 1) * 2, (start_byte_or_bit + length - 1) * 2);
                d[(start_byte_or_bit + length - 1) * 2] = '\0';

                if (strlen(d) > 0) {
                    if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2571") == 0) {
                        int year = strtol(d, NULL, 16) + 1985;
                        int month = strtol(d + 2, NULL, 16);
                        int day = strtol(d + 4, NULL, 16);
                        char date_str[128];
                        snprintf(date_str, sizeof(date_str), "%d年%d月%d日", year, month, day);
                        strcat(c, json_object_get_string(json_object_object_get(sl, "content")));
                        strcat(c, date_str);
                        strcat(c, "；");
                    } else if (strcmp(json_object_get_string(json_object_object_get(sl, "SPN")), "2576") == 0) {
                        // Handle other date formats
                    }
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


