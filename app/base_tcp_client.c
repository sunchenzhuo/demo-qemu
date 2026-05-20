/*
 * @Author: shu shuxianshengio@126.com
 * @Date: 2026-05-20 15:49:05
 * @LastEditors: shu shuxianshengio@126.com
 * @LastEditTime: 2026-05-20 16:06:32
 * @FilePath: /shu/projects/demo-qemu/app/base_tcp_client.c
 * @Description: TCP 客户端示例程序，连接到指定服务器，发送简单命令并打印响应。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// 默认连接的服务器地址和端口
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7000

int main(int argc, char const *argv[])
{
    // 默认目标地址和端口
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    // 支持可选命令行参数
    // 用法: ./base_tcp_client [host] [port]
    // 例如: ./base_tcp_client 192.168.1.100 7000
    // 注意：这段逻辑中判断 argc 的条件为 <1，正常情况下第一个参数应当通过 argc > 1 来判断。
    // 这里保留原逻辑，仅增加注释解释其行为。
    if (argc > 1)
    {
        // 如果程序接收到了参数，则将第一个参数作为目标主机地址
        host = argv[1];
    }

    if (argc > 2)
    {
        // 如果传入第二个参数，则将其解析为目标端口号
        // atoi 返回整数值，若参数不是合法数字则返回 0。
        port = atoi(argv[2]);
    }

    // 创建一个 TCP Socket
    // AF_INET 表示 IPv4 地址族，SOCK_STREAM 表示 TCP 连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    // 初始化服务器地址结构，全部置 0
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // 地址族设置为 IPv4
    server_addr.sin_family = AF_INET;
    // 将主机字节序的端口转换为网络字节序
    server_addr.sin_port = htons(port);

    // 将字符串形式的 IPv4 地址转换成二进制形式，存入 sin_addr
    // inet_pton 返回 1 表示成功，0 表示参数不是有效地址字符串，-1 表示调用失败并设置 errno
    // 这里保留原有判断方式，但更严格的写法应当检查返回值是否等于 1。
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) != 1)
    {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    // 打印要连接的目标地址和端口
    printf("connecting to %s:%d\n", host, port);

    // 连接到服务器
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock);
        return 1;
    }

    // 要发送给服务器的命令字符串
    const char *cmd = "CMD 0.10 0.00 0.20\n";
    printf("TX:%s", cmd);

    // 发送命令到服务器，send 的第四个参数为标志位，这里传 0 表示默认行为
    if (send(sock, cmd, strlen(cmd), 0) < 0)
    {
        perror("send");
        close(sock);
        return 1;
    }

    // 接收服务器返回的数据
    char buf[256];
    // 将缓冲区清零，便于后续处理
    memset(buf, 0, sizeof(buf));

    // recv 返回接收到的字节数，如果连接关闭返回 0，出错返回 -1
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n < 0)
    {
        perror("recv");
        close(sock);
        return 1;
    }

    if (n == 0)
    {
        // 服务器主动关闭连接时，recv 返回 0
        printf("RX:server closed connection\n");
    }
    else
    {
        // 将接收到的数据按照字符串处理，确保末尾有终止符
        buf[n] = '\0';
        printf("RX:%s\n", buf);
    }

    // 关闭 socket，释放资源
    close(sock);
    return 0;
}
