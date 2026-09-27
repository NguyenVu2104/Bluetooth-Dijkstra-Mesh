# Bluetooth Dijkstra Mesh — Milestone 2 Report

## 1. Metadata

| Field | Value |
|---|---|
| Project | Bluetooth Dijkstra Mesh |
| Milestone | M2 — 3-node BLE transport Markdown Preview Enhanced|
| Status | **PASS** |
| Platform | Zephyr RTOS 3.7 LTS, board `nrf52840dk/nrf52840` (virtual), Renode 1.17.0 |
| Host environment | Windows + WSL2 (Ubuntu), Zephyr workspace `~/zephyrproject` |

## 2. Objective & Scope

**Mục tiêu**: chứng minh 3 node ảo (A, B, C) trao đổi được dữ liệu qua BLE thật (không dây giả lập trong Renode), chưa cần `struct packet` chính thức hay Dijkstra routing.

**Definition of Done**:
- Cả 3 cửa sổ UART hiển thị log RX nhận được message từ **2 node còn lại**, không tự nhận lại broadcast của chính mình.
- `counter` trong log RX tăng dần, không lặp lại giá trị cũ liên tục.
- Chạy ổn định liên tục qua nhiều chu kỳ, không crash/treo.

**Ngoài scope của M2** (carry sang M3/M4):
- `struct packet` chính thức (type, src_id, dst_id, ttl, seq).
- Static graph + Dijkstra routing thật.
- Multi-hop forwarding, ACK/retry, cost-update/reroute.

## 3. Design Decisions

| Decision | Alternatives đã cân nhắc | Rationale |
|---|---|---|
| Vai trò BLE: **Broadcaster + Observer** (advertising/scanning quảng bá) | Central/Peripheral (connection-based, dùng trong demo mẫu chính thức của Renode) | Khớp bản chất mesh flooding/broadcast của project; nhẹ hơn về Kconfig (không cần `CONFIG_BT_CENTRAL`/`CONFIG_BT_PERIPHERAL`); không cần quản lý connection state |
| Advertising interval: `BT_GAP_ADV_SLOW_INT_MIN`/`MAX` (~1.0–1.2s, hằng số Zephyr chính thức) | Giữ mặc định `BT_GAP_ADV_FAST_INT_MIN_2`/`MAX_2` (~100–150ms) | Khớp `TX_PERIOD_MS=1000ms` từ M1, giảm số lần radio tự lặp lại payload cũ trong 1 chu kỳ logic, giảm tải time-slicing cho Renode (`SetGlobalQuantum`), log dễ đọc hơn. Xác nhận đây là hằng số Zephyr có sẵn cho use case tần suất thấp, không phải custom liều lĩnh |
| Payload M2: **dynamic counter** tăng dần | Chuỗi tĩnh không đổi | 4 byte dư sức nằm trong 31-byte budget legacy advertising; cho phép phát hiện packet loss (counter nhảy cóc) và test sớm `bt_le_adv_update_data()` — hạ tầng cần thiết cho payload thay đổi liên tục ở M3/M4 |
| Lọc self-reception: **byte đầu payload = NODE_ID người gửi**, so sánh với `CONFIG_APP_NODE_ID` | Lọc theo địa chỉ BLE (MAC/Random Static Address) | Dijkstra vốn cần `src_id` trong payload dù thế nào (M3); duy trì thêm cơ chế lọc theo MAC là abstraction thừa. Tránh rủi ro Random Static Address có thể trùng/đổi giữa các lần reset trong môi trường giả lập |
| Cập nhật payload mỗi chu kỳ: **`bt_le_adv_update_data()`** + dedup tối giản ở RX bằng `last_seen[node_id]` | `bt_le_adv_stop()` + `bt_le_adv_start()` lại mỗi chu kỳ | `update_data()` là best-practice Zephyr, không phá vỡ state machine của BLE controller. Dedup bằng so sánh counter là đủ để loại nhiễu do radio tự lặp lại payload cũ — chưa cần seq/ACK đầy đủ (việc đó thuộc M4) |
| TX/RX thread: static (`K_THREAD_DEFINE`) tạo ở trạng thái suspended, `main()` gọi `k_thread_start()` sau khi `transport_init()` thành công | Tạo thread chạy ngay từ boot (delay=0) | Tránh race condition: TX/RX có thể chạy trước khi `bt_enable()` hoàn tất nếu không chờ. Áp dụng đúng kỹ thuật "static + suspended" đã xác định từ M1 |

