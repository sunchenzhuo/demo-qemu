/**
 * @文件路径         : /shu/projects/demo-qemu/app/base_client_loop.c
 * @作者           : 树
 * @创建时间         : 2026-05-21 14:16:07
 * @最后编辑         : 树
 * @最后编辑时间       : 2026-05-25 15:59:31
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
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>

#define DEFAULT_CONFIG "/etc/base-client.conf"
/*
 * AppConfig: 保存运行时可配置的业务参数。
 * - server_ip/server_port: 目标控制服务器的地址和端口。
 * - period_ms: 主循环周期（毫秒），当前实现中示例使用固定 sleep(2)，
 *   若需要严谨周期控制应使用基于时间的调度以避免 drift。
 * - vx/vy/wz: 业务层的线速度/角速度指令（默认值用于演示或临时调试）。
 * - log_file: 可选日志文件路径，程序可将日志追加到此文件以便长期存储。
 */
typedef struct
{
    char server_ip[64];
    int server_port;
    int period_ms;
    double vx;
    double vy;
    double wz;
    char log_file[128];
    int crash_after; // 业务层面未使用，预留给未来可能的崩溃日志或核心转储路径
} AppConfig;

static FILE *g_log_fp = NULL; /* 全局日志文件指针；若成功打开则向磁盘追加日志 */
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
    cfg->crash_after = -1;
    snprintf(cfg->log_file, sizeof(cfg->log_file), "/var/log/base_client.log");
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
    if (*s == '\0')
    {
        return s;
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
        else if (strcmp(key, "crash_after") == 0)
        {
            cfg->crash_after = atoi(value);
        }
    }
    fclose(fp);
    return 0;
}

static int open_log_file(const char *path)
{
    g_log_fp = fopen(path, "a");
    if (!g_log_fp)
    {
        perror("fopen log");
        return -1;
    }
    return 0;
}

