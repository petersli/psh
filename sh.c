#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "jobs.h"

#define BUFFER_SIZE 1024
#define TOKENS_SIZE 512
#define ARGV_SIZE 512
#define PATH_MAX 512

/* Global variables that are reset each iteration */
char buffer[BUFFER_SIZE];
char *tokens[TOKENS_SIZE];
char *argv[ARGV_SIZE];

// 0 if not redirecting input, 1 if redirecting
int input_redirect_code;

// 0 if not redirecting output, 1 if truncated, 2 if appended
int output_redirect_code;

// paths for redirection targets
char *input_file;
char *output_file;

// 0 if foreground process or no child process, 1 if background process
int bg_process_flag;

// number of tokens, not counting redirection tokens or '&' (bg process)
int token_num;

/* Global variables that persist as long as shell is running */
// job list
job_list_t *job_list;

// next available job id, incremented after each job is added to job_list
int next_avail_jid = 1;

// pgid of shell, initialized at start of main
pid_t shell_pgid;

/*
 * is_redirection_sym()
 * - Description: Returns 1 if input string is one of ">", ">>", "<". Returns 0
 * otherwise.
 *
 * - Arguments: str
 */
int is_redirection_sym(char *str) {
    return !(strcmp(str, "<") && strcmp(str, ">") && strcmp(str, ">>"));
}

/*
 * parse()
 * - Description: creates the token and argv arrays from the buffer character
 *   array. Handles input/output redirection by setting global variables.
 *
 * - Arguments: buffer: a char array representing user input, tokens: the
 * tokenized input, argv: the argument array eventually used for execv()
 *
 * - Returns: 0 on success, -1 on error
 *
 * - Usage:
 *      For the tokens array:
 *
 *      cd dir -> [cd, dir]
 *      [tab]mkdir[tab][space]name -> [mkdir, name]
 *      /bin/echo 'Hello world!' -> [/bin/echo, 'Hello, world!']
 *
 *      For the argv array:
 *
 *       char *argv[4];
 *       argv[0] = echo;
 *       argv[1] = 'Hello;
 *       argv[2] = world!';
 *       argv[3] = NULL;
 */
int parse(char buffer[BUFFER_SIZE], char *tokens[512], char *argv[512]) {
    char *str = buffer;
    char *token;

    while ((token = strtok(str, " \t\n")) != NULL) {
        // see sept 21 lecture on strtok
        tokens[token_num] = token;
        str = NULL;
        token_num++;
    }

    char *tokens_final[512] = {
        0};  // will hold tokens without redirection symbols
    int token_final_num = 0;

    int input_redirected = 0;   // has input redirection symbol been parsed
    int output_redirected = 0;  // has output redirection symbol been parsed

    for (int i = 0; i < token_num; i++) {
        // Redirect input
        if (strcmp(tokens[i], "<") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "syntax error: no input file\n");
                return -1;
            }
            if (is_redirection_sym(tokens[i + 1])) {
                fprintf(stderr,
                        "syntax error: input file is a redirection symbol\n");
                return -1;
            }
            if (!input_redirected) {
                input_redirect_code = 1;  // redirect input
                input_file = tokens[i + 1];
                input_redirected = 1;
                i++;  // skip token for input file
            } else {  // input has already been redirected
                fprintf(stderr, "syntax error: multiple input files\n");
                return -1;
            }
        }
        // Redirect output with O_CREAT | O_TRUNC, mode=0666
        else if (strcmp(tokens[i], ">") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "syntax error: no output file\n");
                return -1;
            }
            if (is_redirection_sym(tokens[i + 1])) {
                fprintf(stderr,
                        "syntax error: output file is a redirection symbol\n");
                return -1;
            }
            if (!output_redirected) {
                output_redirect_code = 1;  // redirect output, truncate
                output_file = tokens[i + 1];
                output_redirected = 1;
                i++;  // skip token for output file
            } else {  // output has already been redirected
                fprintf(stderr, "syntax error: multiple output files\n");
                return -1;
            }
        }
        // Redirect output with O_CREAT | O_APPEND, mode=0666
        else if (strcmp(tokens[i], ">>") == 0) {
            if (tokens[i + 1] == NULL) {
                fprintf(stderr, "syntax error: no output file\n");
                return -1;
            }
            if (is_redirection_sym(tokens[i + 1])) {
                fprintf(stderr,
                        "syntax error: output file is a redirection symbol\n");
                return -1;
            }
            if (!output_redirected) {
                output_redirect_code = 2;  // redirect output, append
                output_file = tokens[i + 1];
                output_redirected = 1;
                i++;  // skip token for output file
            } else {  // output has already been redirected
                fprintf(stderr, "syntax error: multiple output files\n");
                return -1;
            }
        }
        // Token is not a redirection symbol or target
        else {
            tokens_final[token_final_num] = tokens[i];
            token_final_num++;
        }
    }

    // clear tokens array
    memset(tokens, 0, (size_t)token_num);

    // populate tokens array with tokens, sans redirect symbols and input/output
    // files
    for (int i = 0; i < token_final_num; i++) {
        tokens[i] = tokens_final[i];
    }

    // set token_num to number of non-redirect tokens
    token_num = token_final_num;

    // token_num may count "&"
    if (token_num > 0) {
        // handle background processes; remove "&" from tokens and token_num
        if (strcmp(tokens[token_num - 1], "&") == 0) {
            bg_process_flag = 1;
            tokens[token_num - 1] = '\0';
            token_num = token_num - 1;
        }
    }

    // token_num without "&"
    if (token_num > 0) {
        char *last_slash = strrchr(tokens[0], '/');

        // set argv[0]
        if (last_slash != NULL) {
            char *binary_name = &last_slash[1];
            argv[0] = binary_name;
        } else {
            argv[0] = tokens[0];
        }

        // set argv[1] through argv[argc - 1]
        for (int i = 1; i < token_num; i++) {
            argv[i] = tokens[i];
        }

        // set argv[argc] to NULL as specified
        argv[token_num] = NULL;
    }

    return 0;
}

