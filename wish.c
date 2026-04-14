/* Habilitar extensiones GNU/POSIX: necesario para strdup, getline, strsep */
#define _GNU_SOURCE

/*
 * wish.c — Wisconsin Shell
 * =============================================================================
 * Laboratorio 2 · Sistemas Operativos · Universidad de Antioquia · 2026-1
 *
 * Equipo:
 *   - [Nombre completo 1] <correo@udea.edu.co>
 *   - [Nombre completo 2] <correo@udea.edu.co>
 *
 * Descripción:
 *   Implementación de un intérprete de comandos Unix básico que soporta:
 *     - Ejecución de programas externos con resolución de PATH
 *     - Comandos built-in: exit, chd, route
 *     - Redirección de stdout y stderr con '>'
 *     - Ejecución paralela de comandos con '&'
 *     - Modo interactivo y modo batch
 * =============================================================================
 */

/* ─── Includes necesarios ──────────────────────────────────────────────────── */

#include <stdio.h>      /* printf, fprintf, getline, fopen, fclose          */
#include <stdlib.h>     /* exit, malloc, free, realloc                       */
#include <string.h>     /* strlen, strcmp, strcpy, strdup, strsep            */
#include <unistd.h>     /* fork, execv, access, chdir, dup2, close, write   */
#include <sys/types.h>  /* pid_t                                             */
#include <sys/wait.h>   /* wait, waitpid                                     */
#include <fcntl.h>      /* open, O_WRONLY, O_CREAT, O_TRUNC                 */
#include <errno.h>      /* errno                                             */

/* ─── Constantes ────────────────────────────────────────────────────────────── */

#define PROMPT          "wish> "
#define MAX_PATH_DIRS   64      /* Máximo número de directorios en el PATH   */
#define MAX_PATH_LEN    256     /* Longitud máxima de una ruta de archivo     */

/* Mensaje de error universal — SIEMPRE usar print_error(), nunca hardcodear */
static const char *ERROR_MSG = "An error has occurred\n";

/* ─── Estado global del shell ───────────────────────────────────────────────── */

/*
 * path[] — Lista de directorios donde buscar ejecutables.
 * Inicialmente contiene solo "/bin".
 * Se modifica con el built-in 'route'.
 *
 * path_count — Número de entradas válidas en path[].
 */
static char *path[MAX_PATH_DIRS];
static int   path_count = 0;

/* ─── Prototipos de funciones ───────────────────────────────────────────────── */

/* Utilidades generales */
void  print_error(void);
void  init_path(void);
void  free_path(void);

/* Bucle principal */
void  run_shell(FILE *input, int interactive);

/* Parsing */
int   split_parallel(char *line, char **commands[], int *count);
int   parse_command(char *cmd_str, char ***argv_out, int *argc_out,
                    char **outfile);
void  free_argv(char **argv, int argc);
void  trim_whitespace(char *str);

/* Búsqueda en PATH */
int   find_executable(const char *cmd, char *full_path, size_t path_size);

/* Ejecución */
void  execute_single(char **argv, int argc, const char *outfile);
void  execute_parallel(char **cmd_strings, int count);

/* Redirección */
int   setup_redirection(const char *outfile);

/* Built-ins */
int   handle_builtin(char **argv, int argc);
void  builtin_exit(char **argv, int argc);
void  builtin_chd(char **argv, int argc);
void  builtin_route(char **argv, int argc);

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: main
 * ─────────────────────────────────────────────────────────────────────────────
 * Punto de entrada del shell. Valida los argumentos de línea de comandos y
 * determina el modo de operación (interactivo vs batch).
 *
 * Reglas de invocación:
 *   - ./wish          → modo interactivo, lee de stdin
 *   - ./wish file.txt → modo batch, lee del archivo
 *   - ./wish a b ...  → error fatal → exit(1)
 * ═══════════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {
    if (argc > 2) {
        print_error();
        exit(1);
    }

    init_path();

    FILE *input       = NULL;
    int   interactive = 0;

    if (argc == 1) {
        /* Modo interactivo — leer de stdin */
        input       = stdin;
        interactive = 1;
    } else {
        /* Modo batch — abrir el archivo */
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            exit(1);
        }
        interactive = 0;
    }

    run_shell(input, interactive);

    /* Limpiar recursos (run_shell llama exit(), estas líneas son por completitud) */
    if (input != NULL && input != stdin) {
        fclose(input);
    }
    free_path();
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: print_error
 * ─────────────────────────────────────────────────────────────────────────────
 * Imprime el mensaje de error estándar en STDERR_FILENO.
 * Esta es la ÚNICA manera de reportar errores en wish.
 * NUNCA usar printf, fprintf(stderr,...) o perror() para errores del shell.
 * ═══════════════════════════════════════════════════════════════════════════════ */
