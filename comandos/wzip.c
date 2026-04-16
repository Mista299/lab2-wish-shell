#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { // Validar que haya al menos un archivo
        printf("wzip: file1 [file2 ...]\n");
        exit(1);
    }

    char prev = 0;   // Caracter anterior
    int count = 0;   // Conteo de repeticiones
    int firstChar = 1; // Flag para el primer caracter

    // Recorrer cada archivo pasado por argumentos
    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if (fp == NULL) {
            printf("wzip: cannot open file\n");
            exit(1);
        }

        char c;
        while ((c = fgetc(fp)) != EOF) { // Leer char por char
            if (firstChar) { // Primer caracter que encontramos
                prev = c;
                count = 1;
                firstChar = 0;
            } else if (c == prev) {
                count++; // Mismo caracter, aumentar contador
            } else {
                // Guardar grupo anterior: [4 bytes int][1 byte char]
                fwrite(&count, sizeof(int), 1, stdout);
                fwrite(&prev, sizeof(char), 1, stdout);

                prev = c;  // Nuevo caracter
                count = 1; // Reiniciar conteo
            }
        }

        fclose(fp);
    }

    // Guardar el último grupo
    if (!firstChar) {
        fwrite(&count, sizeof(int), 1, stdout);
        fwrite(&prev, sizeof(char), 1, stdout);
    }

    return 0;
}