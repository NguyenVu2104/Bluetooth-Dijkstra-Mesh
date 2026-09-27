#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Bật/tắt fault injection cho Node B bằng cách đổi giá trị biến này:
#   y = demo có lỗi (A-B rớt gói, quan sát Dijkstra reroute qua A-C)
#   n = demo baseline (không có lỗi, A->B->C chạy bình thường)
FAULT_INJECTION=y

mkdir -p logs/M4
rm -f logs/M4/*.log

west build -b nrf52840dk/nrf52840 app -d build_a -p always -- -DCONFIG_APP_NODE_ID=0 2>&1 | tee logs/M4/build_a.log
west build -b nrf52840dk/nrf52840 app -d build_b -p always -- -DCONFIG_APP_NODE_ID=1 -DCONFIG_APP_FAULT_INJECTION=${FAULT_INJECTION} 2>&1 | tee logs/M4/build_b.log
west build -b nrf52840dk/nrf52840 app -d build_c -p always -- -DCONFIG_APP_NODE_ID=2 2>&1 | tee logs/M4/build_c.log
# -DCONFIG_APP_FAULT_INJECTION=${FAULT_INJECTION}: chỉ Node B mới cần cờ này,
# vì đây là node được chọn để "làm rớt" gói từ Node A trong kịch bản demo M4.
# Node A và C không cần cờ này vì điều kiện fault check trong main.c đã tự
# giới hạn theo self_id (chỉ B mới xét), truyền thêm cũng không có tác dụng.

renode --console renode/topology_m4.resc 2>&1 | tee logs/M4/renode_monitor.log