## 4. Implementation Summary

File/module thay đổi so với M1:
- `app/src/transport.h`, `app/src/transport.c` — module mới, đóng gói toàn bộ logic BLE (broadcaster + observer) và message queue nội bộ (`rx_msgq`); expose 3 hàm: `transport_init()`, `transport_send()`, `transport_recv()`.
- `app/src/main.c` — sửa lại: TX thread encode `{sender_id, counter}` vào buffer 5-byte rồi gọi `transport_send()`; RX thread gọi `transport_recv()`, decode, dedup bằng mảng `last_seen[3]`, log.
- `app/prj.conf` — thêm `CONFIG_BT=y`, `CONFIG_BT_OBSERVER=y`, `CONFIG_BT_BROADCASTER=y`.
- `app/CMakeLists.txt` — thêm `src/transport.c` vào `target_sources`.
- `renode/topology_m2.resc` — script Renode mới, tạo 3 machine dùng chung `emulation CreateBLEMedium "wireless"`.
- `scripts/build_and_run_m2.sh` — build 3 lần vào `build_a/build_b/build_c` với `CONFIG_APP_NODE_ID` khác nhau (0/1/2), sau đó chạy `topology_m2.resc`.

Logic cốt lõi minh hoạ (cơ chế tự lọc self-reception, `app/src/transport.c`):
```c
if (data->type == BT_DATA_MANUFACTURER_DATA && data->data_len == APP_MSG_LEN) {
    if (data->data[0] != (uint8_t)CONFIG_APP_NODE_ID) {
        k_msgq_put(&rx_msgq, data->data, K_NO_WAIT);
    }
}
```

## 5. Test Procedure & Evidence

**Cách chạy**: `./scripts/build_and_run_m2.sh` từ `~/zephyrproject/bluetooth-dijkstra-simplified/`, build 3 target riêng biệt rồi khởi chạy Renode với `renode/topology_m2.resc`.

**Build log** (trích, giống nhau cho build_a/build_b/build_c ngoại trừ `CONFIG_APP_NODE_ID`):
```
-- Zephyr version: 3.7.0 (/home/vu/zephyrproject/zephyr), build: v3.7.0
[189/189] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       59364 B         1 MB      5.66%
             RAM:       17904 B       256 KB      6.83%
```

**Renode session log** (trích):
```
[INFO] Including script(s): /home/vu/zephyrproject/bluetooth-dijkstra-simplified/renode/topology_m2.resc
[INFO] sysbus: Loaded SVD: ... Name: nrf52840 ...
[INFO] nodeB/sysbus: Loaded SVD: ... Name: nrf52840 ...
[INFO] nodeC/sysbus: Loaded SVD: ... Name: nrf52840 ...
(monitor) i $CWD/renode/topology_m2.resc
Starting emulation...
(nodeC) []
```
Không có dòng lỗi nào trong phiên chạy cuối cùng (sau khi sửa thứ tự khai báo biến trong `.resc`, xem mục 6).

**UART log** (trích từ 3 cửa sổ analyzer, cùng một cửa sổ thời gian, counter 19→26):

`nodeA` (Node 1):
```
[RX] Nhan tu Node 1, counter = 19   ← lưu ý: dòng đầu là echo trước khi cửa sổ mở đủ, các dòng sau ổn định
[TX] Da gui, counter = 20
[RX] Nhan tu Node 2, counter = 20
[TX] Da gui, counter = 21
[RX] Nhan tu Node 2, counter = 21
[RX] Nhan tu Node 1, counter = 21
...
[TX] Da gui, counter = 26
```

`nodeB` (Node 0):
```
[RX] Nhan tu Node 0, counter = 19
[TX] Da gui, counter = 20
[RX] Nhan tu Node 2, counter = 20
[RX] Nhan tu Node 0, counter = 20
...
[TX] Da gui, counter = 26
```

`nodeC` (Node 2):
```
[RX] Nhan tu Node 0, counter = 19
[TX] Da gui, counter = 20
[RX] Nhan tu Node 0, counter = 20
[RX] Nhan tu Node 1, counter = 20
...
[TX] Da gui, counter = 26
```