/*
 * handle_fg_process()
 * - Description: Pass terminal control to child process, then call waitpid to
 * wait for the process to finish. Check status for terminating or suspending
 * signals, and take appropiate action.
 * If command == NULL, this function is being called by "fg" command
 * subroutine. Update state of job if suspended, remove from job list if
 * terminated.
 * if command != NULL, this is the first time this job is being run.
 * Add job to job list if suspended.
 *
 * - Arguments: child_pid: the pid of the fg process, command: the name of the
 * command or NULL
 *
 * - Returns: 0 on success, -1 on error
 *
 * - Usage example:
 *      To handle fg job with pid=17792 generated by command "/bin/sleep":
 *      handle_fg_process(17792, "/bin/sleep");
 */

int handle_fg_process(pid_t child_pid, char *command) {
    // Give foreground child process group terminal control
    if (tcsetpgrp(STDIN_FILENO, child_pid) < 0) {
        perror("tcsetpgrp");
        return -1;
    }

    /* Wait for fg process to finish */
    int fg_status;
    waitpid(child_pid, &fg_status, WUNTRACED);

    /* Job is already on job list (called during fg subroutine) */
    if (command == NULL) {
        // get jid
        int fg_jid;
        if ((fg_jid = get_job_jid(job_list, child_pid)) < 0) {
            fprintf(stderr, "Error getting jid");
            return -1;
        }
        // Exited normally
        if (WIFEXITED(fg_status)) {
            /* Remove job from job list */
            if (remove_job_pid(job_list, child_pid) < 0) {
                fprintf(stderr, "Error removing job");
            }
        }
        // Foreground process terminated by signal
        if (WIFSIGNALED(fg_status)) {
            int signum = WTERMSIG(fg_status);
            // print message with current jid
            if (printf("[%d] (%d) terminated by signal %d\n", fg_jid, child_pid,
                       signum) < 0) {
                fprintf(stderr, "Error printing");
                return -1;
            }
            /* Remove job from job list */
            if (remove_job_pid(job_list, child_pid) < 0) {
                fprintf(stderr, "Error removing job");
                return -1;
            }
        }
        // Foreground process suspended by signal
        if (WIFSTOPPED(fg_status)) {
            int signum = WSTOPSIG(fg_status);
            // print message with current jid
            if (printf("[%d] (%d) suspended by signal %d\n", fg_jid, child_pid,
                       signum) < 0) {
                fprintf(stderr, "Error printing");
                return -1;
            }
            if (update_job_pid(job_list, child_pid, STOPPED) < 0) {
                fprintf(stderr, "Error updating job state");
                return -1;
            }
        }

    }
    /* Job is being run for the first time. Add to job list if suspended. */
    else {
        // Foreground process terminated by signal
        if (WIFSIGNALED(fg_status)) {
            int signum = WTERMSIG(fg_status);
            // print message with next available jid (arbitrary)
            if (printf("[%d] (%d) terminated by signal %d\n", next_avail_jid,
                       child_pid, signum) < 0) {
                fprintf(stderr, "Error printing");
                return -1;
            }
        }
        // Foreground process suspended by signal
        if (WIFSTOPPED(fg_status)) {
            int signum = WSTOPSIG(fg_status);
            // print message with next_avail_jid (arbitrary)
            if (printf("[%d] (%d) suspended by signal %d\n", next_avail_jid,
                       child_pid, signum) < 0) {
                fprintf(stderr, "Error printing");
                return -1;
            }
            if (add_job(job_list, next_avail_jid, child_pid, STOPPED, command) <
                0) {
                fprintf(stderr, "Error adding job");
                return -1;
            }
            next_avail_jid++;
        }
    }

    /* Pass terminal control back to parent (shell) */
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) {
        perror("tcsetpgrp");
        return -1;
    }

    return 0;
}

