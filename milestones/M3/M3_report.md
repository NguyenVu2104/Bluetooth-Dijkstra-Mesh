# M3 — Technical Design & Test Report

## 1. Metadata

| Trường | Giá trị |
|---|---|
| Project | Bluetooth Dijkstra Mesh (trước đây: Bluetooth Dijkstra Simplified Mesh) |
| Milestone | M3 — `struct packet` chính thức + Dijkstra forwarding thật |
| Trạng thái | **PASS** |
| Môi trường | Windows + WSL2 Ubuntu, Zephyr 3.7 LTS, board ảo `nrf52840dk/nrf52840`, Renode 1.17.0 (1.17.0+20260916git84b37e724) |

---

## 2. Objective & Scope

### Mục tiêu milestone

Thay thế quy ước payload 5-byte tạm thời của M2 (byte0=NODE_ID, byte1-4=counter) bằng một định dạng packet chính thức (`struct packet`), đồng thời hiện thực **Dijkstra forwarding thật**: gói tin gửi từ Node A tới Node C phải đi qua Node B (multi-hop), với next-hop tại mỗi chặng được tính bằng thuật toán Dijkstra chạy độc lập trên từng node (hop-by-hop routing), không phải source routing.

### Definition of Done

- Node A gửi gói tới Node C (không có liên kết vật lý/logic trực tiếp) và Node C phải nhận được nội dung đúng, thông qua Node B làm trung gian.
- Node B phải log rõ ràng hành động forward (nhận gói không phải đích cuối, tự tính lại next-hop, gửi tiếp).
- Cơ chế routing phải hoạt động ở **tầng logic**, độc lập với hành vi broadcast vật lý của BLE advertising — cụ thể: nếu một node nghe được sóng vật lý của một node khác nhưng không phải next-hop hợp lệ theo Dijkstra, node đó phải chủ động bỏ qua gói, không xử lý.
- Không có gói bị xử lý trùng lặp (duplicate) do cơ chế dedup sai.
- Demo chạy ổn định qua nhiều chu kỳ liên tục (không crash, không treo) trên Renode.

### Ngoài scope (dời sang M4)

ACK, timeout, retry, cập nhật cost động (dynamic cost-update) khi liên kết lỗi, demo reroute khi đổi cost. M3 dùng static graph cố định, không có cơ chế phát hiện lỗi liên kết.

---

## 3. Design Decisions

| Decision | Alternatives đã cân nhắc | Rationale |
|---|---|---|
| Routing model: **hop-by-hop** — mỗi node giữ bản copy graph cục bộ, tự chạy Dijkstra độc lập tới `dst_id`, forward tới next-hop cục bộ tính được | Source routing — node nguồn tính toàn bộ path, nhúng vào packet | Không cần đồng bộ trạng thái path toàn cục giữa các node; mỗi node độc lập quyết định, đúng bản chất distributed routing; dễ mở rộng khi M4 cần cost động (chỉ cần đổi graph cục bộ, không cần đổi giao thức packet) |
| Static graph: A(0)-B(1) cost=1, B(1)-C(2) cost=1, **A(0)-C(2) không có edge trực tiếp** | Graph đầy đủ (mesh 3 cạnh, mọi node nối trực tiếp nhau) | Nếu A-C nối trực tiếp, Dijkstra(A→C) luôn chọn đường thẳng — không chứng minh được gì về multi-hop forwarding, phá vỡ mục tiêu cốt lõi của M3 |
| `struct packet` có field **`next_hop_id`** tách biệt với `dst_id` | Chỉ dùng `dst_id`, dựa vào giả định topology logic để "coi như" các node không liên kết trực tiếp không nghe được nhau | BLE advertising là broadcast vật lý thật — mọi node trong tầm nghe (theo mô hình radio của Renode) đều nhận được gói bất kể topology logic có cho phép hay không. `next_hop_id` cho phép RX filter tại tầng logic, tách biệt hoàn toàn khỏi hành vi phát sóng vật lý — đây là bằng chứng kỹ thuật quan trọng nhất của M3 |
| Payload là chuỗi có nghĩa (`"Hello from X"`) thay cho counter tăng dần của M2 | Giữ nguyên counter | Dễ đọc log khi quay demo cho CV/phỏng vấn; M3 không cần kiểm tra tính liên tục dữ liệu như mục đích ban đầu của counter ở M2 |
| `transport_send()` dùng **mutex + guard delay cố định** (`ADV_INTERVAL_GUARD_MS = 1300`) sau mỗi lần ghi payload | Chỉ dùng mutex đơn thuần (không chờ) | Xem chi tiết Mục 6.B2 — mutex đơn thuần chỉ đảm bảo an toàn truy cập đồng thời ở tầng API, không đảm bảo payload đã thực sự phát sóng ra trước khi bị lệnh gửi kế tiếp ghi đè |
| Dedup key = **`(src_id, next_hop_id)`**, mảng 2 chiều lưu `seq` gần nhất cho mỗi cặp | `(src_id)` đơn thuần (M2); `(src_id, seq)` (lần sửa đầu của M3) | Xem chi tiết Mục 6.B1 và 6.B3 — một node có thể nghe nhiều loại gói khác nhau cùng `src_id` gần như đồng thời (gói gốc và gói đã qua 1 lần forward, mang `next_hop_id` khác nhau); dedup phải phân biệt theo cặp (nguồn, chặng), không chỉ theo nguồn hay theo số thứ tự |
| `CreateFileBackend` trong `topology_m3.resc` dùng **absolute path** | Relative path (cách M2 dùng cho `showAnalyzer`, `LoadPlatformDescription`, và cách `CreateFileBackend` được thiết kế ban đầu) | Xem chi tiết Mục 6.A3 — relative path gây lỗi không rõ nguyên nhân cụ thể trong Renode 1.17.0 dù thư mục tồn tại và có quyền ghi đầy đủ; absolute path loại bỏ hoàn toàn phụ thuộc vào cơ chế resolve path nội bộ (chưa xác định chính xác) của lệnh này |

