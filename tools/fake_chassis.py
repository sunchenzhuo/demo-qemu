#!/usr/bin/env python3
import os
import time

# 假底盘使用的伪 TTY 设备文件路径。
PORT = "/tmp/ttyV1"
# 每次响应中返回的固定电池电量。
BATTERY = 12.30

def main():
    print(f"fake chassis opened {PORT}")

    # 以读写模式打开设备。O_NOCTTY 防止该 TTY 成为控制终端。
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)

    # 将接收的数据组装为完整行。
    buffer = b""

    try:
        while True:
            data = os.read(fd, 128)
            if not data:
                # 当前没有可读数据，避免忙循环。
                time.sleep(0.1)
                continue

            buffer += data

            while b"\n" in buffer:
                # 提取每个以换行符终止的完整行。
                line, buffer = buffer.split(b"\n", 1)
                text = line.decode(errors="ignore").strip()

                if not text:
                    continue

                print(f"RX: {text}")

                parts = text.split()
                if len(parts) == 4 and parts[0] == "CMD":
                    # 解析期望的命令格式：CMD vx vy wz
                    vx = float(parts[1])
                    vy = float(parts[2])
                    wz = float(parts[3])

                    # 返回接收到的速度值和电池状态。
                    response = f"VEL {vx:.2f} {vy:.2f} {wz:.2f} BAT {BATTERY:.2f}\n"
                else:
                    # 对于未知或格式错误的命令返回错误信息。
                    response = "ERR bad command\n"

                os.write(fd, response.encode())
                print(f"TX: {response.strip()}")

    except KeyboardInterrupt:
        # 捕获 Ctrl+C 并干净退出。
        print("fake chassis stopped")
    finally:
        # 退出时确保关闭文件描述符。
        os.close(fd)

if __name__ == "__main__":
    main()