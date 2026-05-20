#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname info;
    printf("hello from cross compiled ARM program\n");
    printf("pid:%d\n", getpid());
    if (uname(&info) == 0)
    {
        printf("sysname:%s\n", info.sysname);
        printf("nodename:%s\n", info.nodename);
        printf("release:%s\n", info.release);
        printf("machine:%s\n", info.machine);
    }
    return 0;
}