---

## 4. Implementation Summary

### File mới

- **`app/src/packet.h`** — định nghĩa `struct packet` (`type`, `src_id`, `dst_id`, `next_hop_id`, `ttl`, `seq`, `payload_len`, `payload[13]`), `enum packet_type`. Struct `__packed`, kích thước cố định vừa trong ngân sách 31-byte của legacy BLE advertising.
- **`app/src/routing.h` / `routing.c`** — thuật toán Dijkstra generic O(N²) chạy trên ma trận kề tĩnh 3×3. API: `routing_init()`, `routing_set_cost()` (dành cho M4, chưa dùng ở M3), `routing_next_hop(self_id, dst_id)`.
- **`tests/routing_host/test_routing.c`** — unit test độc lập cho `routing_next_hop()`, build bằng `gcc` thường (cờ `-DROUTING_HOST_TEST`), không cần Zephyr/Renode. 6 test case, tất cả PASS trước khi tích hợp vào firmware.
- **`renode/topology_m3.resc`**, **`scripts/build_and_run_m3.sh`** — bản sao có điều chỉnh từ M2, dùng absolute path cho `CreateFileBackend`, script có `mkdir -p`/`rm -f logs/M3/*.log` và `tee` toàn bộ output build + Renode monitor (cờ `--console`).

### File sửa

- **`app/src/transport.h`** — `APP_MSG_LEN` đổi từ hằng số 5 (M2) sang `sizeof(struct packet)`.
- **`app/src/transport.c`** — `data_cb()` đọc `src_id` qua `struct packet` (thay vì `data[0]` thô của M2); thêm `struct k_mutex send_lock` và logic guard delay trong `transport_send()` (chi tiết Mục 6.B2).
- **`app/src/main.c`** — viết lại toàn bộ. TX thread luân phiên gửi tới 2 node còn lại, tự gọi `routing_next_hop()` để điền `next_hop_id` mỗi lần gửi. RX thread: dedup → filter theo `next_hop_id` → xử lý đích cuối hoặc forward tiếp (giảm TTL, tự chạy lại Dijkstra từ chính mình).
- **`app/CMakeLists.txt`** — thêm `src/routing.c` vào `target_sources()`.

### Đoạn logic lõi (minh hoạ quyết định #3 — next-hop filter)

```c
if (pkt.next_hop_id != self_id) {
    /* Nghe duoc qua broadcast vat ly nhung khong phai luot minh
     * theo logic Dijkstra — day chinh la bang chung routing
     * hoat dong o tang logic, doc lap voi tang vat ly. */
    printk("[RX] BO QUA (khong phai next-hop): ...\n");
    continue;
}
if (pkt.dst_id == self_id) {
    printk("[RX] NHAN DICH: ...\n");
    continue;
}
/* La next-hop dung, chua phai dich -> forward tiep */
pkt.next_hop_id = routing_next_hop(self_id, pkt.dst_id);
```

