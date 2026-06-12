import serial
import threading
import math
import queue
import time
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import Button

# ================= CẤU HÌNH HỆ THỐNG =================
SERIAL_PORT = 'COM4'     # Đảm bảo cổng COM khớp với mạch của bạn
BAUD_RATE = 9600

STEP_DISTANCE = 1.5      # Quãng đường mỗi bước vẽ (đi thẳng)
START_ANGLE = 90.0       # Hướng xuất phát (90 độ = hướng mũi xe lên trên)
TURN_COOLDOWN = 1.0      # Thời gian "đóng băng" (giây) chống nhận diện rẽ kép

# ================= QUẢN LÝ TỌA ĐỘ & TRẠNG THÁI =================
cmd_queue = queue.Queue()

x_data, y_data = [0.0], [0.0]
current_x, current_y = 0.0, 0.0
current_angle = START_ANGLE

last_turn_time = 0.0     # Mốc thời gian của lần bẻ lái gần nhất

# =========================================GIAI ĐOẠN 3=====================================================================
# HÀM DI CHUYỂN VÀ RẼ XE (CẬP NHẬT TỌA ĐỘ VÀ GÓC HƯỚNG)
# Tính toán tọa độ tịnh tiến
def move_forward(sensors):
    global current_x, current_y
    rad = math.radians(current_angle)

    current_x += STEP_DISTANCE * math.cos(rad)
    current_y += STEP_DISTANCE * math.sin(rad)

    x_data.append(current_x)
    y_data.append(current_y)
   
    print(f"⬆️ Đi thẳng | Sensor: {sensors} | x={current_x:.1f}, y={current_y:.1f}, angle={current_angle:.0f}°")

# Tính toán tọa độ góc quay
def turn_left():
    global current_angle, last_turn_time
    current_angle = (current_angle + 90) % 360
    last_turn_time = time.time()  # Kích hoạt khiên chống nhiễu
    print(f"↩️ Rẽ trái (Góc vuông) | Góc hiện tại: {current_angle}°")


def turn_right():
    global current_angle, last_turn_time
    current_angle = (current_angle - 90) % 360
    last_turn_time = time.time()  # Kích hoạt khiên chống nhiễu
    print(f"↪️ Rẽ phải (Góc vuông) | Góc hiện tại: {current_angle}°")
# ==============================KẾT THÚC GIAI ĐOẠN 3=====================================================================

# ---------------------------------------------------------------------------------------------------
# ======================================GIAI ĐOẠN 2=================================================
# PHÂN TÍCH LOGIC VÀ RA QUYẾT ĐỊNH
# ==================================================================================================
def process_command(line):
    # Chỉ xử lý dữ liệu chuẩn từ STM32 gửi lên (VD: "Sen:00100 | L:90 R:90")

    # BỘ LỌC TÍN HIỆU (CHỈ NHẬN CHUỖI CHUẨN "SEN:")
    # =================================================
    if not line.startswith("Sen:"):
        return
    # =================================================

    try:

        # BỘ TÁCH TRẠNG THÁI 5 MẮT CẢM BIẾN
        # =================================================================================
        sensors = line[4:9]
        L2, L1, C, R1, R2 = sensors[0], sensors[1], sensors[2], sensors[3], sensors[4]
        # =================================================================================
    except IndexError:
        return

    # TÍNH TOÁN THỜI GIAN ĐÓNG BĂNG  
    # =========================================================
    current_time = time.time()
    can_turn = (current_time - last_turn_time) > TURN_COOLDOWN
    # =========================================================

    # ================= LOGIC RẼ (Mê cung) =================
    # Chỉ cho phép rẽ nếu đã hết thời gian Cooldown
    if can_turn:
        if L2 == '1':
            turn_left()
            return

        if R2 == '1' and L2 == '0':
            turn_right()
            return
    # ======================================================

    # ================= LOGIC ĐI THẲNG =================
    # Đi thẳng khi 1 trong 3 mắt giữa thấy vạch
    if C == '1' or L1 == '1' or R1 == '1':
        move_forward(sensors)
    # =================================================
