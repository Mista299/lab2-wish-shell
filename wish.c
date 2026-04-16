/* Necesario para strdup, getline, strsep en -std=c99 */
#define _GNU_SOURCE

/*
 * wish.c — Wisconsin Shell
 * Laboratorio 2 · Sistemas Operativos · Universidad de Antioquia · 2026-1
 *
 * Equipo:
 *   - [Nombre completo 1] <correo@udea.edu.co>
 *   - [Nombre completo 2] <correo@udea.edu.co>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define PROMPT        "wish> "
#define MAX_PATH_DIRS 64
#define MAX_PATH_LEN  256

static const char *ERROR_MSG = "An error has occurred\n";

static char *path[MAX_PATH_DIRS];
static int   path_count = 0;

void  print_error(void);
void  init_path(void);
void  free_path(void);
void  run_shell(FILE *input, int interactive);
int   split_parallel(char *line, char **commands[], int *count);
int   parse_command(char *cmd_str, char ***argv_out, int *argc_out, char **outfile);
void  free_argv(char **argv, int argc);
void  trim_whitespace(char *str);
int   find_executable(const char *cmd, char *full_path, size_t path_size);
void  execute_single(char **argv, int argc, const char *outfile);
void  execute_parallel(char **cmd_strings, int count);
int   setup_redirection(const char *outfile);
int   handle_builtin(char **argv, int argc);
void  builtin_exit(char **argv, int argc);
void  builtin_chd(char **argv, int argc);
void  builtin_route(char **argv, int argc);

int main(int argc, char *argv[]) {
    if (argc > 2) {
        print_error();
        exit(1);
    }

    init_path();

    FILE *input       = NULL;
    int   interactive = 0;

    if (argc == 1) {
        input       = stdin;
        interactive = 1;
    } else {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            exit(1);
        }
    }

    run_shell(input, interactive);

    if (input != NULL && input != stdin)
        fclose(input);
    free_path();
    return 0;
}

void print_error(void) {
    write(STDERR_FILENO, ERROR_MSG, strlen(ERROR_MSG));
}

void init_path(void) {
    path[0]    = strdup("/bin");
    path_count = 1;
}

void free_path(void) {
    for (int i = 0; i < path_count; i++) {
        free(path[i]);
        path[i] = NULL;
    }
    path_count = 0;
}

void trim_whitespace(char *str) {
    if (str == NULL) return;

    int start = 0;
    while (str[start] == ' ' || str[start] == '\t') start++;
    if (start > 0) {
        int len = strlen(str);
        memmove(str, str + start, len - start + 1);
    }

    int len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

void run_shell(FILE *input, int interactive) {
    char   *line    = NULL;
    size_t  bufsize = 0;
    ssize_t nread;

    while (1) {
        if (interactive) {
            fprintf(stdout, "%s", PROMPT);
            fflush(stdout);
        }

        nread = getline(&line, &bufsize, input);

        if (nread == -1) {
            free(line);
            exit(0);
        }

        if (nread > 0 && line[nread - 1] == '\n')
            line[nread - 1] = '\0';

        trim_whitespace(line);
        if (strlen(line) == 0) continue;

        char **commands  = NULL;
        int    cmd_count = 0;

        if (split_parallel(line, &commands, &cmd_count) == -1)
            continue;

        if (cmd_count == 1) {
            trim_whitespace(commands[0]);

            if (strlen(commands[0]) > 0) {
                char **argv    = NULL;
                int    argc    = 0;
                char  *outfile = NULL;

                if (parse_command(commands[0], &argv, &argc, &outfile) == 0) {
                    if (argc > 0 && !handle_builtin(argv, argc))
                        execute_single(argv, argc, outfile);
                    free_argv(argv, argc);
                    if (outfile) free(outfile);
                }
            }
        } else {
            execute_parallel(commands, cmd_count);
        }

        for (int i = 0; i < cmd_count; i++) free(commands[i]);
        free(commands);
    }

    free(line);
}

int split_parallel(char *line, char **commands[], int *count) {
    *count    = 0;
    *commands = NULL;

    char *copy = strdup(line);
    if (!copy) { print_error(); return -1; }

    char **result = NULL;
    int    n      = 0;
    char  *cursor = copy;
    char  *token;

    while ((token = strsep(&cursor, "&")) != NULL) {
        char **tmp = realloc(result, (n + 1) * sizeof(char *));
        if (!tmp) {
            for (int i = 0; i < n; i++) free(result[i]);
            free(result); free(copy);
            print_error(); return -1;
        }
        result = tmp;
        result[n] = strdup(token);
        if (!result[n]) {
            for (int i = 0; i < n; i++) free(result[i]);
            free(result); free(copy);
            print_error(); return -1;
        }
        n++;
    }

    free(copy);
    *commands = result;
    *count    = n;
    return 0;
}

int parse_command(char *cmd_str, char ***argv_out, int *argc_out, char **outfile) {
    *argv_out = NULL;
    *argc_out = 0;
    *outfile  = NULL;

    char *copy = strdup(cmd_str);
    if (!copy) { print_error(); return -1; }

    char **argv          = NULL;
    int    argc          = 0;
    char  *found_outfile = NULL;
    int    redirect_seen = 0;
    int    after_redirect = 0; /* 0=argv, 1=espera archivo, 2=listo */
    int    error         = 0;

    char *cursor = copy;
    char *token;

    while ((token = strsep(&cursor, " \t")) != NULL) {
        if (strlen(token) == 0) continue;

        if (after_redirect == 1) {
            if (strcmp(token, ">") == 0) { error = 1; break; }
            found_outfile  = strdup(token);
            if (!found_outfile) { error = 1; break; }
            after_redirect = 2;

        } else if (after_redirect == 2) {
            /* token extra después del archivo → error */
            error = 1; break;

        } else {
            if (strcmp(token, ">") == 0) {
                if (redirect_seen) { error = 1; break; }
                redirect_seen  = 1;
                after_redirect = 1;
            } else {
                char **tmp = realloc(argv, (argc + 2) * sizeof(char *));
                if (!tmp) { error = 1; break; }
                argv = tmp;
                argv[argc] = strdup(token);
                if (!argv[argc]) { error = 1; break; }
                argc++;
                argv[argc] = NULL;
            }
        }
    }

    /* '>' visto pero sin nombre de archivo */
    if (!error && redirect_seen && found_outfile == NULL)
        error = 1;

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

    if (argv == NULL) {
        argv = malloc(sizeof(char *));
        if (argv) argv[0] = NULL;
    }

    *argv_out = argv;
    *argc_out = argc;
    *outfile  = found_outfile;
    return 0;
}

