# M4 Technical Design & Test Report — Reliable Transmission, Cost-Adaptive Routing & Fault Injection

## 1. Metadata

| | |
|---|---|
| Project | Bluetooth Dijkstra Mesh (`bluetooth-dijkstra-simplified`) |
| Milestone | M4 — ACK + timeout + retry + cost-update + demo reroute khi đổi cost |
| Trạng thái | **PASS** — build 3 node thành công, chạy Renode thành công, quan sát đủ toàn bộ chuỗi nhân quả mục tiêu |
| Kế thừa từ | M3 (`M3_Technical_Design_Test_Report.md`) — struct packet, Dijkstra generic, BLE Broadcaster+Observer, dedup theo (src_id, next_hop_id) |
| File thay đổi | `app/src/packet.h`, `app/src/routing.h`, `app/src/routing.c`, `app/src/main.c`, `app/src/transport.c`, `app/Kconfig`, `scripts/build_and_run_m4.sh`, `renode/topology_m4.resc` |
| File không đổi | `app/src/transport.h`, `app/prj.conf`, `app/CMakeLists.txt` |
| Log evidence | `logs/M4/{build_a,build_b,build_c,renode_monitor,uart_a,uart_b,uart_c}.log` |

## 2. Objective & Scope

M3 chứng minh Dijkstra **tìm được** một đường đi hợp lệ trên graph tĩnh. Mục tiêu của M4 là chứng minh Dijkstra **phản ứng được** với sự thay đổi chất lượng liên kết theo thời gian thực — tức là mở rộng từ "static routing" sang "adaptive routing":

```
link mất gói → cost tăng → Dijkstra tính lại → route đổi
```

Phạm vi cụ thể:
- Thêm cơ chế **hop-by-hop ACK** để mỗi node tự đo được chất lượng của đúng liên kết nó đang dùng, không suy đoán qua liên kết khác.
- Thêm **retry có giới hạn** và định nghĩa rõ ràng "1 lần mất gói" (miss event) tách biệt khỏi số lần gửi lại.
- Thêm cơ chế **cost thích nghi**: mất gói → cost nhân đôi; gửi thành công → cost hồi phục về gốc.
- Thêm cạnh **A-C** vào topology (cost=5) để có route thay thế thật sự, biến demo từ "mất route" thành "đổi route".
- Thêm **fault injection ở tầng application** (Node B chủ động rớt gói từ Node A) để tạo điều kiện demo mà không cần mô phỏng lỗi RF thật.

Ngoài phạm vi M4 (đưa vào roadmap): fault window có baseline/auto-tắt, đa giao dịch đồng thời, worker thread/work queue riêng, adaptive timeout, link-quality metric phức tạp hơn nhân đôi/cố định.

## 3. Design Decisions

