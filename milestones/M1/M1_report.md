# Milestone Report — M1

## 1. Metadata
- **Project:** Bluetooth Dijkstra Mesh
- **Milestone:** M1 — One Zephyr node: threads + internal queue (chưa BLE)
- **Trạng thái:** **PASS**

## 2. Objective & Scope
**Mục tiêu:** Dựng hạ tầng RTOS cơ bản (TX thread, RX thread, message queue) làm nền cho M2+ — nơi message queue nội bộ sẽ được thay bằng transport BLE thật — đồng thời xác nhận toolchain và cơ chế Kconfig hoạt động đúng trước khi thêm độ phức tạp của BLE.

**Definition of Done:**
- Build sạch (pristine), script chạy lại từ đầu không cần thao tác tay
- Log `Node ID = X` (từ `CONFIG_APP_NODE_ID`) in đúng ngay sau boot banner
- TX log gửi mỗi ~1s (`TX_PERIOD_MS`) với counter tăng dần
- RX log nhận đúng giá trị counter tương ứng, không mất/trùng gói tin qua nhiều chu kỳ liên tiếp

**Ngoài scope:** BLE (advertising/scanning), packet struct chính thức, Dijkstra, mutex (chưa có shared state nào cần bảo vệ ở M1).

## 3. Design Decisions

| Decision | Alternatives đã cân nhắc | Rationale |
|---|---|---|
| Static thread creation qua `K_THREAD_DEFINE` | `k_thread_create()` tạo động | Ít boilerplate hơn, compiler/linker tự đăng ký thread với kernel tại boot; hỗ trợ deferred start (`delay=K_FOREVER` + `k_thread_start()`) khi cần chờ BLE sẵn sàng ở milestone sau. (Lưu ý sửa: lý do ban đầu "tránh heap fragmentation" không chính xác — `k_thread_create()` mặc định vẫn dùng stack tĩnh qua `K_THREAD_STACK_DEFINE`, chỉ khác thời điểm gọi tại runtime; heap chỉ liên quan khi bật `CONFIG_DYNAMIC_THREAD_STACK_SIZE`, không dùng ở project này) |
| `NODE_ID` cấu hình qua Kconfig (`app/Kconfig`, `CONFIG_APP_NODE_ID`) | Hardcode trong `main.c` | M2 cần build 3 node với NODE_ID khác nhau; làm quen cơ chế Kconfig sớm khi rủi ro thấp, tránh học công cụ mới giữa chừng ở M2 |
| TX↔RX giao tiếp qua `k_msgq` nội bộ (`K_MSGQ_DEFINE`) | `k_fifo`, biến global + polling | Mô phỏng đúng interface "RX callback → message queue → RX thread" mà transport BLE thật (M2+) sẽ dùng; tái sử dụng được cấu trúc |
| Không thêm mutex ở M1 | Thêm mutex bảo vệ sẵn cho quen tay | Chưa có shared state (graph/cost table) cần bảo vệ tới M3/M4; thêm sớm chỉ là nghi thức trống, không kiểm chứng được race condition thật nào |

## 4. Implementation Summary
- **File thay đổi/tạo mới:** `app/main.c` (thêm `K_MSGQ_DEFINE(tx_to_rx_q, ...)`, `K_THREAD_DEFINE` cho `tx_tid`/`rx_tid`, hai hàm `tx_thread_entry`/`rx_thread_entry`); `app/Kconfig` (mới — khai báo `CONFIG_APP_NODE_ID`); `app/prj.conf` (thêm `CONFIG_APP_NODE_ID=0`)
- **Logic cốt lõi:** TX thread mỗi `TX_PERIOD_MS` (1000ms) tăng `counter`, gọi `k_msgq_put()` non-blocking (`K_NO_WAIT`) vào `tx_to_rx_q`; RX thread block trên `k_msgq_get(..., K_FOREVER)`, log ngay khi nhận.
- **Đoạn minh hoạ quyết định lõi** (kiểm tra return value của `k_msgq_put`, sửa sau review — xem mục 6):
```c
int ret = k_msgq_put(&tx_to_rx_q, &msg, K_NO_WAIT);
if (ret == 0) {
    printk("[TX] Da gui goi tin, counter = %u\n", msg.counter);
} else {
    printk("[TX] LOI: queue day, khong gui duoc counter = %u (ret=%d)\n",
           msg.counter, ret);
}
```

