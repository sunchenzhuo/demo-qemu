/**
 * @文件路径         : /shu/projects/demo-qemu/app/base_client_loop.c
 * @作者           : 树
 * @创建时间         : 2026-05-21 14:16:07
 * @最后编辑         : 树
 * @最后编辑时间       : 2026-05-21 17:44:34
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

/*
 * 详细技术注释（低级实现细节、跨平台与安全注意事项）
 *
 * 头文件用途：
 * - <stdio.h> / <stdlib.h>: 标准I/O与内存/退出管理；用于打印与字符串/数值转换。
 * - <unistd.h>: POSIX API（如 close、sleep）；在 Windows 上需替代实现。
 * - <string.h>: 字符串/内存操作（memset、strcmp、strlen）。
 * - <arpa/inet.h>: 提供网络地址转换（inet_pton、ntohl/htonl），处理 IPv4 地址文本/二进制转换。
 * - <sys/socket.h>: socket API 的核心声明（socket、connect、send、recv、bind、listen、accept）。
 *
 * 字节序与端口：
 * - 网络字节序（big-endian）与主机字节序可能不同。使用 htons/ntohs/htonl/ntohl 保证跨平台一致性。
 * - 在此程序中，通过 htons(port) 将主机无符号短整型转换为网络字节序后再赋值给 sockaddr_in。
 *
 * 错误与信号处理建议：
 * - 对于 send，当对端已关闭时可能触发 SIGPIPE 导致进程被终止。生产代码中常用 `signal(SIGPIPE, SIG_IGN)` 或在 send 时使用 `MSG_NOSIGNAL` 标志来避免程序异常退出。
 * - 当前程序在遇到 socket 错误时直接打印 perror 并关闭 socket，这在测试场景足够，但生产代码应区分可重试错误与无法恢复的错误。
 *
 * 阻塞行为、超时与可扩展性：
 * - 本例使用阻塞 socket，connect/send/recv 会阻塞当前线程。对于 GUI 或并发客户端，应使用非阻塞 socket + select/epoll 或专用工作线程。
 * - 可通过 setsockopt(..., SO_RCVTIMEO) 或 select/poll 实现读超时，避免长时间阻塞。
 *
 * I/O 原子性与部分发送/接收：
 * - send 不保证一次写入将全部发送（尤其大数据）；应使用循环直到发送完毕（检查返回值并移动指针）。本例发送的数据短，通常一次发送完成。
 * - recv 可能返回部分消息、多个消息或在消息中间断开。文件中使用了按换行拆分的缓冲处理，但仍需处理边界情况（无换行时继续累积，避免无限增长）。
 *
 * 安全与缓冲：
 * - snprintf 在缓冲区不足时会截断，要检查返回值是否>=bufsize以检测截断。
 * - 使用固定大小的栈缓冲区（如 char buf[256]）时要考虑输入数据可能超过预期长度，避免缓冲区溢出。生产代码可考虑动态缓冲或限制每次读取量并丢弃超长行。
 *
 * 文本协议解析注意事项：
 * - sscanf 易用但不够健壮：当输入格式不严格或存在异常字符时可能解析失败或出现未定义行为。
 * - 推荐使用更安全的解析方式，如 strtok + strtol/strtod 或手写有限状态机来逐字段解析并检查边界与返回值。
 */

#define DEFAULT_HOST "10.0.2.2" // 默认目标主机地址
#define DEFAULT_PORT 7000       // 默认目标端口号

static const char *err_to_text(int err)
{
    switch (err)
    {
    case 0:
        return "OK";
    case 1:
        return "LOW_BATTERY";
    case 2:
        return "BAD_COMMAND";
    case 3:
        return "SPEED_LIMIT";
    default:
        return "UNKNOWN";
    }
}

// 发送一次命令并接收服务器响应
static int send_once(const char *host, int port, int seq)
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
    snprintf(cmd, sizeof(cmd), "CMD %d %.2f %.2f %.2f\n", seq, vx, vy, wz);

    printf("loop %d TX: %s", seq, cmd);
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
        printf("loop %d RX: server closed connection\n", seq);
        close(sock);
        return -1;
    }

    buf[n] = '\0'; // 确保字符串以null结尾
    printf("loop %d RX: %s", seq, buf);

    /*
     * 解析响应时注意类型匹配：
     * - 服务器返回格式为: STA seq vx vy wz battery err
     * - vx, vy, wz 在服务器端使用浮点格式输出 (两位小数)
     * - battery 在服务器端也按浮点格式输出
     * 但下面原始代码把接收变量声明为 int 并将 %lf (double) 赋给 int*，
     * 这是未定义行为，会导致解析错误或崩溃。
     * 建议将这些变量声明为 double 或按服务器协议调整格式说明符。
     */
    char tag[16];
    int rx_seq = -1;
    double rx_vx = 0.0; // 修正类型：双精度以匹配 %lf
    double rx_vy = 0.0;
    double rx_wz = 0.0;
    double battery = 0.0; // 电量通常为浮点数（服务器格式为两位小数）
    int err = -1;

    /* 使用与服务器格式匹配的 scanf 格式：%lf 对应 double* */
    int parsed = sscanf(buf, "%15s %d %lf %lf %lf %lf %d", tag, &rx_seq, &rx_vx, &rx_vy, &rx_wz, &battery, &err);
    if (parsed == 7 && strcmp(tag, "STA") == 0)
    {
        if (rx_seq != seq)
        {
            printf("loop %d WARN: seq mismatch,rx_seq=%d\n", seq, rx_seq);
        }

        /*
         * 输出时注意类型：rx_vx/ry_vy/rx_wz 为 double，battery 为 double。
         * 原代码以 %d 打印 battery，会截断并导致错误显示，改为打印浮点或四舍五入为整数。
         */
        printf("loop %d STATUS: vx=%.2f,vy=%.2f,wz=%.2f,battery=%.2f%%,err=%s\n", seq, rx_vx, rx_vy, rx_wz, battery, err_to_text(err));
    }
    else
    {
        printf("loop %d WARN: bad response format\n", seq);
    }

    fflush(stdout);
    close(sock);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int seq = 0;

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
        if (send_once(host, port, seq) != 0)
        {
            printf("loop %d failed,will retry\n", seq);
            fflush(stdout);
        }
        seq++;
        sleep(2); // 每2秒发送一次
    }

    return 0;
}