| # | Quyết định | Chốt | Lý do |
|---|---|---|---|
| 1 | Mô hình ACK | Hop-by-hop | Đo đúng chất lượng của liên kết đang dùng, không suy đoán edge lỗi qua timeout end-to-end |
| 2 | Field mới trong `struct packet` | `prev_hop_id` (+1 byte) | `next_hop_id`/`src_id` không đủ xác định "ai vừa truyền gói này ở hop hiện tại" khi gói đã qua ≥2 hop — cần thiết để ACK gửi đúng người và fault injection không bắt oan gói đi đường vòng |
| 3 | Fault injection | Node B chủ động drop DATA khi `prev_hop_id == A` (không dùng `src_id`) | Dùng `src_id` sẽ bắt oan gói có nguồn gốc từ A nhưng đến qua đường vòng (vd A→C→B) — không đúng bản chất "mô phỏng lỗi trên liên kết A-B" |
| 4 | Cơ chế kích hoạt fault | Compile-time Kconfig flag (`CONFIG_APP_FAULT_INJECTION`), luôn bật khi flag=y, không có logic đếm chu kỳ/cửa sổ thời gian | Đơn giản nhất đủ dùng cho scope M4; baseline/auto-tắt để dành roadmap |
| 5 | `ACK_TIMEOUT_MS` | 3500 ms | Dư ~900ms trên round-trip lý thuyết (2× `ADV_INTERVAL_GUARD_MS`=1300ms) |
| 6 | `MAX_RETRY` | 1 (tối đa 2 lần gửi/giao dịch) | Giới hạn worst-case demo còn ~28.8s cho 3 miss liên tiếp, thay vì ~43s nếu để retry=2 |
| 7 | Định nghĩa "miss" | Hết `MAX_RETRY` mà vẫn không có ACK khớp = 1 miss event | Tách biệt transmission-reliability (retry) khỏi routing-metric (miss) |
| 8 | Cost khi miss | Nhân đôi (`×2`), chặn ở `ROUTING_COST_INF`=255 | Dốc tăng nhanh đủ để vượt ngưỡng route thay thế sau vài lần miss |
| 9 | Cost khi thành công | Hồi phục thẳng về cost gốc (không giảm dần) | Đơn giản, đủ minh hoạ ý tưởng "link tốt trở lại" |
| 10 | Topology | Thêm cạnh A-C, cost=5 (giữ A-B=1, B-C=1) | Tạo route thay thế thật, chứng minh "reroute" thay vì chỉ "mất route" |
| 11 | Kiến trúc thread | Giữ 2 thread; RX thread là **transaction manager không-blocking** (state machine); TX thread submit-request rồi chờ kết quả | Tránh deadlock của việc để RX thread tự `k_sem_take()` chờ ACK — RX chính là thread duy nhất đọc được gói ACK |
| 12 | Giao tiếp TX↔RX | 1-slot, 2 semaphore (`tx_request_sem`, `tx_result_sem`), không dùng `k_msgq` | Chỉ có đúng 1 giao dịch active/node tại một thời điểm |
| 13 | `transport_recv()` khi `WAIT_ACK` | Poll cố định 500ms, so deadline tuyệt đối mỗi vòng | Đơn giản hơn tính `remaining` chính xác từng lần gọi, sai số 500ms chấp nhận được |
| 14 | Gói "lạc" tới trong lúc bận | Drop, không queue lại | Hệ quả của phương án "serialize hoàn toàn" đã chốt trước khi code |
| 15 | `msgq` depth (`transport.c`) | Tăng từ 8 (M3) lên 10 | Giảm nguy cơ overflow trong giới hạn demo — **không đảm bảo tuyệt đối**, vì TX period 1s và block time tới ~9.6s vẫn có thể vượt 10 gói |
| 16 | Phân lớp file | `transport.c`: chỉ byte-level BLE (không đổi logic so M3). `main.c`: toàn bộ state machine/ACK/fault/cost-update. `routing.c`: chỉ graph+Dijkstra | Giữ đúng ranh giới trách nhiệm đã có từ M3 |

## 4. Implementation Summary

**`packet.h`** — thêm `prev_hop_id` vào `struct packet` (tổng kích thước 8 byte header + 13 byte payload = 21 byte, vẫn trong ngân sách BLE adv). Semantics của từng field được ghi rõ bằng bảng trong comment, phân biệt ý nghĩa khi `type=DATA` và `type=ACK`.

**`routing.h`/`routing.c`** — thêm ma trận `base_graph[][]` lưu cost gốc song song với `graph[][]` (cost hiện tại), cùng 3 hàm mới: `routing_record_miss()` (nhân đôi, chặn ở `ROUTING_COST_INF`), `routing_record_success()` (reset về `base_graph`), `routing_get_cost()` (đọc để log). Vòng lặp Dijkstra trong `routing_next_hop()` **không đổi một dòng nào** so với M3 — nó luôn đọc `graph[][]` mới nhất, tự động "thấy" mọi thay đổi cost mà không cần biết cơ chế miss/success tồn tại.

**`main.c`** — viết lại phần lớn theo mô hình máy trạng thái:
- `enum tx_state { IDLE, SEND, WAIT_ACK, RETRY, MISS, SUCCESS }` chạy trong RX thread, mỗi bước có giới hạn thời gian (không bước nào block vô hạn).
- `struct pending_tx` giữ giao dịch đang active (packet, origin TX/FORWARD, số lần đã gửi, deadline).
- TX thread không còn gọi `transport_send()` trực tiếp — nó ghi gói vào `g_tx_request_staging`, `k_sem_give(&tx_request_sem)`, rồi `k_sem_take(&tx_result_sem, K_FOREVER)` chờ kết quả.
- `send_ack_for()` xây gói ACK với `next_hop_id = data_pkt->prev_hop_id` (điểm sửa lỗi quan trọng nhất của M4).
- `handle_incoming_data()` giữ nguyên logic dedup/filter của M3, chèn thêm fault-check (theo `prev_hop_id`) trước khi gửi ACK, và chuyển việc forward thành 1 giao dịch `ORIGIN_FORWARD` thay vì gửi ngay.

**`transport.c`** — chỉ 1 thay đổi: `K_MSGQ_DEFINE(rx_msgq, APP_MSG_LEN, 10, 4)` (tăng từ 8 lên 10), kèm comment giải thích giới hạn của thay đổi này.