/*
 * exec_child()
 * - Description: If in child process, sets child pgid, restores signal
 * handlers, and does file redirection. Calls execv. If in parent process, does
 * nothing.
 *
 * - Arguments: child_pid: the process id of the child process
 */
void exec_child(pid_t child_pid) {
    // Child Process
    if (child_pid == 0) {
        /* Set child's pgid to its pid (to make distinct from parent's pgid) */
        if (setpgid(getpid(), getpid()) < 0) {
            perror("setpgid");
            exit(1);
        }

        /* Set previously ignored signals back to default behavior for
         * child */
        if (signal(SIGTTOU, SIG_DFL) == SIG_ERR) {
            perror("signal");
            exit(1);
        }
        if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
            perror("signal");
            exit(1);
        }
        if (signal(SIGTSTP, SIG_DFL) == SIG_ERR) {
            perror("signal");
            exit(1);
        }
        if (signal(SIGQUIT, SIG_DFL) == SIG_ERR) {
            perror("signal");
            exit(1);
        }

        /* I/O Redirection */
        if (input_redirect_code == 1) {  // input
            if (close(STDIN_FILENO) < 0) {
                perror("close");
            }
            if (open(input_file, O_RDONLY, 0) < 0) {
                perror("open");
            }
        }

        if (output_redirect_code == 1) {  // output truncated
            if (close(STDOUT_FILENO) < 0) {
                perror("close");
            }
            if (open(output_file, O_RDWR | O_CREAT | O_TRUNC, 0666) <
                0) {  // read write mode is 0666
                perror("open");
            }
        } else if (output_redirect_code == 2) {  // output appended
            if (close(STDOUT_FILENO) < 0) {
                perror("close");
            }
            if (open(output_file, O_RDWR | O_CREAT | O_APPEND, 0666) <
                0) {  // read write mode is 0666
                perror("open");
            }
        }

        execv(tokens[0], argv);

        // only reach here if execv failed
        perror("execv");
        exit(1);
    }
}

/*
 * reap_jobs()
 * - Description: waitpid on all jobs in job list, printing and updating job
 * list as needed.
 */