static void close_log_file(void)
{
    if (g_log_fp)
    {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

static void get_time_string(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);
}

static void log_msg(const char *level, const char *fmt, ...)
{
    char time_buf[32];
    char msg_buf[512];
    get_time_string(time_buf, sizeof(time_buf));
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    /*
     * open_log_file: 打开日志文件（追加模式）并设置全局文件指针。
     * 业务目的：将运行日志持久化到磁盘，便于离线分析与故障排查。
     * 返回值：0 表示成功，-1 表示打开失败（已通过 perror 打印错误原因）。
     * 注意事项：
     *  - 使用追加模式("a")会在每次写入时自动移动到文件尾，适合单进程多次追加场景。
     *  - 本函数非线程安全；若在多线程环境中写日志，请使用互斥或选择线程安全的日志库。
     *  - 未实现日志轮转或大小限制，生产环境需配合外部日志轮转（logrotate）或在程序内实现轮转逻辑。
     */

    /* 将格式化后的日志输出到 stdout；同时可选写入文件（若 g_log_fp 非空）。
     * 业务说明：标准输出便于交互调试与容器日志采集，文件写入用于长期保存。
     * 实现细节与限制：
     *  - 使用 vsnprintf 将可变参数格式化到固定大小缓冲区 msg_buf（512 字节）。
     *    返回值可用于检测截断：若返回值 < 0 表示编码错误，若返回值 >= sizeof(msg_buf) 表示输出被截断。
     *  - 当前实现没有检查 vsnprintf 的返回值；在日志非常长或参数异常时可能导致截断且信息丢失。
     *  - 为保证日志按时间顺序写入并能被外部系统及时收集，调用后立即 flush（stdout 和文件）。
     */
    printf("%s [%s] %s\n", time_buf, level, msg_buf);
    fflush(stdout);

    if (g_log_fp)
    {
        fprintf(g_log_fp, "%s [%s] %s\n", time_buf, level, msg_buf);
        fflush(g_log_fp);
    }
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
        /* strerror 返回字符串，格式化时应使用 %s */
        log_msg("ERROR", "socket failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cfg->server_port); // 将端口转换为网络字节序

    // 将IP字符串转换为网络地址结构
    if (inet_pton(AF_INET, cfg->server_ip, &server_addr.sin_addr) != 1)
    {
        log_msg("ERROR", "inet_pton failed for ip=%s", cfg->server_ip);
        close(sock);
        return -1;
    }

    // 连接服务器
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        log_msg("ERROR", "connect %s:%d failed:%s", cfg->server_ip, cfg->server_port, strerror(errno));
        close(sock);
        return -1;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "CMD %d %.2f %.2f %.2f\n", seq, cfg->vx, cfg->vy, cfg->wz);

    log_msg("INFO", "loop=%d TX: %s", seq, cmd);

    /* 发送命令到服务器
     * 注意：send 可能返回已发送字节数小于请求长度（部分发送），
     * 对于短文本命令这通常不会发生，但严格实现应循环发送直到所有字节发送完毕或出错。
     */
    if (send(sock, cmd, strlen(cmd), 0) < 0)
    {
        log_msg("ERROR", "send failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    char buf[256];
    memset(buf, 0, sizeof(buf));

    /* 接收服务器响应（单次 recv）
     * 说明：单次 recv 可能读到部分报文或包含多条报文；鉴于协议为行文本，理想做法是循环读取直到遇到 '\n'。
     * 本示例为简化逻辑仅做一次 recv，适用于服务端一次性返回且数据量较小的场景。
     */
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n < 0)
    {
        log_msg("ERROR", "recv failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    if (n == 0)
    {
        // 服务器关闭了连接
        log_msg("WARN", "loop %d server closed connection", seq);
        close(sock);
        return -1;
    }

    buf[n] = '\0'; // 确保字符串以null结尾
    log_msg("INFO", "loop %d RX: %s", seq, buf);

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
            log_msg("WARN", "loop %d WARN: seq mismatch,rx_seq=%d", seq, rx_seq);
        }

        /*
         * 输出时注意类型：rx_vx/ry_vy/rx_wz 为 double，battery 为 double。
         * 原代码以 %d 打印 battery，会截断并导致错误显示，改为打印浮点或四舍五入为整数。
         */
        log_msg("INFO", "loop %d STATUS: vx=%.2f,vy=%.2f,wz=%.2f,battery=%.2f%%,err=%s", seq, rx_vx, rx_vy, rx_wz, battery, err_to_text(err));

        if (err == 1)
        {
            log_msg("WARN", "loop=%d low battery :%.2f", seq, battery);
        }
        else if (err != 0)
        {
            log_msg("ERROR", "loop=%d chassis error :%s", seq, err_to_text(err));
        }
    }
    else
    {
        log_msg("WARN", "loop %d WARN: bad response format", seq);
    }
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
        log_msg("WARN", "use default config");
        fflush(stdout);
    }

    log_msg("INFO", "base client loop started");
    log_msg("INFO", "config:server_ip=%s server_port=%d period_ms=%d vx=%.2f vy=%.2f wz=%.2f", cfg.server_ip, cfg.server_port, cfg.period_ms, cfg.vx, cfg.vy, cfg.wz);
    fflush(stdout);

    int fail_count = 0;       // 连续失败次数
    int was_disconnected = 0; // 之前是否处于断链状态

    /* 主循环：每次调用 send_once 发送一次命令并等待响应。
     * 说明：
     * - 本循环为演示用途，使用固定的 sleep(2) 间隔发送命令；生产环境应使用基于时间的调度
     *  （例如使用 cfg.period_ms + clock_gettime/nanosleep）来避免累积漂移并提高精度。
     * - 该循环包含一个可选的崩溃模拟分支（cfg.crash_after），用于测试程序在异常退出时的行为。
     */
    while (1)
    {
        /* 崩溃模拟：当 seq 达到 cfg.crash_after（>0）时，模拟异常退出以便生成日志/核心转储用于调试。
         * 业务影响：这是测试/开发时的辅助开关，生产环境不要依赖该行为；若需要生成 core 文件
         * 请改用 abort() 并在运行环境启用 core dumps。
         */
        if (cfg.crash_after > 0 && seq >= cfg.crash_after)
        {
            log_msg("ERROR", "simulate crash after seq=%d", seq);
            /* 先 flush stdout，再关闭日志文件，确保所有日志写入磁盘后再退出 */
            fflush(stdout);
            close_log_file();
            /* 返回非0状态表示异常退出；调用方或上层监控可据此触发重启或上报 */
            return 2;
        }

        /* 发送一次控制命令并处理响应。send_once 内部已完成与服务器的短连接交互。
         * - 成功时返回 0，并已在内部打印状态（包括 battery）。
         * - 失败时返回非0，此处仅记录警告并在下次循环重试（不做重传或停机）。
         */
        /*
         * 处理一次 send_once 的返回结果：
         * - 如果返回非0，表示本次通信（连接/发送/接收/解析）失败：
         *     1) 将连续失败计数 `fail_count` 增加；
         *     2) 将 `was_disconnected` 置为 1，用于记录当前处于断链/失败状态；
         *     3) 打印 WARN 日志并立即 flush，以便能够尽快看到错误信息；
         *     4) 下次循环继续重试（该客户端不做重传，仅记录失败）。
         * - 如果返回0，表示本次通信成功：
         *     1) 若之前处于断链状态（`was_disconnected` 为真），打印 INFO 日志说明通信已恢复，日志中包含恢复时的循环序号和之前的连续失败次数；
         *     2) 将 `fail_count` 重置为 0，清除 `was_disconnected` 标志，恢复正常计数。
         */
        if (send_once(&cfg, seq) != 0)
        {
            /* 通信失败，记录并标记为断链状态 */
            fail_count++;
            was_disconnected = 1;

            log_msg("WARN", "loop %d communication failed,fail_count=%d,will retry", seq, fail_count);
            fflush(stdout);
        }
        else
        {
            /* 通信成功，若之前断链则记录恢复信息 */
            if (was_disconnected)
            {
                log_msg("INFO", "communication recovered at loop=%d after %d failures", seq, fail_count);
                fflush(stdout);
            }

            /* 重置失败计数和断链标志，恢复正常状态 */
            fail_count = 0;
            was_disconnected = 0;
        }

        /* 序号递增：用于下一次请求的识别，与服务端返回的 seq 做对比以检测乱序/重放 */
        seq++;

        /* 固定睡眠：每次循环等待固定时间后再发下一次请求（演示用）。生产环境请基于 cfg.period_ms
         * 实现更精确的周期控制。sleep(2) 为阻塞调用，会影响响应及时性，但实现简单。
         */
        sleep(2); // 每2秒发送一次（演示用）
    }
    close_log_file();
    return 0;
}
