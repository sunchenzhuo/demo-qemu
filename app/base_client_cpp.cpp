/**
 * @文件路径         : /shu/projects/demo-qemu/app/base_client_cpp.cpp
 * @作者           : 树
 * @创建时间         : 2026-05-26 14:46:46
 * @最后编辑         : 树
 * @最后编辑时间       : 2026-05-27 17:16:25
 * @Version      : V1.0.0
 * @功能描述         : 基础客户端 C++ 实现
 * @Copyright    : Copyright (c) 2026 by 树, All Rights Reserved.
 */
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>

// 应用程序配置结构体
// 说明：此结构体保存客户端运行时所需的可配置参数及其默认值。
// 可由配置文件或命令行解析器覆盖这些默认值。
struct AppConfig
{
    std::string server_ip = "127.0.0.1"; // 服务器 IP，默认指向主机
    int server_port = 7000;              // 服务器端口
    int period_ms = 2000;                // 周期（毫秒）：客户端发送/轮询的时间间隔

    // 运动控制量（线速度 vx, vy，角速度 wz）
    // 这些值可能用于发送给远端控制器或模拟移动行为
    double vx = 0.10; // 前向速度（m/s）
    double vy = 0.00; // 侧向速度（m/s），默认无侧向运动
    double wz = 0.20; // 角速度（rad/s）

    std::string log_file = "/var/log/client_log.log"; // 日志文件路径，客户端将日志写入此文件（需要有写权限）
    int crash_after = -1;                             // 在多少次循环后模拟崩溃。-1 表示不触发崩溃。
    int max_fail_count = 3;                           // 最大失败重试次数（例如网络发送失败）

    // 安全速度：在出现故障或进入安全模式时使用的速度（通常设为 0）
    double safe_vx = 0.00;
    double safe_vy = 0.00;
    double safe_wz = 0.00;
};
// 定义状态数据结构体
struct Status
{
    int seq = -1; // 表示还没有收到有效序号
    double vx = 0.0;
    double vy = 0.0;
    double wz = 0.0;
    double battery_voltage = 0.0; // 电池电压，单位：V
    int err = -1;
};

// 把错误码 err 转换成能看懂的错误文本
std::string errToText(int err)
{
    switch (err)
    {
    case 0:
        return "OK"; // 正常，没有错误
    case 1:
        return "LOW_BATTERY"; // 低电压/低电量告警
    case 2:
        return "BAD_COMMOND"; // 错误命令
    case 3:
        return "SPEED_LIMIT"; // 发送的速度超过安全范围，服务端拒绝执行
    default:
        return "UNKNOW";
    }
}
// 去掉字符串开头和结尾的空白字符
std::string trim(const std::string &text)
{
    const std::string spaces = " \t\n\r"; // 字符串开头和结尾如果是 tab、换行、回车，就去掉。

    const std::size_t begin = text.find_first_not_of(spaces); // 找到第一个不是空白字符的位置

    // 判断字符串是不是全是空白字符
    if (begin == std::string::npos)
    {
        return "";
    }

    const std::size_t end = text.find_last_not_of(spaces); // 找到最后一个不是空白字符的位置
    return text.substr(begin, end - begin + 1);            // 截取中间有效字符串，substr 是截取字符串
}
// 解析状态数据
bool parseStatus(const std::string &text, Status &status)
{
    std::istringstream iss(text); // 创建字符串输入流

    std::string tag;                                                                                         // 保存状态消息开头的标识
    iss >> tag >> status.seq >> status.vx >> status.vy >> status.wz >> status.battery_voltage >> status.err; // 解析整行数据

    if (!iss || tag != "STA") //! iss输入流状态异常，也就是解析失败了
    {
        return false;
    }
    return true;
}
// 从配置文件加载应用程序配置
// 说明：此函数读取指定路径的配置文件，并解析其中的参数来覆盖 AppConfig 结构体中的默认值。
// 配置文件格式为 key=value，每行一个参数
bool loadConfig(const std::string &path, AppConfig &cfg)
{
    std::ifstream file(path); // 打开配置文件
    // 判断文件是否打开成功
    if (!file.is_open())
    {
        std::cerr << "open config failed:" << path << std::endl;
        return false;
    }

    std::string line;
    // 逐行读取配置文件
    while (std::getline(file, line))
    {
        line = trim(line);

        if (line.empty() || line[0] == '#') // 跳过空行和注释行
        {
            continue;
        }

        const std::size_t pos = line.find('='); // 查找等号=
        if (pos != std::string::npos)
        {
            // 拆出 key 和 value
            const std::string key = trim(line.substr(0, pos));
            const std::string value = trim(line.substr(pos + 1));

            if (key == "server_ip")
            {
                cfg.server_ip = value;
            }
            else if (key == "server_port")
            {
                cfg.server_port = std::stoi(value);
            }
            else if (key == "period_ms")
            {
                cfg.period_ms = std::stoi(value);
            }
            else if (key == "vx")
            {
                cfg.vx = std::stod(value);
            }
            else if (key == "vy")
            {
                cfg.vy = std::stod(value);
            }
            else if (key == "wz")
            {
                cfg.wz = std::stod(value);
            }
            else if (key == "log_file")
            {
                cfg.log_file = value;
            }

            else if (key == "crash_after")
            {
                cfg.crash_after = std::stoi(value);
            }
            else if (key == "max_fail_count")
            {
                cfg.max_fail_count = std::stoi(value);
            }

            else if (key == "safe_vx")
            {
                cfg.safe_vx = std::stod(value);
            }
            else if (key == "safe_vy")
            {
                cfg.safe_vy = std::stod(value);
            }
            else if (key == "safe_wz")
            {
                cfg.safe_wz = std::stod(value);
            }
        }
    }
    return true;
}

