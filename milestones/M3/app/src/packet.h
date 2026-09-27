/**
 * @file packet.h
 * @brief Định nghĩa định dạng packet dùng chung cho toàn bộ mesh, thay thế
 *        quy ước payload 5-byte tạm thời của M2. Đây là "giao thức" logic
 *        giữa các node, độc lập với tầng vận chuyển vật lý (BLE adv/scan).
 */

#ifndef PACKET_H_
#define PACKET_H_

#include <zephyr/types.h>

/**
 * Giới hạn kích thước payload để toàn bộ struct packet nằm trong ngân sách
 * 31-byte của legacy BLE advertising (AD flags ~3 byte + AD header
 * manufacturer data ~4 byte để lại ~24 byte). "Hello from X" = 12 ký tự +
 * '\0' = 13 byte, vừa khít không cần tính toán lại mỗi lần đổi payload.
 */
#define PACKET_PAYLOAD_MAX_LEN 13

/** Phân loại packet — chỉ DATA được dùng ở M3, ACK dành cho M4. */
enum packet_type {
    PACKET_TYPE_DATA = 0,
    PACKET_TYPE_ACK  = 1,
};

/**
 * @brief Định dạng 1 packet mesh, được gửi nguyên khối byte qua BLE
 *        manufacturer data (không mã hoá/giải mã riêng — memcpy trực tiếp
 *        vì tất cả node chạy cùng 1 binary/kiến trúc CPU).
 *
 * next_hop_id là điểm khác biệt quan trọng nhất so với thiết kế ban đầu:
 * nó tách biệt "đích cuối cùng" (dst_id) khỏi "trạm kế tiếp theo Dijkstra"
 * (next_hop_id), để RX có thể lọc bỏ gói nghe được qua broadcast vật lý
 * nhưng không phải lượt mình xử lý theo logic routing.
 */
struct __packed packet {
    uint8_t type;             /**< packet_type: DATA hoặc ACK */
    uint8_t src_id;            /**< Node id nguồn phát gói ban đầu */
    uint8_t dst_id;            /**< Node id đích cuối cùng */
    uint8_t next_hop_id;       /**< Node id được phép xử lý gói này ở hop hiện tại */
    uint8_t ttl;                /**< Hop limit — chống loop vô hạn nếu graph lỗi */
    uint8_t seq;                /**< Số thứ tự, dùng để dedup và (M4) khớp ACK */
    uint8_t payload_len;        /**< Số byte thực sự dùng trong payload[] */
    uint8_t payload[PACKET_PAYLOAD_MAX_LEN]; /**< Nội dung, vd "Hello from A" */
};

#endif /* PACKET_H_ */