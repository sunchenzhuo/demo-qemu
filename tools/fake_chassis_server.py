'''
 @文件路径         : /shu/projects/demo-qemu/tools/fake_chassis_server.py
 @作者           : 树
 @创建时间         : 2026-05-20 15:13:55
 @最后编辑         : 树
 @最后编辑时间       : 2026-05-21 16:56:26
 @Version      : V1.0.0
 @功能描述         : 模拟底盘 TCP 控制服务器，用于接收控制命令并返回状态信息。
 @Copyright    : Copyright (c) 2026 by 树, All Rights Reserved.
'''
#!/usr/bin/env python3
import socket

# 这个脚本实现了一个简单的模拟底盘 TCP 服务器，适用于测试底盘控制协议。
# 它监听固定端口，接收按行传输的控制命令，返回状态响应，并维护一个简单的电量状态。

# 监听的网络地址和端口
HOST = "0.0.0.0"  # 接受来自任意网络接口的连接
PORT = 7000       # 约定的 TCP 端口号

# 运行时电池状态
battery = 12.30  # 当前电池电量（会在每次命令处理时递减）

# 模拟底盘的参数限制
LOW_BATTERY = 10.60  # 低电量阈值
MAX_VX = 1.00        # X 方向最大速度
MAX_VY = 1.00        # Y 方向最大速度
MAX_WZ = 1.00        # 角速度最大值

# 协议中的错误码定义
ERR_OK = 0            # 正常
ERR_LOW_BATTERY = 1   # 低电量警告
ERR_BAD_COMMAND = 2   # 命令格式或参数错误
ERR_SPEED_LIMIT = 3   # 速度超过限制


def handle_line(line):
    """处理单条客户端命令并生成响应字符串。

    参数:
        line (str): 客户端发送的一行文本命令，通常以换行符结尾。

    返回:
        str: 服务器返回给客户端的响应字符串，包含状态码和当前电量。
    """
    global battery

    # 清除首尾空白并拆分各个字段
    parts = line.strip().split()

    # 命令协议要求 5 个字段：CMD seq vx vy wz
    if len(parts) != 5 or parts[0] != "CMD":
        # 只要命令不匹配协议，就直接返回错误响应
        return f"STA -1 0.00 0.00 0.00 {battery:.2f} {ERR_BAD_COMMAND}\n"

    try:
        # seq：命令序号，用于客户端和服务器端跟踪请求/响应关系
        seq = int(parts[1])
        vx = float(parts[2])
        vy = float(parts[3])
        wz = float(parts[4])
    except ValueError:
        # 字段无法转换为数字，认为命令参数无效
        return f"STA -1 0.00 0.00 0.00 {battery:.2f} {ERR_BAD_COMMAND}\n"

    err = ERR_OK

    # 如果任一速度超过允许范围，则拒绝该命令并返回限速错误
    if abs(vx) > MAX_VX or abs(vy) > MAX_VY or abs(wz) > MAX_WZ:
        err = ERR_SPEED_LIMIT
        vx = 0.0
        vy = 0.0
        wz = 0.0

    # 模拟电池消耗：每次处理命令时减少固定电量
    battery -= 0.01
    if battery < LOW_BATTERY and err == ERR_OK:
        # 只有在命令本身合法时，才返回低电量警告
        err = ERR_LOW_BATTERY

    # 响应语法：STA seq vx vy wz battery err\n
    return f"STA {seq} {vx:.2f} {vy:.2f} {wz:.2f} {battery:.2f} {err}\n"


def main():
    """启动 TCP 服务器并循环接收客户端连接与命令。"""
    print(f"fake chassis tcp server listening on {HOST}:{PORT}", flush=True)

    # 创建 TCP socket，并在 with 块结束时自动关闭
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        # 允许地址重用，避免程序重启后端口被长时间占用
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        # 绑定到指定地址和端口
        server.bind((HOST, PORT))

        # 开始监听，backlog=1 表示最多保持一个待处理的连接队列
        server.listen(1)

        while True:
            # 阻塞等待客户端连接；返回连接套接字和客户端地址
            conn, addr = server.accept()
            print(f"Client connected from {addr}", flush=True)

            # 使用 with 保障连接关闭和资源释放
            with conn:
                buffer = b""  # 用于存储从 socket 读取的未处理字节
                while True:
                    # 从客户端读取数据，最多 128 字节
                    data = conn.recv(128)
                    if not data:
                        # 对端关闭连接时退出当前连接循环
                        print("Client disconnected", flush=True)
                        break

                    # 累积读取到的字节，可能包含多条命令或不完整命令
                    buffer += data

                    # 以换行符为边界，逐行提取完整命令
                    while b"\n" in buffer:
                        raw_line, buffer = buffer.split(b"\n", 1)
                        # 忽略解码错误，避免因非法字节导致整个连接崩溃
                        line = raw_line.decode(errors="ignore").strip()

                        if not line:
                            # 跳过空行
                            continue

                        print(f"RX:{line}", flush=True)
                        response = handle_line(line)
                        # 将响应发送回客户端，必须使用字节形式
                        conn.sendall(response.encode())
                        print(f"TX:{response.strip()}", flush=True)


if __name__ == "__main__":
    # 当脚本作为主程序执行时启动 main()，而 import 时不自动运行服务器
    main()