// 打印当前配置参数
void printConfig(const AppConfig &cfg)
{
    std::cout << "server_ip:" << cfg.server_ip << std::endl;
    std::cout << "server_port:" << cfg.server_port << std::endl;
    std::cout << "period_ms:" << cfg.period_ms << std::endl;
    std::cout << "vx:" << cfg.vx << std::endl;
    std::cout << "vy:" << cfg.vy << std::endl;
    std::cout << "wz:" << cfg.wz << std::endl;
    std::cout << "log_file:" << cfg.log_file << std::endl;
    std::cout << "crash_after:" << cfg.crash_after << std::endl;
    std::cout << "max_fail_count:" << cfg.max_fail_count << std::endl;
    std::cout << "safe_vx:" << cfg.safe_vx << std::endl;
    std::cout << "safe_vy:" << cfg.safe_vy << std::endl;
    std::cout << "safe_wz:" << cfg.safe_wz << std::endl;
}

// 创建一个 TCP 客户端，连接服务器，发送一条控制命令，然后接收服务器返回的数据，最后关闭连接
bool sendOnce(const AppConfig &cfg, int seq)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0); // 创建 socket, AF_INET 表示 IPv4，SOCK_STREAM 表示 TCP
    // 判断 socket 是否创建成功
    if (sock < 0)
    {
        std::cerr << "socket failed" << std::endl;
        return false;
    }

    sockaddr_in server_addr{};                     // 创建 socket地址结构体，存储服务器的 IP 和端口信息
    server_addr.sin_family = AF_INET;              // IPv4
    server_addr.sin_port = htons(cfg.server_port); // 服务器端口，htons 将主机字节序转换为网络字节序
    // 将服务器 IP 地址从字符串转换为二进制形式，并存储在 server_addr.sin_addr 中
    if (inet_pton(AF_INET, cfg.server_ip.c_str(), &server_addr.sin_addr) != 1)
    {
        std::cerr << "bad server ip:" << cfg.server_ip << std::endl;
        close(sock);
        return false;
    }
    // TCP 客户端连接服务器
    if (connect(sock, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    {
        std::cerr << "connect failed:" << cfg.server_ip << ":" << cfg.server_port << std::endl;
        close(sock);
        return false;
    }
    // 拼接要发送的命令
    std::ostringstream cmd; // 字符串输出流
    cmd << "CMD " << seq << " " << cfg.vx << " " << cfg.vy << " " << cfg.wz << "\n";

    const std::string cmd_text = cmd.str(); // 获取最终字符串
    std::cout << "TX:" << cmd_text;

    // 把命令发给服务器
    if (send(sock, cmd_text.c_str(), cmd_text.size(), 0) < 0)
    {
        std::cerr << "send failed" << std::endl;
        close(sock);
        return false;
    }

    char buffer[256]{};                                      // 定义一个字符数组，用来接收服务器返回的数据
    const int n = recv(sock, buffer, sizeof(buffer) - 1, 0); // 接收服务器回复
    // 判断接收是否失败
    if (n <= 0)
    {
        std::cerr << "recv faild or server closed" << std::endl;
        close(sock);
        return false;
    }

    // 给 buffer 加字符串结束符
    buffer[n] = '\0';
    std::string response = buffer;
    std::cout << "RX:" << response;

    Status status; // 创建一个 Status 对象，用来保存解析后的状态数据
    // 解析服务器返回的数据
    if (!parseStatus(response, status))
    {
        // 服务器返回格式不对，关闭 socket，本次发送失败
        std::cerr << "bad response format" << std::endl;
        close(sock);
        return false;
    }

    // 检查序号是否一致
    if (status.seq != seq)
    {
        std::cerr << "seq mismatch,tx_seq=" << seq << ",rx_seq=" << status.seq << std::endl;
        close(sock);
        return false;
    }

    std::cout << "STATUS:"
              << "vx=" << status.vx
              << ",vy=" << status.vy
              << ",wz=" << status.wz
              << ",battery_voltage=" << status.battery_voltage
              << ",err=" << errToText(status.err)
              << std::endl;

    close(sock);
    return true;
}

/* 确定配置文件路径
   加载配置
   打印配置
   进入周期发送循环
   发送失败时累计失败次数
   连续失败过多后进入安全模式
   成功后清空失败状态
   按 period_ms 周期休眠 */
int main(int argc, char const *argv[])
{
    std::string config_path = "/etc/base-client.conf";
    if (argc > 1)
    {
        config_path = argv[1];
    }

    AppConfig cfg; // 创建配置对象

    // 配置文件失败就直接退出
    if (!loadConfig(config_path, cfg))
    {
        std::cerr << "bad config fail: " << config_path << std::endl;
        return 1;
    }

    printConfig(cfg);

    // 周期性发送循环：从 seq=0 开始，不停调用 sendOnce(cfg, seq) 给服务器发送命令，每发一次序号加 1，然后按照配置里的 period_ms 休眠一段时间
    int seq = 0;                   // 命令序号，从 0 开始，每发送一轮加 1
    int fail_count = 0;            // 连续失败次数
    bool was_disconnected = false; // 判断是否经历过断线状态
    bool safe_mode = false;

    /*发送一次
    判断成功/失败
    更新 fail_count 和 was_disconnected
    seq 加 1
    睡眠 period_ms 毫秒
    进入下一轮*/
    while (true)
    {
        if (cfg.crash_after >= 0 && seq >= cfg.crash_after)
        {
            std::cerr << "simulate crash after seq=" << seq << std::endl;
            return 2; // 程序异常退出,以后 systemd 看到非 0 退出码，就会配合：Restart=on-failure
        }

        bool ok = sendOnce(cfg, seq);
        // 通信失败时的逻辑
        if (!ok)
        {
            fail_count++;
            was_disconnected = true;

            std::cerr << "communication failed,seq=" << seq
                      << ",fail_count=" << fail_count
                      << ",will retry" << std::endl;
            // 连续失败超过一定次数后进入安全模式
            if (fail_count >= cfg.max_fail_count && !safe_mode)
            {
                safe_mode = true;
                std::cerr << "too many communication failures,enter safe mode"
                          << ",safe_cmd="
                          << cfg.safe_vx << " "
                          << cfg.safe_vy << " "
                          << cfg.safe_wz
                          << std::endl;
                break;
            }
        }
        // 通信成功时的逻辑
        else
        {
            if (was_disconnected)
            {
                std::cout << "communication recovered , seq=" << seq
                          << ", after" << fail_count
                          << " failures" << std::endl;
            }
            if (safe_mode)
            {
                std::cout << "leave safe mode" << std::endl;
            }

            fail_count = 0;
            was_disconnected = false;
            safe_mode = false;
        }

        seq++;
        // std::chrono::milliseconds()把一个数字包装成“毫秒时间长度”
        // std::this_thread::sleep_for()让当前线程睡眠一段时间
        // std::chrono是 C++ 标准库里的时间工具命名空间。
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.period_ms)); // 当前线程睡眠一段时间
    }

    return 0;
}
