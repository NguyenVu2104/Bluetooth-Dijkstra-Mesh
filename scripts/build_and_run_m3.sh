#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Chuẩn bị thư mục log sạch cho lần chạy này, dùng quy ước logs/M<N>/
mkdir -p logs/M3
rm -f logs/M3/*.log

west build -b nrf52840dk/nrf52840 app -d build_a -p always -- -DCONFIG_APP_NODE_ID=0 2>&1 | tee logs/M3/build_a.log
west build -b nrf52840dk/nrf52840 app -d build_b -p always -- -DCONFIG_APP_NODE_ID=1 2>&1 | tee logs/M3/build_b.log
west build -b nrf52840dk/nrf52840 app -d build_c -p always -- -DCONFIG_APP_NODE_ID=2 2>&1 | tee logs/M3/build_c.log
# -b nrf52840dk/nrf52840 app: Nạp sơ đồ phần cứng của board mạch và trỏ vào thư mục chứa mã nguồn.
# app: Thư mục chứa CMakeLists.txt
# build_x: Thư mục build riêng cho từng node, tránh xung đột khi build nhiều node cùng lúc.
# -p always: Ép xóa sạch cache để build lại từ đầu, ngăn chặn lỗi sửa code mới nhưng hệ thống vẫn nạp file cũ.
# --: Bức tường phân quyền lệnh. Nó báo cho công cụ west: "Nhiệm vụ của mi đến đây là hết. Ném nguyên xi mọi thứ phía sau bức tường này thẳng vào lõi CMake"
# -DCONFIG_APP_NODE_ID=x: Truyền biến cấu hình cho CMake, xác định ID của node (0, 1, 2) để build từng node riêng biệt. Cơ chế này dùng đúng 1 mã nguồn main.c để sinh ra 3 phiên bản firmware khác nhau hoàn toàn (Node A, B, C) mà không phải mở file mã nguồn ra sửa tay sửa thủ công. Cú pháp chuẩn của CMake là -D<Tên_biến>=<Giá_trị>, -D (Define). Tên câu lệnh này (CONFIG_APP_NODE_ID) được định nghĩa trong Kconfig nên mới có tên như vậy.

renode --console renode/topology_m3.resc 2>&1 | tee logs/M3/renode_monitor.log
