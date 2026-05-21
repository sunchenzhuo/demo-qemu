/**
 * @文件路径         : /shu/projects/demo-qemu/app/base_client_loop.c
 * @作者           : 树
 * @创建时间         : 2026-05-21 14:16:07
 * @最后编辑         : 树
 * @最后编辑时间       : 2026-05-21 14:51:49
 * @Version      : V1.0.0
 * @功能描述         : 一个简单的TCP客户端循环示例，每2秒向服务器发送一次控制命令并接收响应。
 * @Copyright    : Copyright (c) 2026 by 树, All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define DEFAULT_HOST "10.0.2.2" // 默认目标主机地址
#define DEFAULT_PORT 7000       // 默认目标端口号

// 发送一次命令并接收服务器响应
static int send_once(const char *host, int port, int count)
{
    // 创建TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); // 将端口转换为网络字节序

    // 将IP字符串转换为网络地址结构
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) != 1)
    {
        perror("inet_pton");
        close(sock);
        return -1;
    }

    // 连接服务器
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock);
        return -1;
    }

    // 构造要发送的控制命令数据
    double vx = 0.10;
    double vy = 0.00;
    double wz = 0.20;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "CMD %.2f %.2f %.2f", vx, vy, wz);

    printf("loop %d TX: %s", count, cmd);
    fflush(stdout);

    // 发送命令到服务器
    if (send(sock, cmd, strlen(cmd), 0) < 0)
    {
        perror("send");
        close(sock);
        return -1;
    }

    char buf[256];
    memset(buf, 0, sizeof(buf));

    // 接收服务器响应
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n < 0)
    {
        perror("recv");
        close(sock);
        return -1;
    }

    if (n == 0)
    {
        // 服务器关闭了连接
        printf("loop %d RX: server closed connection\n", count);
    }
    else
    {
        buf[n] = '\0';
        printf("loop %d RX: %s\n", count, buf);
    }

    fflush(stdout);
    close(sock); // 关闭socket连接
    return 0;
}

int main(int argc, char const *argv[])
{
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int count = 0;

    // 读取命令行参数，允许指定目标主机和端口
    if (argc > 1)
    {
        host = argv[1];
    }

    if (argc > 2)
    {
        port = atoi(argv[2]);
    }

    printf("base client loop started,target=%s:%d\n", host, port);
    fflush(stdout);

    // 无限循环，每次发送一次命令，失败则重试
    while (1)
    {
        if (send_once(host, port, count) != 0)
        {
            printf("loop %d failed,will retry\n", count);
            fflush(stdout);
        }
        count++;
        sleep(2); // 每2秒发送一次
    }

    return 0;
}