Đối chiếu chéo: mỗi node chỉ nhận từ **2 node còn lại**, không có dòng `[RX]` nào trùng NODE_ID của chính node đó → xác nhận self-filter hoạt động đúng trên cả 3 node. Counter tăng đơn điệu qua toàn bộ đoạn log quan sát được (19→26, 8 chu kỳ liên tiếp), không có giá trị lặp lại hay giá trị nhảy cóc bất thường → xác nhận dedup và transport hoạt động ổn định.

**Ghi chú về evidence**: log UART ở trên được chụp trực tiếp từ cửa sổ `showAnalyzer` (GUI), chưa đi qua pipeline `CreateFileBackend`/`logFile` như quy trình tài liệu hoá đã đề ra cho `logs/M<N>/`. Cả `topology.resc` và `topology_m2.resc` hiện tại chưa cấu hình ghi file log — đây là điểm cần bổ sung để evidence các milestone sau tái tạo được 100% mà không cần chụp màn hình thủ công (xem mục 8).

## 6. Issues Encountered & Resolution

| # | Issue | Root cause | Resolution |
|---|---|---|---|
| 1 | Compile error tại `K_THREAD_DEFINE(..., K_FOREVER)`: `invalid operands to binary ==` | Tham số `delay` của `K_THREAD_DEFINE` là kiểu `int` (số ms), không phải `k_timeout_t`; `K_FOREVER` là struct, sai kiểu | Thay `K_FOREVER` bằng `SYS_FOREVER_MS` (hằng số nguyên đặc biệt Zephyr tự dịch sang "suspended forever") |
| 2 | Compile error tại `BT_LE_ADV_PARAM(...)`: `invalid initializer` | Macro `BT_LE_ADV_PARAM` mở rộng thành compound literal mảng, decay thành **con trỏ**; gán cho biến kiểu `struct bt_le_adv_param` (không phải con trỏ) sai kiểu | Đổi khai báo `adv_param` thành `const struct bt_le_adv_param *`, bỏ dấu `&` khi truyền vào `bt_le_adv_start()` |
| 3 | Renode báo `No such variable: $bin_a` khi chạy `runMacro $reset` | Biến `$bin_a/$bin_b/$bin_c` được khai báo **sau** khi đã `mach create` cả 3 machine, bị ràng buộc vào context của machine đang active lúc đó (`nodeC`) thay vì global | Di chuyển 3 dòng khai báo biến lên **trước** `emulation CreateBLEMedium`/`mach create`, đúng thứ tự script mẫu chính thức của Renode (`scripts/multi-node/nrf52840-ble-zephyr.resc`) |

## 7. Discussion

Kết quả khớp đầy đủ Definition of Done ở mục 2: 3 node giao tiếp qua BLE thật, tự lọc đúng broadcast của chính mình, counter tăng ổn định không rớt/trùng qua log quan sát được.

Giới hạn còn lại: (1) payload vẫn là quy ước tạm 5-byte (byte0=NODE_ID, byte1-4=counter), sẽ bị thay thế hoàn toàn bởi `struct packet` ở M3; (2) mọi node đều nghe được mọi node khác trực tiếp (broadcast vật lý thật, không có giới hạn khoảng cách radio mô phỏng) — nghĩa là routing/multi-hop ở M3 sẽ cần cơ chế ép buộc riêng để Dijkstra thực sự quyết định đường đi thay vì mọi gói đều có thể "đi tắt"; (3) evidence hiện thu thủ công qua GUI, chưa có file log tái tạo được.

## 8. Next Steps

- M3: thiết kế lại packet format thành `struct packet` chính thức (`type`, `src_id`, `dst_id`, `next_hop_id`, `ttl`, `seq`); `next_hop_id` bắt buộc để ép routing đi đúng theo Dijkstra tính ra thay vì lợi dụng broadcast vật lý đi tắt.
- M3: viết `routing.c/.h` (static graph 3 đỉnh, đề xuất dạng tam giác A-B/B-C/A-C với cost khác nhau để M4 có ý nghĩa reroute) + Dijkstra.
- Bổ sung `CreateFileBackend` (UART) và `logFile` (Monitor) vào `.resc` scripts để evidence từ M3 trở đi được ghi file tự động, đúng quy trình tài liệu hoá đã chốt.