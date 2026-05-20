#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

// 打开并初始化串口设备，返回文件描述符
static int setup_serial(const char *port)
{
    // 以读写方式打开串口，O_NOCTTY 表示不把该设备当作控制终端
    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror("open serial");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    // 获取当前串口属性
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    // 设置输入输出波特率为 115200
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    // 配置控制标志
    tty.c_cflag |= (CLOCAL | CREAD); // 本地连接，启用接收
    tty.c_cflag &= ~CSIZE;           // 清除数据位掩码
    tty.c_cflag |= CS8;              // 8 数据位
    tty.c_cflag &= ~PARENB;          // 无奇偶校验
    tty.c_cflag &= ~CSTOPB;          // 1 个停止位
    tty.c_cflag &= ~CRTSCTS;         // 禁用 RTS/CTS 硬件流控

    // 非规范模式，不回显，不启用信号字符
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // 禁用软件流控
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    // 禁用输出处理
    tty.c_oflag &= ~OPOST;

    // 设置最小读取字符数和超时时间
    tty.c_cc[VMIN] = 0;   // 立即返回
    tty.c_cc[VTIME] = 20; // 2 秒超时（单位 0.1 秒）

    // 应用配置
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char const *argv[])
{
    const char *port = "/tmp/ttyV0"; // 默认串口设备路径
    if (argc > 1)
    {
        port = argv[1]; // 如果传入参数，则使用第一个参数指定的设备
    }

    int fd = setup_serial(port);
    if (fd < 0)
    {
        return -1;
    }

    const char *cmd = "CMD 0.10 0.00 0.20\n";
    printf("TX:%s", cmd);

    // 向串口写入命令
    if (write(fd, cmd, strlen(cmd)) < 0)
    {
        perror("write");
        close(fd);
        return 1;
    }

    char buf[256];
    memset(buf, 0, sizeof(buf));

    // 从串口读取应答
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0)
    {
        perror("read");
        close(fd);
        return 1;
    }

    if (n == 0)
    {
        printf("RX:timeout\n"); // 读取超时
    }
    else
    {
        buf[n] = '\0';
        printf("RX:%s\n", buf); // 输出接收到的数据
    }

    close(fd);
    return 0;
}