## 5. Test Procedure & Evidence
**Environment:** WSL2 Ubuntu, Zephyr 3.7.0, board `nrf52840dk/nrf52840`, Renode 1.17.0 (1.17.0+20260916git84b37e724), Zephyr SDK 0.16.8
**Cách chạy:** `./scripts/build_and_run.sh` (`west build -b nrf52840dk/nrf52840 app -p always` + `renode renode/topology.resc`)

Build log (trích):
```
-- west build: making build dir .../build pristine
...
[135/135] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       19652 B         1 MB      1.87%
             RAM:        8208 B       256 KB      3.13%
```

Renode load log (trích, xác nhận Kconfig plumbing hoạt động):
```
Parsing /home/vu/zephyrproject/bluetooth-dijkstra-simplified/app/Kconfig
...
[node] Machine started.
```

UART output (trích, đầy đủ 30 chu kỳ không rớt/trùng — log gốc trong terminal đính kèm):
```
*** Booting Zephyr OS build v3.7.0 ***
Node ID = 0
[TX] Da gui goi tin, counter = 1
[RX] Da nhan goi tin, counter = 1
...
[TX] Da gui goi tin, counter = 30
[RX] Da nhan goi tin, counter = 30
```

Ghi chú: các dòng `[WARNING]` từ Renode (FICR/UART/RTC/clock) là cảnh báo mô phỏng phần cứng chưa implement đầy đủ trong platform description, không liên quan firmware, không ảnh hưởng kết quả test.

## 6. Issues Encountered & Resolution
**Vấn đề:** bản code đầu tiên gọi `k_msgq_put()` nhưng bỏ qua giá trị trả về — log "Da gui goi tin" vô điều kiện dù enqueue có thể thất bại (`-ENOMSG` khi queue đầy, do dùng `K_NO_WAIT`). Bug không lộ ra trong log M1 vì RX luôn rảnh chờ sẵn (`K_FOREVER`) và queue depth 4 dư an toàn với cấu hình 1 producer/1 consumer — nhưng sẽ gây log sai sự thật (báo "đã gửi" trong khi gói bị rớt âm thầm) một khi transport thật (BLE, có delay) thay thế queue nội bộ ở M2+.
**Giải quyết:** thêm kiểm tra return value, tách log thành 2 nhánh thành công/thất bại rõ ràng.

## 7. Discussion
Kết quả khớp đầy đủ Definition of Done ở mục 2: build sạch, Node ID đúng, TX/RX khớp 1:1 tuần tự qua 30 chu kỳ, không rớt/trùng gói. Giới hạn còn lại: chưa test được nhánh lỗi "queue đầy" trong thực tế (chưa xảy ra ở kịch bản 1 producer/1 consumer luôn rảnh) — cần re-test khi RX thread bận hơn ở milestone sau (ví dụ RX phải chạy Dijkstra trước khi rảnh lại để nhận gói tiếp theo).

## 8. Next Steps
- Carry-over sang M2: thay `k_msgq` nội bộ bằng BLE advertising/scanning thật giữa 3 machine trong Renode
- Build 3 lần với `CONFIG_APP_NODE_ID` = 0/1/2 (build_a/build_b/build_c)
- Cấu hình Renode multi-node: `emulation CreateBLEMedium "wireless"` + `connector Connect sysbus.radio wireless` cho mỗi machine
- Bắt đầu tổ chức log evidence theo cấu trúc `logs/M<N>/...` từ M2 trở đi