void print_error(void) {
    write(STDERR_FILENO, ERROR_MSG, strlen(ERROR_MSG));
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: init_path
 * ─────────────────────────────────────────────────────────────────────────────
 * Inicializa el PATH global con un único directorio: "/bin".
 * Llamar al inicio de main().
 * ═══════════════════════════════════════════════════════════════════════════════ */
void init_path(void) {
    path[0]    = strdup("/bin");
    path_count = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: free_path
 * ─────────────────────────────────────────────────────────────────────────────
 * Libera la memoria de todos los strings en path[].
 * Llamar al terminar el shell para evitar memory leaks.
 * ═══════════════════════════════════════════════════════════════════════════════ */
void free_path(void) {
    for (int i = 0; i < path_count; i++) {
        free(path[i]);
        path[i] = NULL;
    }
    path_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: trim_whitespace
 * ─────────────────────────────────────────────────────────────────────────────
 * Elimina espacios y tabulaciones al inicio y al final del string str.
 * Modifica el string in-place.
 *
 * Parámetros:
 *   str — string a limpiar (modificado in-place)
 * ═══════════════════════════════════════════════════════════════════════════════ */
void trim_whitespace(char *str) {
    if (str == NULL) return;

    /* Eliminar espacios/tabs al inicio desplazando el contenido */
    int start = 0;
    while (str[start] == ' ' || str[start] == '\t') start++;
    if (start > 0) {
        int len = strlen(str);
        memmove(str, str + start, len - start + 1);
    }

    /* Eliminar espacios/tabs al final */
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: run_shell
 * ─────────────────────────────────────────────────────────────────────────────
 * Bucle principal del shell. Lee líneas del stream 'input', las parsea
 * y las ejecuta en un ciclo infinito hasta recibir EOF o ejecutar 'exit'.
 *
 * Parámetros:
 *   input       — stream de donde leer (stdin en interactivo, FILE* en batch)
 *   interactive — 1 si debe imprimir "wish> " antes de cada lectura, 0 si no
 * ═══════════════════════════════════════════════════════════════════════════════ */
void run_shell(FILE *input, int interactive) {
    char   *line    = NULL;  /* Buffer para getline (se aloca automáticamente) */
    size_t  bufsize = 0;     /* Tamaño del buffer (gestionado por getline)     */
    ssize_t nread;

    while (1) {
        /* Imprimir prompt solo en modo interactivo */
        if (interactive) {
            fprintf(stdout, "%s", PROMPT);
            fflush(stdout);
        }

        /* Leer una línea del stream */
        nread = getline(&line, &bufsize, input);

        /* EOF — salir limpiamente */
        if (nread == -1) {
            free(line);
            exit(0);
        }

        /* Eliminar el '\n' al final de la línea */
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        /* Saltar líneas vacías o solo con espacios/tabs */
        trim_whitespace(line);
        if (strlen(line) == 0) continue;

        /* Dividir por '&' para detectar comandos paralelos */
        char **commands = NULL;
        int    cmd_count = 0;

        if (split_parallel(line, &commands, &cmd_count) == -1) {
            /* Error de aloc — ya se imprimió el error */
            continue;
        }

        if (cmd_count == 1) {
            /* ── Comando único ── */
            trim_whitespace(commands[0]);

            if (strlen(commands[0]) > 0) {
                char **argv   = NULL;
                int    argc   = 0;
                char  *outfile = NULL;

                if (parse_command(commands[0], &argv, &argc, &outfile) == 0) {
                    if (argc == 0) {
                        /* Segmento vacío tras parsear — ignorar */
                    } else if (!handle_builtin(argv, argc)) {
                        /* No es built-in: ejecutar como externo */
                        execute_single(argv, argc, outfile);
                    }
                    free_argv(argv, argc);
                    if (outfile) free(outfile);
                }
                /* Si parse_command retornó -1, el error ya fue impreso */
            }
        } else {
            /* ── Comandos paralelos (hay más de un segmento) ── */
            execute_parallel(commands, cmd_count);
        }

        /* Liberar el arreglo de comandos */
        for (int i = 0; i < cmd_count; i++) free(commands[i]);
        free(commands);
    }

    free(line); /* inalcanzable, pero buena práctica */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: split_parallel
 * ─────────────────────────────────────────────────────────────────────────────
 * Divide la línea de entrada por el carácter '&' para obtener los comandos
 * que deben ejecutarse en paralelo.
 *
 * Parámetros:
 *   line     — la línea de entrada (se COPIA internamente, no se modifica)
 *   commands — salida: arreglo de strings, uno por comando (alocado aquí)
 *   count    — salida: número de comandos encontrados
 *
 * Retorna:
 *   0 si éxito, -1 si error de aloc
 *
 * Nota: El llamador es responsable de liberar commands[] y cada string.
 * ═══════════════════════════════════════════════════════════════════════════════ */
int split_parallel(char *line, char **commands[], int *count) {
    *count    = 0;
    *commands = NULL;

    /* Copiar la línea para no modificar el original */
    char *copy = strdup(line);
    if (!copy) {
        print_error();
        return -1;
    }

    char **result = NULL;
    int    n      = 0;
    char  *cursor = copy;
    char  *token;

    /* Dividir por '&', guardando cada segmento */
    while ((token = strsep(&cursor, "&")) != NULL) {
        char **tmp = realloc(result, (n + 1) * sizeof(char *));
        if (!tmp) {
            for (int i = 0; i < n; i++) free(result[i]);
            free(result);
            free(copy);
            print_error();
            return -1;
        }
        result = tmp;
        result[n] = strdup(token);
        if (!result[n]) {
            for (int i = 0; i < n; i++) free(result[i]);
            free(result);
            free(copy);
            print_error();
            return -1;
        }
        n++;
    }

    free(copy);
    *commands = result;
    *count    = n;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: parse_command
 * ─────────────────────────────────────────────────────────────────────────────
 * Parsea un string de comando en un arreglo de argumentos (argv[]) y detecta
 * la redirección de salida ('>').
 *
 * Parámetros:
 *   cmd_str  — string del comando a parsear (e.g. "ls -la > out.txt")
 *   argv_out — salida: arreglo argv[] terminado en NULL
 *   argc_out — salida: número de argumentos (sin contar el NULL final)
 *   outfile  — salida: nombre del archivo de redirección, o NULL si no hay
 *
 * Retorna:
 *   0 si éxito
 *  -1 si error de sintaxis (>1 redirector, sin archivo, tokens extra después)
 * ═══════════════════════════════════════════════════════════════════════════════ */
int parse_command(char *cmd_str, char ***argv_out, int *argc_out,
                  char **outfile) {
    *argv_out = NULL;
    *argc_out = 0;
    *outfile  = NULL;

    /* Copiar el string para no modificar el original */
    char *copy = strdup(cmd_str);
    if (!copy) {
        print_error();
        return -1;
    }

    char **argv          = NULL;
    int    argc          = 0;
    char  *found_outfile = NULL;
    int    redirect_seen = 0;  /* ¿Ya se vio un '>'? */
    int    after_redirect = 0; /* 0=recolectando argv, 1=esperando outfile, 2=ya terminó */
    int    error         = 0;

    char *cursor = copy;
    char *token;

    while ((token = strsep(&cursor, " \t")) != NULL) {
        if (strlen(token) == 0) continue; /* ignorar tokens vacíos (espacios dobles) */

        if (after_redirect == 1) {
            /* Esperamos el nombre del archivo de salida */
            if (strcmp(token, ">") == 0) {
                /* Encontramos otro '>' en lugar del nombre → error */
                error = 1;
                break;
            }
            found_outfile = strdup(token);
            if (!found_outfile) { error = 1; break; }
            after_redirect = 2;

        } else if (after_redirect == 2) {
            /* Ya tenemos el outfile — cualquier token extra es error */
            error = 1;
            break;

        } else {
            /* Recolectando argumentos normales */
            if (strcmp(token, ">") == 0) {
                if (redirect_seen) {
                    /* Segundo '>' → error */
                    error = 1;
                    break;
                }
                redirect_seen = 1;
                after_redirect = 1;
            } else {
                /* Argumento normal: agregar a argv */
                char **tmp = realloc(argv, (argc + 2) * sizeof(char *));
                if (!tmp) { error = 1; break; }
                argv = tmp;
                argv[argc] = strdup(token);
                if (!argv[argc]) { error = 1; break; }
                argc++;
                argv[argc] = NULL; /* mantener siempre NULL-terminado */
            }
        }
    }

    /* Caso: "ls > " — hubo '>' pero no llegó el nombre del archivo */
    if (!error && redirect_seen && found_outfile == NULL) {
        error = 1;
    }

    free(copy);

    if (error) {
        if (argv) {
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
        }
        if (found_outfile) free(found_outfile);
        print_error();
        return -1;
    }

    /* Si no hubo argumentos, alocar un arreglo con solo NULL */
    if (argv == NULL) {
        argv = malloc(sizeof(char *));
        if (argv) argv[0] = NULL;
    }

    *argv_out = argv;
    *argc_out = argc;
    *outfile  = found_outfile;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: free_argv
 * ─────────────────────────────────────────────────────────────────────────────
 * Libera la memoria de un arreglo argv[] alocado por parse_command().
 *
 * Parámetros:
 *   argv — arreglo de strings a liberar (terminado en NULL)
 *   argc — número de elementos válidos (sin contar el NULL)
 * ═══════════════════════════════════════════════════════════════════════════════ */
void free_argv(char **argv, int argc) {
    if (argv == NULL) return;
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: find_executable
 * ─────────────────────────────────────────────────────────────────────────────
 * Busca el ejecutable 'cmd' en cada directorio del PATH global.
 * Si lo encuentra, copia la ruta absoluta en full_path.
 *
 * Parámetros:
 *   cmd       — nombre del comando a buscar (e.g. "ls")
 *   full_path — buffer de salida donde escribir la ruta (e.g. "/bin/ls")
 *   path_size — tamaño del buffer full_path
 *
 * Retorna:
 *   0 si encontró el ejecutable (full_path contiene la ruta)
 *  -1 si no se encontró en ningún directorio del PATH
 * ═══════════════════════════════════════════════════════════════════════════════ */
int find_executable(const char *cmd, char *full_path, size_t path_size) {
    /* Caso especial: ruta absoluta (empieza con '/') — usar directamente */
    if (cmd[0] == '/') {
        if (access(cmd, X_OK) == 0) {
            strncpy(full_path, cmd, path_size - 1);
            full_path[path_size - 1] = '\0';
            return 0;
        }
        return -1;
    }

    /* Buscar en cada directorio del PATH */
    for (int i = 0; i < path_count; i++) {
        snprintf(full_path, path_size, "%s/%s", path[i], cmd);
        if (access(full_path, X_OK) == 0) {
            return 0; /* encontrado */
        }
    }

    return -1; /* no encontrado en ningún directorio */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: setup_redirection
 * ─────────────────────────────────────────────────────────────────────────────
 * Abre el archivo de salida y redirige stdout (fd 1) Y stderr (fd 2) a él.
 * Llamar esta función en el proceso HIJO, después de fork() y antes de execv().
 *
 * Parámetros:
 *   outfile — ruta del archivo donde redirigir la salida
 *
 * Retorna:
 *   0 si éxito
 *  -1 si open() o dup2() fallan
 * ═══════════════════════════════════════════════════════════════════════════════ */
int setup_redirection(const char *outfile) {
    /* Abrir (o crear/truncar) el archivo de salida */
    int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        print_error();
        return -1;
    }

    /* Redirigir stdout → archivo */
    if (dup2(fd, STDOUT_FILENO) < 0) {
        print_error();
        close(fd);
        return -1;
    }

    /* Redirigir stderr → archivo (wish redirige AMBOS streams) */
    if (dup2(fd, STDERR_FILENO) < 0) {
        print_error();
        close(fd);
        return -1;
    }

    /* Cerrar el fd original — ya no lo necesitamos (stdout/stderr apuntan al archivo) */
    close(fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: execute_single
 * ─────────────────────────────────────────────────────────────────────────────
 * Ejecuta un único comando externo:
 *   1. Busca el ejecutable en PATH
 *   2. Hace fork()
 *   3. En el hijo: configura redirección (si aplica) y llama execv()
 *   4. En el padre: espera con waitpid()
 *
 * Parámetros:
 *   argv    — arreglo de argumentos terminado en NULL; argv[0] es el comando
 *   argc    — número de argumentos (sin NULL)
 *   outfile — archivo de salida (NULL si no hay redirección)
 * ═══════════════════════════════════════════════════════════════════════════════ */
void execute_single(char **argv, int argc, const char *outfile) {
    char full_path[MAX_PATH_LEN];
    (void)argc;

    /* Buscar el ejecutable en PATH */
    if (find_executable(argv[0], full_path, sizeof(full_path)) == -1) {
        print_error();
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        /* fork() falló */
        print_error();
        return;
    }

    if (pid == 0) {
        /* ── PROCESO HIJO ── */
        if (outfile != NULL) {
            if (setup_redirection(outfile) == -1) {
                exit(1); /* error ya fue impreso en setup_redirection */
            }
        }
        execv(full_path, argv);
        /* Si llegamos aquí, execv falló */
        print_error();
        exit(1);

    } else {
        /* ── PROCESO PADRE ── */
        waitpid(pid, NULL, 0);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: execute_parallel
 * ─────────────────────────────────────────────────────────────────────────────
 * Ejecuta múltiples comandos en paralelo:
 *   1. Para cada comando: parsear y hacer fork() SIN wait() inmediato
 *   2. Guardar todos los PIDs en un arreglo
 *   3. Al final, waitpid() para TODOS los hijos
 *
 * Parámetros:
 *   cmd_strings — arreglo de strings de comando (uno por segmento de '&')
 *   count       — número de segmentos
 * ═══════════════════════════════════════════════════════════════════════════════ */
void execute_parallel(char **cmd_strings, int count) {
    pid_t *pids = malloc(count * sizeof(pid_t));
    if (pids == NULL) {
        print_error();
        return;
    }
    int pid_count = 0;

    /* ── Fase 1: lanzar todos los procesos ── */
    for (int i = 0; i < count; i++) {
        trim_whitespace(cmd_strings[i]);
        if (strlen(cmd_strings[i]) == 0) continue; /* segmento vacío → ignorar */

        char **argv   = NULL;
        int    argc   = 0;
        char  *outfile = NULL;

        if (parse_command(cmd_strings[i], &argv, &argc, &outfile) == -1) {
            /* error ya impreso */
            continue;
        }

        if (argc == 0) {
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

        /* Los built-ins se ejecutan directamente en el shell (sin fork) */
        if (handle_builtin(argv, argc)) {
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

        /* Comando externo: buscar y lanzar */
        char full_path[MAX_PATH_LEN];
        if (find_executable(argv[0], full_path, sizeof(full_path)) == -1) {
            print_error();
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            print_error();
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

        if (pid == 0) {
            /* ── PROCESO HIJO ── */
            if (outfile != NULL) {
                if (setup_redirection(outfile) == -1) {
                    exit(1);
                }
            }
            execv(full_path, argv);
            print_error();
            exit(1);
        }

        /* ── PROCESO PADRE: guardar PID, no esperar todavía ── */
        pids[pid_count++] = pid;

        free_argv(argv, argc);
        if (outfile) free(outfile);
    }

    /* ── Fase 2: esperar a TODOS los hijos ── */
    for (int i = 0; i < pid_count; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: handle_builtin
 * ─────────────────────────────────────────────────────────────────────────────
 * Detecta si argv[0] es un built-in y lo ejecuta.
 *
 * Parámetros:
 *   argv — arreglo de argumentos (argv[0] = nombre del comando)
 *   argc — número de argumentos
 *
 * Retorna:
 *   1 si argv[0] era un built-in (se ejecutó o dio error)
 *   0 si argv[0] NO es un built-in (el llamador debe tratarlo como externo)
 * ═══════════════════════════════════════════════════════════════════════════════ */
int handle_builtin(char **argv, int argc) {
    if (argv == NULL || argv[0] == NULL) return 0;

    if (strcmp(argv[0], "exit") == 0) {
        builtin_exit(argv, argc);
        return 1;
    }
    if (strcmp(argv[0], "chd") == 0) {
        builtin_chd(argv, argc);
        return 1;
    }
    if (strcmp(argv[0], "route") == 0) {
        builtin_route(argv, argc);
        return 1;
    }

    return 0; /* no es un built-in */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: builtin_exit
 * ─────────────────────────────────────────────────────────────────────────────
 * Implementación del built-in 'exit'.
 * Termina el shell con exit(0) si no hay argumentos.
 * Imprime error si se pasan argumentos.
 *
 * Especificación:
 *   exit       → exit(0)    ✓
 *   exit 0     → error      ✗
 *   exit algo  → error      ✗
 * ═══════════════════════════════════════════════════════════════════════════════ */
void builtin_exit(char **argv, int argc) {
    (void)argv;
    /* argc=1 significa solo "exit" (el nombre del comando cuenta como arg) */
    if (argc != 1) {
        print_error();
        return;
    }
    exit(0);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: builtin_chd
 * ─────────────────────────────────────────────────────────────────────────────
 * Implementación del built-in 'chd' (change directory).
 * Cambia el directorio de trabajo del SHELL MISMO usando chdir().
 *
 * Especificación:
 *   chd         → error (sin argumentos)
 *   chd /tmp    → chdir("/tmp")
 *   chd /a /b   → error (más de 1 argumento)
 *   chd /noexis → error (chdir falla)
 *
 * Nota: DEBE ejecutarse en el proceso del shell (no en un hijo),
 *       porque chdir() solo afecta al proceso que lo llama.
 * ═══════════════════════════════════════════════════════════════════════════════ */
void builtin_chd(char **argv, int argc) {
    /* argc=2 significa "chd <directorio>" — exactamente 1 argumento */
    if (argc != 2) {
        print_error();
        return;
    }
    if (chdir(argv[1]) == -1) {
        print_error();
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FUNCIÓN: builtin_route
 * ─────────────────────────────────────────────────────────────────────────────
 * Implementación del built-in 'route'.
 * Sobreescribe completamente el PATH global con los directorios dados.
 *
 * Especificación:
 *   route              → PATH = [] (vacío, ningún externo funcionará)
 *   route /bin         → PATH = ["/bin"]
 *   route /bin /usr/bin → PATH = ["/bin", "/usr/bin"]
 *
 * Comportamiento:
 *   - REEMPLAZA (no agrega) el PATH completo
 *   - Libera las entradas anteriores del PATH
 *   - Copia los nuevos directorios con strdup
 *   - Nunca falla (incluso con dirs inválidos — la validación ocurre al buscar)
 * ═══════════════════════════════════════════════════════════════════════════════ */
void builtin_route(char **argv, int argc) {
    /* Liberar el PATH actual */
    free_path();

    /* Cargar los nuevos directorios (argv[1], argv[2], ...) */
    for (int i = 1; i < argc; i++) {
        if (path_count >= MAX_PATH_DIRS) break; /* límite de seguridad */
        path[path_count] = strdup(argv[i]);
        if (path[path_count]) path_count++;
    }
}