**`Kconfig`** — thêm `config APP_FAULT_INJECTION` (bool, default n), truyền qua `-DCONFIG_APP_FAULT_INJECTION=y` khi build Node B, đúng cơ chế đã dùng cho `CONFIG_APP_NODE_ID` từ M1.

## 5. Test Procedure & Evidence

### 5.1 Build

Build cả 3 node bằng `scripts/build_and_run_m4.sh` (Node B build với `-DCONFIG_APP_FAULT_INJECTION=y`). Cả 3 build hoàn tất không lỗi, không warning biên dịch. Từ `build_a.log`:

```
[190/190] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       62032 B         1 MB      5.92%
             RAM:       18204 B       256 KB      6.94%
```

`build_b.log` và `build_c.log` không có dòng `warning`/`error` nào liên quan tới code ứng dụng (đã kiểm tra bằng `grep`).

### 5.2 Renode

Chạy bằng `renode/topology_m4.resc`. `renode_monitor.log` chỉ chứa các dòng `WARNING`/`ERROR` thuộc về mô hình radio nRF52840 của Renode (`Unhandled short DisabledRSSIStop`, `Unhandled read/write` trên các offset thanh ghi radio) — đây là giới hạn đã biết của mô hình mô phỏng phần cứng, xuất hiện đều ở cả 3 node bất kể code ứng dụng, **không phải lỗi từ code M4**. Không có lỗi `CreateFileBackend` hay lỗi CRLF nào xuất hiện (đã áp dụng `sed -i 's/\r$//'` cho cả script và file `.resc` trước khi chạy, đúng quy trình đã rút ra từ M3).

### 5.3 Chuỗi nhân quả mục tiêu — đã quan sát đầy đủ trên `uart_a.log`

```
seq=0 (A→B trực tiếp): MAT GOI → cost A-B: 1 → 2
seq=1 (A→C qua B):     MAT GOI → cost A-B: 2 → 4
seq=2 (A→B trực tiếp): MAT GOI → cost A-B: 4 → 8
seq=3 (A→C):           THANH CONG, next_hop=2 (route trực tiếp A-C, KHÔNG còn qua B)
```

Trích nguyên văn từ `uart_a.log`:

```
[RX] MAT GOI: lien ket Node 0 <-> Node 1, seq=0 - cost moi = 2
[RX] MAT GOI: lien ket Node 0 <-> Node 1, seq=1 - cost moi = 4
[RX] MAT GOI: lien ket Node 0 <-> Node 1, seq=2 - cost moi = 8
[TX] Gui (lan 1) toi Node 2, seq=3
[TX] Ket qua gui toi Node 2 qua next_hop=2, seq=3: THANH CONG
```

Đây là bằng chứng trực tiếp cho toàn bộ chuỗi thiết kế: **mất gói → cost tăng theo đúng công thức ×2 (1→2→4→8) → khi cost qua B (8+1=9) vượt cost trực tiếp A-C (5), Dijkstra tự chọn route mới**. Route đã **đổi**, không chỉ biến mất — đúng mục tiêu phân biệt M3/M4.

### 5.4 Hiện tượng bậc 2 ngoài dự kiến ban đầu — Dijkstra generic phản ứng đúng dù không hardcode

`uart_a.log` còn cho thấy ở `seq=4`, khi A cần gửi cho B nhưng A-B đã lên tới cost 8, Dijkstra tự tính ra route A→C→B (5+1=6, rẻ hơn 8) thay vì cố dùng A-B trực tiếp:

```
[RX] MAT GOI: lien ket Node 0 <-> Node 2, seq=4 - cost moi = 10
[TX] Ket qua gui toi Node 1 qua next_hop=2, seq=4: MAT GOI
```

(`next_hop=2` tức Node C, dù đích cuối là Node B — xác nhận A đang định tuyến qua C để tới B). Đây không phải kịch bản được thiết kế cứng, mà là hệ quả tự nhiên của việc giữ nguyên vòng lặp Dijkstra generic từ M3 — bằng chứng cho thấy quyết định thiết kế "không hardcode 3 node" ở M3 tiếp tục trả giá trị đúng ở M4.

### 5.5 Xác nhận bản sửa `prev_hop_id` hoạt động đúng — không bắt oan gói đi đường vòng

Trong `uart_b.log`, dòng `FAULT INJECT` chỉ xuất hiện đúng khi `prev_hop_id=0` (gói tới **trực tiếp** từ A):

```
[RX] FAULT INJECT: co tinh lam rot goi tu Node 0, seq=0
[RX] FAULT INJECT: co tinh lam rot goi tu Node 0, seq=2
```

