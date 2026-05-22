/**
 * @文件路径         : /shu/projects/demo-qemu/app/base_client_loop.c
 * @作者           : 树
 * @创建时间         : 2026-05-21 14:16:07
 * @最后编辑         : 树
 * @最后编辑时间       : 2026-05-22 14:23:09
 * @Version      : V1.0.0
 * @功能描述         : 一个简单的TCP客户端循环示例，每2秒向服务器发送一次控制命令并接收响应。
 *
 * 业务流程说明（面向产品/运营的高层描述）:
 * - 启动时读取配置（优先从指定路径读取配置文件，否则使用内置默认值）。
 * - 建立一个循环（周期由配置项 period_ms/默认2秒决定），每次循环：
 *     1. 构造一条控制命令（格式："CMD <seq> <vx> <vy> <wz>"），其含义为请求设备以给定线速度(vx,vy)
 *        和角速度(wz)执行一次驱动动作，seq 用于识别请求序列号。此命令按文本协议发送到远端控制服务器。
 *     2. 与服务器建立短连接（TCP），发送命令并等待一次响应，然后关闭连接。
 *     3. 解析服务器响应（期望格式："STA <seq> <vx> <vy> <wz> <battery> <err>"），并将状态/电量/错误码按业务语义打印/记录。
 *     4. 如果连接或收发失败，打印警告并在下次循环重试（客户端不长连接、不重传具体命令，仅记录失败并继续）。
 * - 该程序为演示型客户端，侧重命令/状态的交互与简单错误提示，适合作为上层调试或集成测试工具。
 *
 * 关键业务注意点：
 * - 协议为行文本协议（以 "\n" 结尾），易于人工调试与日志收集；生产环境可迁移为二进制或更严格的帧协议。
 * - 序号 `seq` 用于检测消息乱序或重放；当响应中 seq 与请求不同时，会记录 WARN 以便排查网络问题。
 * - `battery` 为浮点数，表示百分比，程序打印到日志用于监控设备电量。
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define DEFAULT_CONFIG "/etc/base-client.conf"

typedef struct
{
    char server_ip[64];
    int server_port;
    int period_ms;
    double vx;
    double vy;
    double wz;
} AppConfig;

/*
 * set_default_config: 为业务运行设置安全的默认配置。
 * 业务含义：当没有外部配置文件时，客户端仍需能与默认模拟/测试服务器通信，
 * 因此填充合理的IP/端口/周期及默认运动指令参数，便于本地开发和CI环境使用。
 */
static void set_default_config(AppConfig *cfg)
{
    snprintf(cfg->server_ip, sizeof(cfg->server_ip), "10.0.2.2");
    cfg->server_port = 7000;
    cfg->period_ms = 2000;
    cfg->vx = 0.10;
    cfg->vy = 0.00;
    cfg->wz = 0.20;
}

/*
 * trim: 去除字符串两端的空白字符（空格、制表、换行等）。
 * 业务角度：读取配置文件或网络文本时会遇到额外空白，需清理以避免解析错误。
 */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
    {
        s++;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
    {
        *end = '\0';
        end--;
    }

    return s;
}

/*
 * load_config: 从文本配置文件加载业务参数。
 * 配置文件格式为 key=value，每行一个配置，可使用 '#' 注释行。
 * 支持的键：server_ip, server_port, period_ms, vx, vy, wz。
 * 业务影响：运维/测试可以通过配置文件调整目标服务器地址、循环周期和默认驱动参数，
 * 便于在不同环境下（模拟器、实机、CI）复用同一二进制。
 */
static int load_config(const char *path, AppConfig *cfg)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        perror("fopen config");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        char *text = trim(line);
        if (text[0] == '#' || text[0] == '\0')
        {
            continue;
        }
        char *eq = strchr(text, '=');
        if (!eq)
        {
            continue;
        }
        *eq = '\0';
        char *key = trim(text);
        char *value = trim(eq + 1);

        if (strcmp(key, "server_ip") == 0)
        {
            snprintf(cfg->server_ip, sizeof(cfg->server_ip), "%s", value);
        }
        else if (strcmp(key, "server_port") == 0)
        {
            cfg->server_port = atoi(value);
        }
        else if (strcmp(key, "period_ms") == 0)
        {
            cfg->period_ms = atoi(value);
        }
        else if (strcmp(key, "vx") == 0)
        {
            cfg->vx = atof(value);
        }
        else if (strcmp(key, "vy") == 0)
        {
            cfg->vy = atof(value);
        }
        else if (strcmp(key, "wz") == 0)
        {
            cfg->wz = atof(value);
        }
    }
    fclose(fp);
    return 0;
}

/*
 * err_to_text: 将业务错误码转换为易读文本。
 * 业务含义：服务端返回的错误码代表设备端或指令层面的异常。客户端将错误码转换为文本输出，
 * 方便日志聚合和人工排查，例如 LOW_BATTERY 表示设备电量低，应触发告警或回收策略。
 */
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
static int send_once(const AppConfig *cfg, int seq)
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
    server_addr.sin_port = htons(cfg->server_port); // 将端口转换为网络字节序

    // 将IP字符串转换为网络地址结构
    if (inet_pton(AF_INET, cfg->server_ip, &server_addr.sin_addr) != 1)
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

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "CMD %d %.2f %.2f %.2f\n", seq, cfg->vx, cfg->vy, cfg->wz);

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
    /*
     * send_once: 与控制服务器建立短连接，发送一次控制命令并等待状态响应。
     * 输入：配置（包含目标IP/端口和运动指令参数）和 seq（业务序号）。
     * 输出：0 表示发送并成功解析响应（或至少收到可解析的响应），-1 表示通信/解析失败。
     * 业务流程：
     *  1) 建立 TCP 连接 -> 2) 发送文本命令 CMD -> 3) 等待单次响应 STA -> 4) 解析并打印/记录 -> 5) 关闭连接
     * 注意：此处采用短连接策略（每次命令建立新连接），便于演示和避免长连接管理复杂性。
     */
    double rx_vx = 0.0;
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
    const char *config_path = DEFAULT_CONFIG;
    AppConfig cfg;
    int seq = 0;

    if (argc > 1)
    {
        config_path = argv[1];
    }

    set_default_config(&cfg);

    if (load_config(config_path, &cfg) != 0)
    {
        printf("WARN: use default config\n");
        fflush(stdout);
    }

    printf("base client loop started\n");
    printf("config:server_ip=%s server_port=%d period_ms=%d vx=%.2f vy=%.2f wz=%.2f\n",
           cfg.server_ip, cfg.server_port, cfg.period_ms, cfg.vx, cfg.vy, cfg.wz);
    fflush(stdout);

    while (1)
    {
        if (send_once(&cfg, seq) != 0)
        {
            printf("loop %d failed,will retry\n", seq);
            fflush(stdout);
        }
        seq++;
        sleep(2); // 每2秒发送一次
    }
    return 0;
}
