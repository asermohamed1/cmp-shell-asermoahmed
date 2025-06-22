#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#define MAX_INPUT_LEN 1024
#define MAX_ARGS 100
#define ERROR_MSG "An error has occured!\n" 

char *paths[MAX_ARGS];
volatile sig_atomic_t child_pid = -1; // intial value for child process = -1 

void print_error() {
    fprintf(stderr, ERROR_MSG);
}

void signal_handler(int sig) {
    if (child_pid != -1) { // to check there isnt child process working now 
        kill(child_pid, sig);
    }
}

void free_paths() {
    for (int i = 0; paths[i] != NULL; i++) {
        free(paths[i]);
        paths[i] = NULL;
    }
}

int tokenize(char *line, char **tokens) {
    int count = 0;
    while (*line) {
        while (*line && isspace((unsigned char)*line)) line++;
        if (!*line) break;
        char token[MAX_INPUT_LEN];
        int pos = 0;
        if (*line == '"' || *line == '\'') {
            char quote = *line;
            line++; 
            while (*line && *line != quote) {
                token[pos++] = *line;
                line++;
            }
            if (*line == quote) line++; 
        } else {
            while (*line && !isspace((unsigned char)*line)) {
                token[pos++] = *line;
                line++;
            }
        }
        token[pos] = '\0';
        tokens[count] = strdup(token);
        if (tokens[count] == NULL) {
            print_error();
            exit(1);
        }
        count++;
    }
    tokens[count] = NULL;
    return count;
}

void execute_command(char **args, int redirect, char *file) {
    pid_t pid = fork();
    if (pid == 0) { // execute external command 
        if (redirect) {
            FILE *f = fopen(file, "w");
            if (f == NULL) {
                print_error();
                exit(1);
            }
            if (dup2(fileno(f), STDOUT_FILENO) < 0) {    // if redirect to file rather than output screen  (fail) 
                print_error();
                fclose(f);
                exit(1);
            }
            fclose(f);
        }
        if (strchr(args[0], '/') != NULL) {  // if command has / which has more priority 
            execv(args[0], args);
            print_error();
            exit(1);
        } else {
            for (int i = 0; paths[i] != NULL; i++) {
                char exec_path[PATH_MAX];
                snprintf(exec_path, sizeof(exec_path), "%s/%s", paths[i], args[0]);
                if (access(exec_path, X_OK) == 0) {
                    execv(exec_path, args);
                    print_error();// if path isnt correct so print error and dont complete
                    exit(1);
                }
            }
            print_error();
            exit(1);
        }
    } else if (pid > 0) {
        child_pid = pid;
        wait(NULL);
        child_pid = -1;
    } else {
        print_error();
    }
}

void cd_handler(char **args) {
    if (args[1] == NULL || args[2] != NULL || chdir(args[1]) != 0) {//  cd should take only one argument  
        print_error();
    }
}

void pwd_handler() { // take current path and print it 
    char pwd_path[PATH_MAX];
    if (getcwd(pwd_path, sizeof(pwd_path))) {
        printf("%s\n", pwd_path);
    } else {
        print_error();
    }
}