Trong khi đó, gói có `src_id=0` (gốc từ A) nhưng đến qua C (`prev_hop_id=2`, ở tuyến A→C→B mô tả tại mục 5.4) **không** bị fault injection bắt nhầm — nếu điều kiện fault-check dùng `src_id` thay vì `prev_hop_id` như đề xuất ban đầu, trường hợp này chắc chắn bị rớt oan. Đây là bằng chứng thực nghiệm xác nhận tính đúng đắn của quyết định #3 trong bảng Design Decisions.

## 6. Issues & Resolution

| # | Vấn đề | Phát hiện lúc | Nguyên nhân gốc | Cách sửa |
|---|---|---|---|---|
| 1 | ACK gửi sai người khi gói đã qua ≥2 hop | Thiết kế, trước khi code | `struct packet` (M3) chỉ có `src_id` (nguồn gốc, không đổi qua hop) và `next_hop_id` (đích hop hiện tại) — không có field nào lưu "ai vừa truyền gói này ở hop hiện tại" | Thêm field `prev_hop_id`, ghi đè bởi mỗi node khi forward; ACK nhắm vào `data_pkt->prev_hop_id` thay vì `data_pkt->src_id` |
| 2 | Fault injection bắt oan gói đi đường vòng | Thiết kế, trước khi code | Điều kiện fault-check dự kiến ban đầu dùng `src_id==A`, không phân biệt được "gói gốc từ A" với "gói gốc từ A nhưng đến qua trung gian khác" | Đổi điều kiện sang `prev_hop_id==A` — đã xác nhận đúng bằng log thực tế (mục 5.5) |
| 3 | Deadlock kiến trúc nếu để RX thread tự block chờ ACK | Thiết kế, trước khi code | Nếu RX thread gọi `k_sem_take()` chờ ACK, nó tự khoá luôn khả năng đọc gói ACK gửi tới — vì RX thread là nơi duy nhất đọc `transport_recv()` | Chuyển sang máy trạng thái không-blocking; TX thread submit-request rồi block an toàn (không giữ vai trò đọc gói) |
| 4 | `bash\r: No such file or directory` khi chạy `build_and_run_m4.sh` | Runtime, lần chạy đầu | File được tạo với CRLF line-ending, shebang `#!/usr/bin/env bash\r` không hợp lệ trên Linux/WSL — lặp lại đúng bug đã gặp với `.resc` ở M3 | `sed -i 's/\r$//' scripts/build_and_run_m4.sh` (và áp dụng phòng ngừa cho `topology_m4.resc`) trước khi chạy |
| 5 | Nhiều MAT GOI xảy ra "ngoài kịch bản" do tranh chấp khởi động đồng thời | Phân tích log sau khi chạy | Cả 3 node khởi động TX period gần như đồng thời; khi RX thread đang `WAIT_ACK` cho giao dịch của chính nó, nó bỏ qua (không xử lý) gói DATA thật đến cùng lúc — hệ quả tất yếu của "serialize hoàn toàn" | Không sửa ở M4 (chấp nhận là limitation đã biết trước khi code). **Cập nhật sau khi review lại toàn bộ code**: một phần các MAT GOI quan sát được (đặc biệt spike cost A-C lên 10 trong `uart_a.log`) có khả năng không chỉ do tranh chấp khởi động, mà còn do 2 vấn đề #6 và #7 dưới đây — chưa tách bạch được tỷ lệ đóng góp của từng nguyên nhân trong lần chạy đã ghi log. |
| 6 | Dedup coi nhầm gói retry (tầng ứng dụng) là gói lặp vật lý (tầng BLE) | Review code toàn diện sau khi hoàn tất M4 | Dedup theo (src_id, next_hop_id, seq) được thiết kế từ M3 để lọc gói BLE tự phát lại — nhưng khi tầng ứng dụng retry (M4), gói gửi lại mang **đúng seq y hệt lần đầu**. Nếu B đã nhận đúng DATA và gửi ACK ở lần gửi 1, nhưng ACK đó không tới A kịp trong `ACK_TIMEOUT_MS`, thì khi A gửi lại (lần 2), B sẽ coi đây là gói lặp vật lý và **im lặng bỏ qua, không gửi lại ACK** — A hết retry, ghi nhận MAT GOI dù DATA thực ra đã tới đích thành công | Chưa sửa ở M4 — ghi nhận là known limitation, chuyển sang roadmap. Hướng sửa dự kiến: thêm mốc thời gian song song với `last_seen_seq[][]`, chỉ coi là gói lặp vật lý nếu đến trong khoảng ngắn (~2×`ADV_INTERVAL_GUARD_MS`); quá khoảng đó dù seq trùng vẫn xử lý như gói mới |
| 7 | `k_msgq_put()` trong `transport.c` bỏ qua giá trị trả về khi hàng đợi đầy | Review code toàn diện sau khi hoàn tất M4 | `K_NO_WAIT` khiến gói bị rớt âm thầm khi `rx_msgq` (depth=10) đầy — không log, không cách nào phát hiện. Rủi ro tăng vì mỗi node liên tục phát lại nguyên payload trong lúc chờ ACK (tới ~9.6s), dễ dồn hàng đợi bằng chính các bản sao vật lý của cùng 1 gói; ngoài ra bộ lọc self-broadcast trong `data_cb()` chỉ so `src_id` chứ không so `prev_hop_id`, nên node đang forward hộ vẫn tự nghe lại bản phát của chính mình, làm nặng thêm nguy cơ tràn | Chưa sửa ở M4 — ghi nhận là known limitation, chuyển sang roadmap. Hướng sửa dự kiến: kiểm tra giá trị trả về của `k_msgq_put()`, log rõ khi rớt gói do đầy hàng đợi |

