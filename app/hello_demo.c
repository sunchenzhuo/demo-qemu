#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <time.h>

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    struct utsname info;
    int count = 0;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    if (uname(&info) != 0)
    {
        perror("uname");
        return 1;
    }
    printf("hello_demo started\n");
    printf("sysname:%s\n", info.sysname);
    printf("nodename:%s\n", info.nodename);
    printf("release:%s\n", info.release);
    printf("machine:%s\n", info.machine);
    fflush(stdout);

    while (running)
    {
        time_t now = time(NULL);
        printf("hello_demo tick %d,time=%ld,pid=%d\n", count++, (long)now, getpid());
        fflush(stdout);
        sleep(2);
    }

    printf("hello_demo stopping\n");
    fflush(stdout);
    return 0;
}