#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

west build -b nrf52840dk/nrf52840 app -p always
# -b nrf52840dk/nrf52840 app: Nạp sơ đồ phần cứng của board mạch và trỏ vào thư mục chứa mã nguồn.
# -p always: Ép xóa sạch cache để build lại từ đầu, ngăn chặn lỗi sửa code mới nhưng hệ thống vẫn nạp file cũ.

renode renode/topology.resc
