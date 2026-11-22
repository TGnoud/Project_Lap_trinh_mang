#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

volatile sig_atomic_t finished_children = 0;

void signal_handler(int signum) {
    (void)signum;
    pid_t pid;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        printf("Child PID %ld terminated.\n", (long)pid);
        finished_children++;
    }
}

int main(void) {
    if (signal(SIGCHLD, signal_handler) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    int created_children = 0;
    int exit_code = 0;

    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            exit_code = 1;
            break;
        }

        if (pid == 0) {
            printf("Child process: PID: %ld\n", (long)getpid());
            sleep(2 * (i + 1));
            exit(0);
        }

        created_children++;
    }

    while (finished_children < created_children) {
        sleep(1);
    }

    return exit_code;
}
