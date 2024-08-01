#include "gbt27930-2015.h"

extern char line_output[1024];
extern char line_input[1024];
extern FILE *output_file;
extern cJSON *pgn_json ;

int main(int argc,char *argv[]){
    init();

    FILE* input_file = fopen(argv[1], "r");
    if (!input_file) {
        perror("Error opening CAN log file");
        return 1;
    }

    while (fgets(line_input, sizeof(line_input), input_file)){
        can_parse(line_input,pgn_json);
        fprintf(output_file, "%s\n", line_output);
    }

    deinit();
}