void reap_jobs(void) {
    pid_t current_pid;
    while ((current_pid = get_next_pid(job_list)) > 0) {
        // get jid
        int current_jid;
        if ((current_jid = get_job_jid(job_list, current_pid)) < 0) {
            fprintf(stderr, "Error getting job jid");
        }

        int status;
        /* If current child process changed state, update job list and print
         * explanation */
        if (waitpid(current_pid, &status, WNOHANG | WUNTRACED | WCONTINUED) >
            0) {
            // Exited normally
            if (WIFEXITED(status)) {
                int exit_status = WEXITSTATUS(status);
                if (printf("[%d] (%d) terminated with exit status %d\n",
                           current_jid, current_pid, exit_status) < 0) {
                    fprintf(stderr, "Error printing");
                }
                /* Remove job from job list */
                if (remove_job_pid(job_list, current_pid) < 0) {
                    fprintf(stderr, "Error removing job");
                }
            }
            // Terminated by signal
            else if (WIFSIGNALED(status)) {
                int signum = WTERMSIG(status);
                // print message
                if (printf("[%d] (%d) terminated by signal %d\n", current_jid,
                           current_pid, signum) < 0) {
                    fprintf(stderr, "Error printing");
                }
                /* Remove job from job list */
                if (remove_job_pid(job_list, current_pid) < 0) {
                    fprintf(stderr, "Error removing job");
                }
            }
            // Suspended via signal
            else if (WIFSTOPPED(status)) {
                if (update_job_pid(job_list, current_pid, STOPPED) < 0) {
                    fprintf(stderr, "Error updating job state");
                }
                int signum = WSTOPSIG(status);
                if (printf("[%d] (%d) suspended by signal %d\n", current_jid,
                           current_pid, signum) < 0) {
                    fprintf(stderr, "Error printing");
                }
            }
            // Resumed via signal
            else if (WIFCONTINUED(status)) {
                if (update_job_pid(job_list, current_pid, RUNNING) < 0) {
                    fprintf(stderr, "Error updating job state");
                }
                if (printf("[%d] (%d) resumed\n", current_jid, current_pid) <
                    0) {
                    fprintf(stderr, "Error printing");
                }
            }
        }
    }
}