Toàn bộ code chi tiết nằm trong các file tương ứng, không lặp lại ở đây.

---

## 5. Test Procedure & Evidence

### Cách chạy

```bash
./scripts/build_and_run_m3.sh
```
Script build 3 firmware riêng (`build_a`/`build_b`/`build_c`, khác nhau qua `-DCONFIG_APP_NODE_ID`), sau đó chạy `renode --console renode/topology_m3.resc`, ghi log UART từng node và log build vào `logs/M3/`.

### Evidence — packet 2-hop A→C (qua B)

`uart_a.log` (Node A gửi):
```
[TX] Gui toi Node 2 (next_hop=1), seq=1
```
`uart_b.log` (Node B forward):
```
[RX] FORWARD: src=0 dst=2 qua Node 1 -> next_hop=2
[TX] Gui toi Node 2 (next_hop=2), seq=3
```
`uart_c.log` (Node C nhận đích):
```
[RX] NHAN DICH: tu Node 0, seq=1, noi dung="Hello from A"
```
→ Xác nhận gói `seq=1` phát từ A tới đúng C, qua trung gian B, nội dung không sai lệch.

### Evidence — packet 2-hop C→A (chiều ngược)

`uart_c.log`:
```
[TX] Gui toi Node 0 (next_hop=1), seq=0
```
`uart_b.log`:
```
[RX] FORWARD: src=2 dst=0 qua Node 1 -> next_hop=0
```
`uart_a.log`:
```
[RX] NHAN DICH: tu Node 2, seq=0, noi dung="Hello from C"
```

### Evidence — next-hop filter hoạt động độc lập với broadcast vật lý

`uart_a.log` (A nghe được trực tiếp sóng của B gửi cho C, nhưng đúng logic không xử lý):
```
[RX] BO QUA (khong phai next-hop): src=1 dst=2 next_hop=2
```
`uart_c.log` (C nghe được trực tiếp sóng của A dù A-C không nối logic, nhưng tự chặn):
```
[RX] BO QUA (khong phai next-hop): src=0 dst=2 next_hop=1
```
Đây là bằng chứng quan trọng nhất của M3: routing được quyết định hoàn toàn ở tầng logic (`next_hop_id`), không phụ thuộc việc mô hình radio của Renode có cho phép 2 node "nghe" được nhau ở tầng vật lý hay không.

### Evidence — không còn duplicate sau fix dedup cuối cùng

Rà toàn bộ `uart_a.log`/`uart_b.log`/`uart_c.log` (mỗi file ~30 chu kỳ liên tục, `seq` chạy tới 28-31): không có cặp `(src_id, seq)` nào bị in `NHAN DICH` 2 lần. (Trước fix cuối, ví dụ lỗi cụ thể: `Node 2, seq=6` từng bị in trùng trong `uart_a.log` — xem Mục 6.B3.)

### Về `renode_monitor.log`

File này được đính kèm nhưng không chứa nội dung cần trích dẫn thêm cho phần evidence (mọi bằng chứng nghiệp vụ nằm trong 3 file UART) — chỉ dùng để xác nhận Renode khởi động và include script thành công, không phát sinh lỗi runtime khác ngoài các lỗi tooling đã ghi nhận ở Mục 6.A.

---

## 6. Issues Encountered & Resolution

M3 phát sinh số lượng lỗi đáng kể hơn hẳn M2, chia làm 2 nhóm bản chất khác nhau.

### 6.A — Nhóm lỗi tooling / môi trường (Renode)

**A1. `CreateFileBackend` báo lỗi "File ... could not be created" — nguyên nhân: file `.resc` chứa CRLF line-ending**
Phát hiện qua `file topology_m3.resc` cho kết quả "ASCII text, with CRLF line terminators", và `cat -A` cho thấy ký tự `^M` cuối mỗi dòng lệnh `CreateFileBackend`. Nguyên nhân: file được tạo/sửa qua công cụ phía Windows không chuẩn hoá line-ending Unix. Khắc phục: `sed -i 's/\r$//' renode/topology_m3.resc`.

