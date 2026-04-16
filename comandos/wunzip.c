#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { // Validar que haya al menos un archivo
        printf("wunzip: file1 [file2 ...]\n");
        exit(1);
    }

    // Recorrer cada archivo pasado por argumentos
    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        if (fp == NULL) {
            printf("wunzip: cannot open file\n");
            exit(1);
        }

        int count;
        char c;

        // Leer bloques de 5 bytes: [int][char]
        while (fread(&count, sizeof(int), 1, fp) == 1 &&
       fread(&c, sizeof(char), 1, fp) == 1) {

            // Imprimir el caracter 'count' veces
            for (int j = 0; j < count; j++) {
                printf("%c", c);
            }
        }

        fclose(fp);
    }

    return 0;
}