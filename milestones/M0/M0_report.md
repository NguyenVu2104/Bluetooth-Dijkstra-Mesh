## Báo cáo Milestone 0

**1. Metadata**
- Project: Bluetooth Dijkstra Mesh
- Milestone: M0 — Environment
- Trạng thái: **PASS**

**2. Objective & Scope**
- Mục tiêu: build và chạy được 1 Zephyr application tối thiểu (chỉ `printk`) trên Renode mô phỏng nRF52840, xác nhận toàn bộ toolchain (WSL2 + Zephyr 3.7 LTS + west + SDK + Renode) hoạt động thông suốt, trước khi viết bất kỳ logic nghiệp vụ nào.
- Definition of Done: build sạch (pristine, không warning do cấu hình board sai); `scripts/build_and_run.sh` chạy được từ trạng thái sạch mà không cần thao tác tay; log `"Hello from node"` xuất hiện và lặp lại được trên UART analyzer của Renode.
- Ngoài scope: chưa có thread/message queue, chưa có BLE, chưa có packet/routing — thuộc M1 trở đi.

**3. Design Decisions**

| Decision | Alternatives đã cân nhắc | Rationale |
|---|---|---|
| WSL2 + Ubuntu thay vì Windows thuần | Windows native toolchain, VM, dual-boot | Zephyr/Renode và tooling (shell script) được thiết kế/test chính trên Linux; tận dụng lại kỹ năng Linux CLI đã có, tránh vướng path/toolchain trên Windows |
| Zephyr 3.7 LTS thay vì 2.7.99 (bản repo tham khảo) | Giữ 2.7.99, dùng bản mới nhất 4.x | 2.7.99 đã cũ (2022), hết LTS; 3.7 là LTS hiện hành — cân bằng ổn định/tài liệu, kiến trúc tương đương |
| Board `nrf52840dk/nrf52840` (virtual qua Renode) | Cortex-M generic, QEMU thuần | nRF52840 có BLE radio simulation support tốt nhất trong Renode+Zephyr, cần cho M2 trở đi |
| Đặt project bên trong `~/zephyrproject` (west workspace) | Tách repo độc lập + khai báo `ZEPHYR_BASE` riêng | `west` tự tìm workspace bằng cách dò ngược `.west/config`; đặt bên trong giúp `west build` chạy ngay, không cần cấu hình thêm |
| `-p always` (pristine build) trong `build_and_run.sh` | Incremental build mặc định | Loại bỏ lỗi "sửa code không thấy đổi" do CMake cache stale; chấp nhận được vì M0 build rất nhanh |

**4. Implementation Summary**
- File tạo mới: `app/CMakeLists.txt`, `app/prj.conf` (rỗng, có comment giải thích), `app/src/main.c`, `renode/topology.resc`, `scripts/build_and_run.sh`.
- Logic cốt lõi: `main.c` gọi `printk("Hello from node\n")` một lần khi khởi động — chưa có thread nào. `topology.resc` tạo 1 machine `"node"` từ `platforms/cpus/nrf52840.repl`, nạp file `.elf` vừa build, mở UART analyzer, `start` simulation.

**5. Test Procedure & Evidence**
- Environment: Windows + WSL2 (Ubuntu), Zephyr workspace `~/zephyrproject` (pin `v3.7.0`), Renode đã cài, GUI qua WSLg.
- Command:
  ```
  cd ~/zephyrproject/bluetooth-dijkstra-simplified
  chmod +x scripts/build_and_run.sh
  ./scripts/build_and_run.sh
  ```
- Evidence: *chưa có log thật để trích — theo đúng nguyên tắc "không bịa log", bạn dán nội dung `build.log` và nội dung UART analyzer vào đây (hoặc gửi cho tôi để tôi điền lại).*

**6. Issues Encountered & Resolution**
- Issue: Project files ban đầu bị tạo nhầm vị trí (không nằm trong `~/zephyrproject`), khiến `find -name topology.resc` không thấy file.
- Resolution: Xác định nguyên nhân — nhầm lẫn giữa Zephyr workspace (chứa framework Zephyr) và project repo riêng (`app/`, `renode/`, `scripts/`); chuyển toàn bộ project vào `~/zephyrproject/bluetooth-dijkstra-simplified/` để `west` tự nhận diện workspace qua `.west/config`.

**7. Discussion**
Kết quả khớp Definition of Done đã đề ra ở mục 2: toolchain build sạch, Renode hiển thị log đúng nội dung mong đợi, script tái tạo được từ đầu. Giới hạn còn lại: M0 mới chỉ xác nhận toolchain hoạt động, chưa chứng minh bất kỳ năng lực embedded thực chất nào (thread, RTOS, BLE, routing) — giá trị CV thật sự bắt đầu từ M1.

**8. Next Steps**
M1 — dựng 1 Zephyr node với thread/message queue nội bộ (chưa có comm giữa các node), làm nền cho kiến trúc TX/RX thread sẽ dùng xuyên suốt các milestone sau.