# =================================KẾT THÚC GIAI ĐOẠN 2=================================================

def reset_map(event):
    global current_x, current_y, current_angle, last_turn_time
    global x_data, y_data

    # Đưa xe về lại vạch xuất phát
    current_x, current_y = 0.0, 0.0
    current_angle = START_ANGLE
    last_turn_time = 0.0

    # Xóa trắng lịch sử đường vẽ
    x_data.clear()
    y_data.clear()
    x_data.append(0.0)
    y_data.append(0.0)

    # Xóa các lệnh cũ đang kẹt trong hàng đợi
    with cmd_queue.mutex:
        cmd_queue.queue.clear()

    # Reset lại khung nhìn mặc định
    ax.set_xlim(-10, 10)
    ax.set_ylim(-10, 10)
    fig.canvas.draw_idle()

    print("\n🔄 ĐÃ RESET BẢN ĐỒ VỀ VẠCH XUẤT PHÁT\n")


def run_serial():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"🟢 Đã kết nối thành công với cổng {SERIAL_PORT}")

        buffer = ""
       

        # ===================================GIAI ĐOẠN 1 (TIẾP)================================
        # ĐỌC VÀ XỬ LÝ CHUỖI TỪ CỔNG SERIAL
        # ---------------------------------------------------------------------------
        while True:
            if ser.in_waiting > 0:
                buffer += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")

                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if line.strip():
                        cmd_queue.put(line.strip())
        # -------------------------------KẾT THÚC GIAI ĐOẠN 1 (TIẾP)--------------------------------
            time.sleep(0.01)

    except Exception as e:
        print(f"🔴 Lỗi kết nối Serial: Cổng {SERIAL_PORT} chưa mở hoặc đang bị chiếm dụng. Chi tiết: {e}")

# =====================================GIAI ĐOẠN 4=================================================
# ================= ĐỒ HỌA MATPLOTLIB =================
fig, ax = plt.subplots(figsize=(8, 6))
fig.canvas.manager.set_window_title("Live Maze Mapping - STM32")
plt.subplots_adjust(bottom=0.2)

# Khởi tạo đường vẽ và điểm đỏ (xe)
line_plot, = ax.plot([], [], 'b-', linewidth=3, label="Quỹ đạo giải mê cung")
dot = ax.scatter([], [], color='red', zorder=5, s=60)

ax.legend()
ax.grid(True, linestyle='--', alpha=0.6)
ax.set_aspect('equal', adjustable='box')
ax.set_xlim(-10, 10)
ax.set_ylim(-10, 10)

# Khởi tạo nút Reset
ax_reset = plt.axes([0.4, 0.05, 0.2, 0.075])
btn_reset = Button(ax_reset, 'Reset Map', color='lightcoral', hovercolor='red')
btn_reset.on_clicked(reset_map)


def update(frame):
    # Lôi toàn bộ lệnh trong hàng đợi ra xử lý
    while not cmd_queue.empty():
        process_command(cmd_queue.get())

    # Cập nhật dữ liệu lên đồ thị
    line_plot.set_data(x_data, y_data)
    dot.set_offsets([[current_x, current_y]])

    # Tự động nới rộng khung nhìn nếu xe chạy ra khỏi màn hình
    if len(x_data) > 1:
        margin = 15
        ax.set_xlim(min(x_data) - margin, max(x_data) + margin)
        ax.set_ylim(min(y_data) - margin, max(y_data) + margin)

    return line_plot, dot



# --------------------GIAI ĐOẠN 1----------------------
# Bật luồng đọc Bluetooth chạy ngầm / KHỞI TẠO LUỒNG CHẠY NGẦM
threading.Thread(target=run_serial, daemon=True).start()
# --------------------KẾT THÚC GIAI ĐOẠN 1--------------------------------


# Kích hoạt vòng lặp vẽ đồ họa (50ms/lần)
ani = animation.FuncAnimation(
    fig,
    update,
    interval=50,
    cache_frame_data=False
)

plt.show()
# ====================================KẾT THÚC GIAI ĐOẠN 4=================================================

