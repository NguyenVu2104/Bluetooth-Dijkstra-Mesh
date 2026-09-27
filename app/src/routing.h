/**
 * @file routing.h
 * @brief API tính toán đường đi ngắn nhất (Dijkstra) trên một static graph
 *        cục bộ. Mỗi node giữ 1 bản copy graph riêng, khởi tạo giống nhau,
 *        và tự chạy Dijkstra độc lập — đây là mô hình hop-by-hop routing,
 *        không phải source routing.
 *
 *        Từ M4: graph không còn "tĩnh tuyệt đối" như M3 nữa — cost của
 *        từng cạnh có thể tăng lên khi phát hiện mất gói
 *        (routing_record_miss) và hồi phục lại khi gửi thành công
 *        (routing_record_success). Bản thân thuật toán Dijkstra trong
 *        routing_next_hop() không cần đổi gì cả, vì nó luôn đọc cost
 *        hiện tại của graph tại thời điểm được gọi — đây chính là lý do
 *        M3 đã cố tình viết Dijkstra tổng quát thay vì hardcode 3 node.
 */

#ifndef ROUTING_H_
#define ROUTING_H_

/* Cho phép build routing.c độc lập trên host (gcc thường, không cần
 * Zephyr) để unit test Dijkstra riêng. */
#ifdef ROUTING_HOST_TEST
#include <stdint.h>
#else
#include <zephyr/types.h>
#endif

#define ROUTING_NODE_COUNT   3
#define ROUTING_COST_INF     255   /**< "Không có cạnh" hoặc "cạnh đã coi như đứt hẳn" */
#define ROUTING_INVALID_NODE 0xFF  /**< Giá trị trả về khi không tìm được route */

/**
 * @brief Khởi tạo graph tĩnh ban đầu của M4: A(0)-B(1) cost=1, B(1)-C(2)
 *        cost=1, và A(0)-C(2) cost=5 — cạnh A-C là điểm mới so với M3,
 *        cho phép Dijkstra chọn đường thay thế thật sự khi A-B xuống
 *        cấp, thay vì chỉ mất route hoàn toàn. Cost gốc của mỗi cạnh
 *        cũng được lưu lại riêng, để routing_record_success() biết
 *        "hồi phục về giá trị nào".
 *        Phải gọi 1 lần duy nhất trong main() trước khi các thread bắt
 *        đầu gọi routing_next_hop().
 */
void routing_init(void);

/**
 * @brief Gán thẳng cost cho 1 cạnh (đối xứng cả 2 chiều). Đây là hàm mức
 *        thấp, dùng nội bộ bởi routing_init()/routing_record_miss()/
 *        routing_record_success(). Code ở main.c nên dùng 2 hàm chuyên
 *        biệt bên dưới thay vì gọi thẳng hàm này, để thể hiện đúng ý
 *        định (miss/success) thay vì tự gán 1 con số cost tuỳ ý.
 *
 * @param node_a Một đầu cạnh
 * @param node_b Đầu còn lại của cạnh
 * @param cost   Cost mới (dùng ROUTING_COST_INF để "xoá" cạnh)
 */
void routing_set_cost(uint8_t node_a, uint8_t node_b, uint8_t cost);

/**
 * @brief Ghi nhận 1 lần mất gói (hết số lần gửi cho phép mà vẫn không
 *        có ACK) trên cạnh (node_a, node_b) — nhân đôi cost hiện tại
 *        của cạnh này, chặn lại ở ROUTING_COST_INF nếu vượt quá. Gọi
 *        hàm này khi máy trạng thái gửi-có-đảm bảo ở main.c rơi vào
 *        trạng thái "mất gói".
 *
 * @param node_a Một đầu cạnh vừa bị miss
 * @param node_b Đầu còn lại
 */
void routing_record_miss(uint8_t node_a, uint8_t node_b);

/**
 * @brief Ghi nhận 1 lần gửi thành công (có ACK) trên cạnh (node_a,
 *        node_b) — đưa cost của cạnh này về đúng giá trị gốc lúc
 *        routing_init(), coi như link đã "khỏi hẳn" ngay khi có 1 lần
 *        thành công, không giảm dần từng bước. Gọi khi máy trạng thái ở
 *        main.c rơi vào trạng thái "thành công".
 *
 * @param node_a Một đầu cạnh vừa gửi thành công
 * @param node_b Đầu còn lại
 */
void routing_record_success(uint8_t node_a, uint8_t node_b);

/**
 * @brief Đọc cost hiện tại của 1 cạnh — chỉ dùng để in log cho dễ theo
 *        dõi khi demo (vd in ra cost hiện tại của A-B sau mỗi lần mất
 *        gói), không ảnh hưởng gì tới việc tính Dijkstra.
 *
 * @param node_a Một đầu cạnh
 * @param node_b Đầu còn lại
 * @return Cost hiện tại (ROUTING_COST_INF nếu không có cạnh)
 */
uint8_t routing_get_cost(uint8_t node_a, uint8_t node_b);

/**
 * @brief Chạy Dijkstra từ self_id trên graph nội bộ, trả về node id của
 *        trạm kế tiếp (next-hop) trên đường đi ngắn nhất tới dst_id.
 *
 * @param self_id Node đang gọi hàm này (gốc của Dijkstra)
 * @param dst_id  Đích cuối cùng cần tới
 * @return Node id của next-hop, hoặc ROUTING_INVALID_NODE nếu dst_id
 *         không tới được hoặc dst_id == self_id (gọi sai ngữ cảnh).
 */
uint8_t routing_next_hop(uint8_t self_id, uint8_t dst_id);

#endif /* ROUTING_H_ */