**A2. Biến `$ORIGIN` không được Renode substitute trong tham số của `CreateFileBackend`**
Sau khi sửa CRLF, thử dùng `$ORIGIN/../logs/M3/uart_a.log` (dựa trên giả định `$ORIGIN` được thay thế phổ quát trong mọi lệnh Monitor) — lỗi mới cho thấy chuỗi `$ORIGIN` xuất hiện **nguyên văn, chưa được resolve** trong thông báo lỗi. Đây là giả định sai của quá trình debug (không phải lỗi phía người thực hiện): biến path của Renode không được substitute đồng nhất trong mọi ngữ cảnh lệnh. Khắc phục: quay lại dùng relative path thuần (như M2).

**A3. Relative path vẫn lỗi dù đã xác nhận `$CWD` đúng và quyền ghi bình thường — dùng absolute path**
Sau khi loại trừ CRLF, path resolution (`$CWD`/`$ORIGIN` in ra đúng giá trị), quyền ghi (`touch` test thành công), và loại trừ khả năng tồn tại thư mục giả trùng tên — `CreateFileBackend` với relative path **vẫn** báo lỗi tương tự, trong khi `LoadPlatformDescription` (cũng dùng relative path) chạy bình thường trong cùng phiên. Tra cứu mã nguồn C# gốc của Renode xác nhận `CreateFileBackend` dùng `File.Open(path, FileMode.CreateNew)` nhưng không tìm được tài liệu chính thức lý giải khác biệt về cơ chế resolve path so với các lệnh khác. **Nguyên nhân gốc chưa được xác định đầy đủ** — kết luận thực dụng: đây là hành vi đặc thù/giới hạn của `CreateFileBackend` trong Renode 1.17.0, không phải lỗi cấu hình phía người thực hiện. Khắc phục: dùng absolute path — chạy thành công ngay lần đầu.

**A4. Output của Renode không vào được `tee` do mặc định mở cửa sổ GUI riêng**
`renode script.resc` mặc định mở Monitor trong cửa sổ GUI tách biệt khỏi `stdout` của terminal gọi lệnh, khiến `2>&1 | tee` không bắt được nội dung. Khắc phục: thêm cờ `--console` để Monitor chạy ngay trong terminal hiện tại.

### 6.B — Nhóm lỗi logic ứng dụng (application logic)

**B1. Dedup theo `(src_id, seq)` khiến gói forward hợp lệ bị loại bỏ nhầm là "gói trùng lặp"**
Lần chạy demo đầu tiên: `uart_b.log` log đầy đủ `FORWARD` cho mọi gói 2-hop, nhưng `uart_c.log`/`uart_a.log` không có `NHAN DICH` tương ứng nào. Nguyên nhân: `seq` không đổi khi một gói bị forward qua nhiều chặng (đúng thiết kế — `seq` là định danh của gói, không phải của chặng), nhưng bản dedup ban đầu chỉ so `(src_id, seq)` — nên tại node đích, gói gốc nghe được qua broadcast vật lý (bị `BO QUA` đúng do next-hop sai) đã "chiếm" ô nhớ dedup của `(src_id, seq)` đó, khiến gói forward hợp lệ đến sau (cùng `src_id`, cùng `seq`) bị hiểu nhầm là bản sao BLE tự phát lại và bị `continue` trước khi kịp kiểm tra `next_hop_id`. Khắc phục lần 1: thêm `next_hop_id` vào key dedup thành bộ ba `(src_id, seq, next_hop_id)`.

**B2. Race condition trong `transport_send()` gây mất gói 2-hop 100% có hệ thống**
Sau khi sửa B1, gói 2-hop vẫn không tới đích ở lần chạy kế tiếp, dù `uart_b.log` vẫn log `FORWARD` đầy đủ không sót lần nào. Nguyên nhân: `bt_le_adv_update_data()` chỉ cập nhật nội dung, dữ liệu mới chỉ thực sự phát sóng ở chu kỳ advertising kế tiếp theo lịch trình cũ (~1.0-1.2s). Tại Node B, TX thread (định kỳ) và RX thread (khi forward) dùng chung 1 "khe" advertising duy nhất; do cả 3 node khởi động gần như đồng thời trong Renode, 2 luồng ghi tại B liên tục ghi đè lẫn nhau trước khi payload trước kịp phát sóng thật — lệnh ghi sau luôn thắng. Đây là nguyên nhân hệ thống (100%, không ngẫu nhiên), giải thích vì sao traffic 1-hop (B↔A, B↔C) luôn thành công còn traffic 2-hop (A↔C qua B) luôn thất bại. Một mutex đơn thuần (đã thêm trước đó để phòng race điều kiện trên state nội bộ BT stack) không giải quyết được vấn đề này vì nó chỉ đảm bảo *thứ tự* thực thi, không đảm bảo *thời gian tồn tại* của payload trước khi bị ghi đè. Khắc phục: thêm `k_sleep(K_MSEC(ADV_INTERVAL_GUARD_MS))` (1300ms) ngay sau khi ghi payload thành công, trước khi nhả mutex — đảm bảo mỗi payload có ít nhất 1 chu kỳ advertising thực sự phát sóng trước khi có thể bị ghi đè bởi lệnh gửi kế tiếp. Đánh đổi: giảm throughput gửi tối đa xuống ~1 gói/1.3s mỗi node — chấp nhận được ở quy mô demo 3 node.