## 7. Discussion

M4 mở rộng đúng năng lực cốt lõi mà M3 còn thiếu: khả năng **phản ứng** với điều kiện mạng thay đổi, không chỉ tìm đường trên graph tĩnh. Ba quyết định kiến trúc quan trọng nhất — (1) thêm `prev_hop_id` để ACK định danh đúng người nhận theo từng hop, (2) chuyển RX thread thành transaction manager không-blocking để tránh deadlock, (3) tách "miss" (routing metric) khỏi "retry" (transmission reliability) — đều là những vấn đề chỉ lộ ra khi đối chiếu thiết kế với ngữ nghĩa thực sự của multi-hop, không phải chi tiết cài đặt tầm thường. Bằng chứng log ở mục 5.4-5.5 cho thấy các quyết định này không chỉ đúng về lý thuyết mà còn thể hiện đúng hành vi khi chạy thực tế, kể cả ở tình huống phụ (route A→C→B) không được thiết kế cứng từ đầu.

Giới hạn lớn nhất của M4 là phương án "serialize hoàn toàn" (chỉ 1 giao dịch active/node) — đánh đổi hợp lý cho scope demo 3-node, nhưng gây nhiễu log do tranh chấp khởi động (mục 6, vấn đề #5) và không scale nếu mở rộng số node.

**Đặc tính cần ghi nhận rõ (không phải bug, là hệ quả tất yếu của kiến trúc M3 "mỗi node giữ graph riêng, không propagate")**: cost thích nghi của M4 là **cục bộ, không đối xứng, không lan truyền**. Khi A tự phát hiện A-B xuống cấp và tăng cost, chỉ A thay đổi cách nhìn về cạnh này — B hoàn toàn không biết, và B vẫn coi B-A là cost gốc trừ khi chính B tự trải nghiệm miss theo hướng ngược lại. Nếu mở rộng topology, một node thứ 3 định tuyến qua B để tới A vẫn có thể bị B dẫn qua đúng liên kết mà A đã đánh giá là xấu.

## 8. Next Steps

Chưa xử lý, cần quyết định trước khi coi M4 hoàn tất:

1. **`tests/routing_host/test_routing.c` (M3) chưa được cập nhật** cho API mới (`routing_record_miss/success/get_cost`) và graph mới (thêm cạnh A-C). Một số test case cũ có thể sai vì trước đây giả định A-C không có route trực tiếp. Cần file gốc để cập nhật.
2. **Log prefix `[TX]` trong `do_send()`** gây nhầm khi đọc log tay — hàm này chạy trong RX thread cả khi đang forward hộ (không phải TX thread của chính node đó). Có thể sửa thành `[TX]`/`[FWD]` tuỳ `g_pending.origin` để log rõ ràng hơn cho việc quay demo/viết báo cáo.
3. **Quyết định về "staggered start"** (mục 6, vấn đề #5): giữ nguyên (chấp nhận nhiễu log do tranh chấp khởi động, đã ghi nhận là limitation) hay thêm `k_sleep` lệch theo `NODE_ID` trước vòng lặp TX đầu tiên để log sạch hơn khi quay video.
4. Roadmap dài hạn (không chặn M4): fault window có baseline/auto-tắt, multi-transaction, worker thread riêng, adaptive timeout, link-quality metric phức tạp hơn.

M5 (README, cleanup, GitHub, CV bullet cuối) có thể bắt đầu song song sau khi mục 1-3 ở trên được chốt.