void free_argv(char **argv, int argc) {
    if (argv == NULL) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

int find_executable(const char *cmd, char *full_path, size_t path_size) {
    /* Ruta absoluta: verificar directamente sin buscar en PATH */
    if (cmd[0] == '/') {
        if (access(cmd, X_OK) == 0) {
            strncpy(full_path, cmd, path_size - 1);
            full_path[path_size - 1] = '\0';
            return 0;
        }
        return -1;
    }

    for (int i = 0; i < path_count; i++) {
        snprintf(full_path, path_size, "%s/%s", path[i], cmd);
        if (access(full_path, X_OK) == 0)
            return 0;
    }

    return -1;
}

int setup_redirection(const char *outfile) {
    int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { print_error(); return -1; }

    if (dup2(fd, STDOUT_FILENO) < 0) { print_error(); close(fd); return -1; }

    /* wish redirige stdout Y stderr al mismo archivo */
    if (dup2(fd, STDERR_FILENO) < 0) { print_error(); close(fd); return -1; }

    close(fd);
    return 0;
}

void execute_single(char **argv, int argc, const char *outfile) {
    char full_path[MAX_PATH_LEN];
    (void)argc;

    if (find_executable(argv[0], full_path, sizeof(full_path)) == -1) {
        print_error();
        return;
    }

    pid_t pid = fork();
    if (pid < 0) { print_error(); return; }

    if (pid == 0) {
        if (outfile != NULL && setup_redirection(outfile) == -1)
            exit(1);
        execv(full_path, argv);
        print_error();
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

void execute_parallel(char **cmd_strings, int count) {
    pid_t *pids = malloc(count * sizeof(pid_t));
    if (!pids) { print_error(); return; }
    int pid_count = 0;

    /* Fase 1: lanzar todos los procesos sin esperar */
    for (int i = 0; i < count; i++) {
        trim_whitespace(cmd_strings[i]);
        if (strlen(cmd_strings[i]) == 0) continue;

        char **argv    = NULL;
        int    argc    = 0;
        char  *outfile = NULL;

        if (parse_command(cmd_strings[i], &argv, &argc, &outfile) == -1)
            continue;

        if (argc == 0) {
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

        /* Built-ins se ejecutan en el proceso del shell, sin fork */
        if (handle_builtin(argv, argc)) {
            free_argv(argv, argc);
            if (outfile) free(outfile);
            continue;
        }

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
            if (outfile != NULL && setup_redirection(outfile) == -1)
                exit(1);
            execv(full_path, argv);
            print_error();
            exit(1);
        }

        pids[pid_count++] = pid;
        free_argv(argv, argc);
        if (outfile) free(outfile);
    }

    /* Fase 2: esperar a TODOS los hijos */
    for (int i = 0; i < pid_count; i++)
        waitpid(pids[i], NULL, 0);

    free(pids);
}

int handle_builtin(char **argv, int argc) {
    if (argv == NULL || argv[0] == NULL) return 0;

    if (strcmp(argv[0], "exit")  == 0) { builtin_exit(argv, argc);  return 1; }
    if (strcmp(argv[0], "chd")   == 0) { builtin_chd(argv, argc);   return 1; }
    if (strcmp(argv[0], "route") == 0) { builtin_route(argv, argc); return 1; }

    return 0;
}

void builtin_exit(char **argv, int argc) {
    (void)argv;
    if (argc != 1) { print_error(); return; }
    exit(0);
}

void builtin_chd(char **argv, int argc) {
    /* chd requiere exactamente 1 argumento */
    if (argc != 2) { print_error(); return; }
    if (chdir(argv[1]) == -1) print_error();
}

void builtin_route(char **argv, int argc) {
    free_path();
    for (int i = 1; i < argc; i++) {
        if (path_count >= MAX_PATH_DIRS) break;
        path[path_count] = strdup(argv[i]);
        if (path[path_count]) path_count++;
    }
}
