/**
 * @file routing.h
 * @brief API tính toán đường đi ngắn nhất (Dijkstra) trên một static graph
 *        cục bộ. Mỗi node giữ 1 bản copy graph riêng, khởi tạo giống nhau,
 *        và tự chạy Dijkstra độc lập — đây là mô hình hop-by-hop routing,
 *        không phải source routing.
 */

#ifndef ROUTING_H_
#define ROUTING_H_

/* Cho phép build routing.c độc lập trên host (gcc thường, không cần
 * Zephyr) để unit test Dijkstra riêng — cô lập lỗi thuật toán khỏi lỗi
 * tầng BLE/Renode khi demo không chạy đúng. Truyền -DROUTING_HOST_TEST
 * lúc compile trên host để kích hoạt nhánh này. */
#ifdef ROUTING_HOST_TEST
#include <stdint.h>
#else
#include <zephyr/types.h>
#endif

#define ROUTING_NODE_COUNT   3
#define ROUTING_COST_INF     255   /**< Giá trị đại diện cho "không có cạnh" */
#define ROUTING_INVALID_NODE 0xFF  /**< Giá trị trả về khi không tìm được route */

/**
 * @brief Khởi tạo graph tĩnh về trạng thái ban đầu của M3: A(0)-B(1)
 *        cost=1, B(1)-C(2) cost=1, A(0)-C(2) không có cạnh trực tiếp.
 *        Phải gọi 1 lần duy nhất trong main() trước khi các thread bắt đầu
 *        gọi routing_next_hop().
 */
void routing_init(void);

/**
 * @brief Cập nhật cost 1 cạnh trong graph (đối xứng cả 2 chiều). Dành cho
 *        M4 khi ACK-timeout kích hoạt tăng cost để kích hoạt reroute —
 *        chưa được gọi ở đâu trong M3.
 *
 * @param node_a Một đầu cạnh
 * @param node_b Đầu còn lại của cạnh
 * @param cost   Cost mới (dùng ROUTING_COST_INF để "xoá" cạnh)
 */
void routing_set_cost(uint8_t node_a, uint8_t node_b, uint8_t cost);

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