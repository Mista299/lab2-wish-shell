#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void print_highlight(char *line, char *search) {
    char *pos = line;
    char *found;

    while ((found = strstr(pos, search)) != NULL) {
        printf("%.*s", (int)(found - pos), pos);

        printf("\033[31m%s\033[0m", search);

        pos = found + strlen(search);
    }

    printf("%s", pos);
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("wgrep: searchterm [file ...]\n");
        exit(1);
    }

    char *search = argv[1];

    if (argc == 2) {

        char *line = NULL;
        size_t len = 0;

        while (getline(&line, &len, stdin) != -1) {
            if (strstr(line, search) != NULL) {
                printf("%s", line);
            }
        }

        free(line);
    }

    else {

        for (int i = 2; i < argc; i++) {

            FILE *fp = fopen(argv[i], "r");

            if (fp == NULL) {
                printf("wgrep: cannot open file\n");
                exit(1);
            }

            char *line = NULL;
            size_t len = 0;

            while (getline(&line, &len, fp) != -1) {
                if (strstr(line, search) != NULL) {
                    print_highlight(line, search);
                }
            }

            free(line);
            fclose(fp);
        }
    }

    return 0;
}