int main() {
    job_list = init_job_list();  // create job list
    ssize_t chars_read;          // set by read()
    shell_pgid = getpgrp();

    do {
        /* Ignore signals in parent process */
        if (signal(SIGTTOU, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(job_list);
            exit(1);
        }
        if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(job_list);
            exit(1);
        }
        if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(job_list);
            exit(1);
        }
        if (signal(SIGQUIT, SIG_IGN) == SIG_ERR) {
            perror("signal");
            cleanup_job_list(job_list);
            exit(1);
        }

        /* Reaping the Jobs List */
        reap_jobs();

#ifdef PROMPT /* Print prompt */
        char cwd[PATH_MAX];
        getcwd(cwd, PATH_MAX);
        if (printf("psh: %s$ ", cwd) < 0) {
            fprintf(stderr, "Error while printing prompt.");
            cleanup_job_list(job_list);
            exit(1);
        }
        if (fflush(stdout) < 0) {
            fprintf(stderr, "Error while flushing prompt.");
            cleanup_job_list(job_list);
            exit(1);
        }
#endif

        // Reset these for each iteration (new line of input)
        input_redirect_code = 0;
        output_redirect_code = 0;
        input_file = NULL;
        output_file = NULL;
        bg_process_flag = 0;
        token_num = 0;

        // clear buffer, tokens, argv
        memset(buffer, 0, BUFFER_SIZE);
        memset(tokens, 0, TOKENS_SIZE * sizeof(char *));
        memset(argv, 0, ARGV_SIZE * sizeof(char *));

        // Read input from user into buffer
        chars_read = read(STDIN_FILENO, buffer, BUFFER_SIZE);
        if (chars_read == -1) {
            perror("read");
            cleanup_job_list(job_list);
            exit(1);
        }

        // Null terminate the buffer
        buffer[chars_read] = '\0';

        // Parse buffered input
        if (parse(buffer, tokens, argv) < 0) {
            // parse exited abnormally due to user error
            continue;
        }

        // Continue if no non-whitespace input
        if (strlen(buffer) == strspn(buffer, " \t\n")) {
            continue;
        }

        /* Built-in Commands */
        // exit
        else if (strcmp(tokens[0], "exit") == 0) {
            cleanup_job_list(job_list);
            exit(0);
        }
        // cd
        else if (strcmp(tokens[0], "cd") == 0) {
            if (token_num == 2) {  // check arg number
                if (chdir(tokens[1]) < 0) {
                    perror("chdir");
                    continue;
                }
            } else {
                fprintf(stderr, "cd: syntax error\n");
                continue;
            }
        }
        // ln
        else if (strcmp(tokens[0], "ln") == 0) {
            if (token_num == 3) {  // check arg number
                if (link(tokens[1], tokens[2]) < 0) {
                    perror("link");
                    continue;
                }
            } else {
                fprintf(stderr, "ln: syntax error\n");
                continue;
            }
        }
        // rm
        else if (strcmp(tokens[0], "rm") == 0) {
            if (token_num == 2) {  // check arg number
                if (unlink(tokens[1]) < 0) {
                    perror("unlink");
                    continue;
                }
            } else {
                fprintf(stderr, "rm: syntax error\n");
                continue;
            }
        }
        // fg
        else if (strcmp(tokens[0], "fg") == 0) {
            if (token_num == 2) {           // check arg number
                if (*(tokens[1]) == '%') {  // jid should start with %
                    int jid_to_resume =
                        atoi((tokens[1]) + 1);  // skip %, pass to atoi
                    pid_t pid_to_resume;
                    // check that jid refers to valid job
                    if ((pid_to_resume = get_job_pid(job_list, jid_to_resume)) <
                        0) {
                        fprintf(stderr, "fg: job not found\n");
                        continue;
                    } else {  // jid is valid
                        // send SIGCONT to job
                        if (killpg(pid_to_resume, SIGCONT) < 0) {
                            perror("killpg");
                            continue;
                        }
                        // set job to RUNNING
                        if (update_job_pid(job_list, pid_to_resume, RUNNING) <
                            0) {
                            fprintf(stderr, "Error updating job state");
                            continue;
                        }
                        // give job terminal control, call waitpid and handle
                        // status
                        if (handle_fg_process(pid_to_resume, NULL) < 0) {
                            fprintf(stderr, "Error handling fg process");
                            continue;
                        }
                    }
                } else {  // first argument did not start with %
                    fprintf(stderr, "fg: job input does not begin with %%\n");
                    continue;
                }
            } else {  // wrong number of args
                fprintf(stderr, "fg: syntax error\n");
                continue;
            }
        }
        // bg
        else if (strcmp(tokens[0], "bg") == 0) {
            if (token_num == 2) {           // check arg number
                if (*(tokens[1]) == '%') {  // jid should start with %
                    int jid_to_resume =
                        atoi((tokens[1]) + 1);  // skip %, pass to atoi
                    pid_t pid_to_resume;
                    // check that jid refers to valid job
                    if ((pid_to_resume = get_job_pid(job_list, jid_to_resume)) <
                        0) {
                        fprintf(stderr, "bg: job not found\n");
                        continue;
                    }

                    else {
                        // send SIGCONT to job
                        if (killpg(pid_to_resume, SIGCONT) < 0) {
                            perror("killpg");
                            continue;
                        }
                        // set job to RUNNING
                        if (update_job_pid(job_list, pid_to_resume, RUNNING) <
                            0) {
                            fprintf(stderr, "Error updating job state");
                            continue;
                        }
                    }
                } else {  // first argument did not start with %
                    fprintf(stderr, "bg: job input does not begin with %%\n");
                    continue;
                }
            } else {  // wrong number of args
                fprintf(stderr, "bg: syntax error\n");
                continue;
            }
        }
        // jobs (print all jobs)
        else if (strcmp(tokens[0], "jobs") == 0) {
            if (token_num == 1) {
                jobs(job_list);
            } else {  // wrong number of args
                fprintf(stderr, "jobs: syntax error\n");
                continue;
            }
        }

        /* Handling Child Processes */
        else {
            int child_pid = fork();

            /* exec_child contains all logic for if (child_pid == 0) */
            exec_child(child_pid);

            /* For bg processes: add to job list, print job id and process id */
            if (bg_process_flag) {
                if (add_job(job_list, next_avail_jid, child_pid, RUNNING,
                            tokens[0]) < 0) {
                    fprintf(stderr, "Error adding background job");
                }
                if (printf("[%d] (%d)\n", next_avail_jid, child_pid) < 0) {
                    fprintf(
                        stderr,
                        "Error printing job id and pid of background process");
                }
                next_avail_jid++;
            }
            /* For fg processes: waitpid until process finishes */
            else {
                if (handle_fg_process(child_pid, tokens[0]) < 0) {
                    fprintf(stderr, "Error handling foreground process");
                    cleanup_job_list(job_list);
                    exit(1);
                }
            }
        }

    } while (chars_read != 0);  // while not EOF (CTRL-D)

    /* Terminate all bg processes upon receiving EOF */
    pid_t pid_to_term;
    while ((pid_to_term = get_next_pid(job_list)) > 0) {
        if (killpg(pid_to_term, SIGINT) < 0) {
            perror("killpg");
            continue;
        }
    }

    cleanup_job_list(job_list);

    return 0;
}