#!/usr/bin/env python3
import socket

# 监听地址和端口
HOST = "0.0.0.0"
PORT = 7000
# 模拟电池电量
BATTERY = 12.30


def handle_line(line):
    # 预处理命令行并按空格拆分
    parts = line.strip().split()

    # 命令格式必须为 CMD vx vy wz
    if len(parts) != 4 or parts[0] != "CMD":
        return "ERR bad command\n"

    try:
        vx = float(parts[1])
        vy = float(parts[2])
        wz = float(parts[3])
    except ValueError:
        # 输入参数必须是浮点数
        return "ERR bad number\n"

    # 返回速度和电池状态
    return f"VEL {vx:.2f} {vy:.2f} {wz:.2f} BAT {BATTERY:.2f}\n"


def main():
    # 启动模拟底盘 TCP 服务器
    print(f"fake chassis tcp server listening on {HOST}:{PORT}")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)

        while True:
            # 等待客户端连接
            conn, addr = server.accept()
            print(f"Client connected from {addr}")
            with conn:
                buffer = b""
                while True:
                    # 从客户端读取数据
                    data = conn.recv(128)
                    if not data:
                        print("Client disconnected")
                        break
                    buffer += data

                    # 按行处理命令
                    while b"\n" in buffer:
                        raw_line, buffer = buffer.split(b"\n", 1)
                        line = raw_line.decode(errors="ignore").strip()

                        if not line:
                            continue
                        print(f"RX:{line}")
                        response = handle_line(line)
                        conn.sendall(response.encode())
                        print(f"TX:{response.strip()}")

if __name__=="__main__":
    # 仅在脚本直接执行时启动服务器
    main()