void paths_handler(char **args) {
    free_paths();
    if (args[1] == NULL) {
        paths[0] = NULL;
        return;
    }
    int i;
    for (i = 1; args[i] != NULL && i-1 < MAX_ARGS - 1; i++) {
        paths[i-1] = strdup(args[i]); // save paths that in file in paths array dynamically allocated for controlled space 
        if (paths[i-1] == NULL) {
            print_error();
            exit(1);
        }
    }
    paths[i-1] = NULL;// end of paths 
}
void process_pipeline(char *input) {
    char *args[MAX_ARGS];
    int num_args = 0;
    char *cmd = strtok(input, "|");
    while (cmd != NULL && num_args < MAX_ARGS) {
        while (*cmd && isspace((unsigned char)*cmd)) cmd++;
        char *end = cmd + strlen(cmd) - 1;
        while (end > cmd && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
         }
        args[num_args++] = cmd; // tokenize line to commands
        cmd = strtok(NULL, "|");
      }

    int pipefds[num_args - 1][2]; // array containing pipes
    for (int i = 0; i < num_args - 1; i++) {
        if (pipe(pipefds[i]) < 0) { // create pipes using pipe()
            print_error();
            return;
        }
    }
    pid_t pids[num_args];
    for (int i = 0; i < num_args; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            print_error();
            return;
        } else if (pid == 0) {
            if (i > 0) {
                if (dup2(pipefds[i-1][0], STDIN_FILENO) < 0) { // for reading of process > 0 
                    print_error();
                    exit(1);
                }
            }
            if (i < num_args - 1) {   // for writing on the next processes (enable writing)
                if (dup2(pipefds[i][1], STDOUT_FILENO) < 0) {
                    print_error();
                    exit(1);
                }
            }
            for (int j = 0; j < num_args - 1; j++) { // close the beginnings and ends of pipes array (unused)
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }
      
            char local_input[MAX_INPUT_LEN * 2];
            strncpy(local_input, args[i], sizeof(local_input)-1);
            local_input[sizeof(local_input)-1] = '\0'; // for safety
            
            char *child_args[MAX_ARGS];
            int redirect = 0;
            char *redirect_file = NULL;
            int token_count = tokenize(local_input, child_args);
            for (int k = 0; k < token_count; k++) {
                if (strcmp(child_args[k], ">") == 0) {
                    if (k + 1 >= token_count) { // print error if doesnt exist any name for file after >
                        print_error();
                        exit(1);
                    }
                    redirect = 1;
                    redirect_file = child_args[k+1];
                    if (k + 2 != token_count) { // must not be any names of files after it so check 
                        print_error();
                        exit(1);
                    }
                    child_args[k] = NULL; 
                    break;
                }
            }

            if (child_args[0] == NULL)
                exit(0);

            if (strchr(child_args[0], '/') != NULL) {
                if (redirect && i != num_args - 1) {
                    print_error();
                    exit(1);
                }
                if (redirect && i == num_args - 1) {
                    FILE *f = fopen(redirect_file, "w");
                    if (f == NULL) {
                        print_error();
                        exit(1);
                    }
                    if (dup2(fileno(f), STDOUT_FILENO) < 0) { // output for screen from file 
                        print_error();
                        fclose(f);
                        exit(1);
                    }
                    fclose(f);
                }
                execv(child_args[0], child_args);
                print_error();
                exit(1);
            } else {
                for (int k = 0; paths[k] != NULL; k++) {
                    char exec_path[PATH_MAX];
                    snprintf(exec_path, sizeof(exec_path), "%s/%s", paths[k], child_args[0]);
                    if (access(exec_path, X_OK) == 0) {
                        if (redirect && i == num_args - 1) {
                            FILE *f = fopen(redirect_file, "w");
                            if (f == NULL) {
                                print_error();
                                exit(1);
                            }
                            if (dup2(fileno(f), STDOUT_FILENO) < 0) {
                                print_error();
                                fclose(f);
                                exit(1);
                            }
                            fclose(f);
                        } else if (redirect) {
                            print_error();
                            exit(1);
                        }
                        execv(exec_path, child_args);
                        print_error();
                        exit(1);
                    }
                }
                print_error();
                exit(1);
            }
        } else {
            pids[i] = pid;
        }
    }
    for (int i = 0; i < num_args - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
    for (int i = 0; i < num_args; i++) {
        waitpid(pids[i], NULL, 0);
    }
}
void process_input(char *input) {
    if (strstr(input, "|") != NULL) {
        process_pipeline(input);
        return;
    }
    char temp[MAX_INPUT_LEN * 2];
    int j = 0;
    for (int i = 0; input[i] && j < (int)sizeof(temp)-1; i++) {// if input is ls>out.txt so will be ls > out.txt 
        if (input[i] == '>') {
            if (j+3 < (int)sizeof(temp)) {
                temp[j++] = ' ';
                temp[j++] = '>';
                temp[j++] = ' ';
            }
        } else {
            temp[j++] = input[i];
        }
    }
    temp[j] = '\0';

    char *args[MAX_ARGS];
    int redirect = 0; // is there redirect or not : 0 not , 1 : there is 
    char *redirect_file = NULL;
    int token_count = tokenize(temp, args);
    if (token_count == 0)
        return;
    for (int i = 0; i < token_count; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (i+1 >= token_count) {
                print_error();
                return;
            }
            redirect = 1;
            redirect_file = args[i+1];
            if (i+2 != token_count) {
                print_error();
                return;
            }
            args[i] = NULL;
            break;
        }
    }
    if (strcmp(args[0], "exit") == 0) {
        if (args[1] != NULL)
            print_error();
        else
            exit(0);
    } else if (strcmp(args[0], "cd") == 0) {
        cd_handler(args);
    } else if (strcmp(args[0], "pwd") == 0) {
        if (args[1] != NULL)
            print_error();
        else
            pwd_handler();
    } else if (strcmp(args[0], "path") == 0) {
        paths_handler(args);
    } else {// for external commands 
        execute_command(args, redirect, redirect_file);
    }
}

void interactive_mode() {
    char input[MAX_INPUT_LEN];
    while (1) {// isatty() checks for its in interactive or not , if it prints cmpsh> 
        if (isatty(STDIN_FILENO)) {
            printf("cmpsh> ");
            fflush(stdout);
        }
        if (!fgets(input, MAX_INPUT_LEN, stdin))
            exit(0);
        input[strcspn(input, "\n")] = 0;// without that , strcmp() will fail , it for removing \n at the end 
        process_input(input);
    }
}

void non_interactive_mode(FILE *f) {
    char input[MAX_INPUT_LEN];
    while (fgets(input, MAX_INPUT_LEN, f)) {
        input[strcspn(input, "\n")] = 0;
        process_input(input);
    }
    fclose(f);
    exit(0);
}

int main(int argc, char *argv[]) {
    paths[0] = strdup("/bin");
    paths[1] = NULL;
    signal(SIGINT, signal_handler);  // ctrl+c  default action is terminating the process.
    signal(SIGTSTP, signal_handler); // ctrl+z
    if (argc == 1) {
        interactive_mode();
    } else if (argc == 2) {
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            print_error();
            exit(1);
        }
        non_interactive_mode(f);// has file to read from it 
    } else {
        print_error();
        exit(1);
    }
    free_paths(); // to release paths free space 
    return 0;
}