**B3. Dedup theo `(src_id, seq, next_hop_id)` (1 ô nhớ/src_id) vẫn gây in trùng `NHAN DICH`**
Sau khi B1 và B2 được sửa, traffic 2-hop đã tới đích đúng, nhưng rà log phát hiện một số dòng `NHAN DICH` bị in 2 lần cho cùng `(src_id, seq)` (ví dụ `Node 2, seq=6` trong `uart_a.log`). Nguyên nhân: cơ chế dedup chỉ có 1 ô nhớ duy nhất cho mỗi `src_id`, lưu giá trị `(seq, next_hop_id)` gần nhất. Một node có thể nghe được gần như đồng thời 2 *loại* gói khác nhau cùng `src_id` (gói gốc phát trực tiếp, và gói đã qua 1 lần forward của chu kỳ trước, mang `next_hop_id` khác nhau) — khi 2 loại này xen kẽ theo thời gian, chúng ghi đè lẫn nhau lên cùng 1 ô nhớ, khiến lần BLE tự phát lại tiếp theo của loại gói bị ghi đè trước đó bị hiểu nhầm là gói mới. Khắc phục cuối cùng: đổi cấu trúc dedup thành mảng 2 chiều theo cặp `(src_id, next_hop_id)`, mỗi cặp có ô nhớ `seq` riêng — loại bỏ hoàn toàn xung đột ghi đè giữa 2 loại gói khác chặng.

---

## 7. Discussion

Kết quả cuối cùng khớp đầy đủ với Definition of Done đặt ra ở Mục 2: forwarding 2 chiều A↔C qua B hoạt động đúng, next-hop filter chứng minh được routing tách biệt khỏi tầng vật lý, không còn xử lý trùng lặp, demo chạy ổn định qua 30+ chu kỳ liên tục không lỗi.

**Giới hạn còn lại:**
- Throughput bị giới hạn nhân tạo bởi `ADV_INTERVAL_GUARD_MS` (~1 gói/1.3s/node) — chấp nhận được cho demo nhưng không phải giải pháp tối ưu; thiết kế đúng đắn hơn (hàng đợi ưu tiên tách biệt TX định kỳ và forward khẩn cấp) nằm ngoài scope M3.
- Nguyên nhân gốc chính xác của lỗi `CreateFileBackend` với relative path (Mục 6.A3) chưa được xác định đầy đủ — chỉ có workaround (absolute path), chưa có lời giải thích kỹ thuật dứt khoát từ tài liệu chính thức Renode.
- Mất gói do broadcast BLE (không liên quan tới các bug đã sửa) vẫn có thể xảy ra ngẫu nhiên về mặt lý thuyết — M3 chưa có cơ chế phát hiện/khắc phục (đây chính là lý do M4 cần ACK+retry).

---

## 8. Next Steps

- Bắt đầu **M4**: ACK + timeout + retry + cost-update + demo reroute khi đổi cost liên kết.
- Carry-over (không chặn M4, làm khi thuận tiện): thêm `pkt.seq` vào các dòng log `FORWARD`/`BO QUA` trong `main.c` để dễ đối chiếu tay khi M4 cần khớp ACK với `seq` cụ thể.
- Cân nhắc (không bắt buộc): tìm hiểu thêm nguyên nhân gốc của vấn đề `CreateFileBackend` + relative path (Mục 6.A3) nếu có thời gian rảnh — không chặn tiến độ project.
- M5 (viết song song, chưa chặn): cập nhật README với sơ đồ kiến trúc mới của M3 (packet format, next-hop filter) và bổ sung mục "Known Issues" tóm tắt Mục 6 của báo cáo này.