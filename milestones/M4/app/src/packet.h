/**
 * @file packet.h
 * @brief Định nghĩa định dạng packet dùng chung cho toàn bộ mesh.
 *
 *        Từ M4: struct này được dùng cho cả 2 loại gói — DATA (như M3)
 *        và ACK (mới). Một số field đổi ý nghĩa tuỳ theo loại gói, xem
 *        bảng giải thích ngay bên dưới struct.
 */

#ifndef PACKET_H_
#define PACKET_H_

#include <zephyr/types.h>

/**
 * Giới hạn kích thước payload để toàn bộ struct packet nằm trong ngân sách
 * 31-byte của legacy BLE advertising. "Hello from X" = 12 ký tự + '\0' =
 * 13 byte. Việc thêm field prev_hop_id ở M4 chỉ tốn thêm 1 byte cho phần
 * header, vẫn còn dư so với ngân sách này.
 */
#define PACKET_PAYLOAD_MAX_LEN 13

/** Phân loại packet — DATA mang nội dung thật, ACK chỉ xác nhận đã nhận
 *  DATA ở đúng 1 hop cụ thể, không mang nội dung gì thêm. */
enum packet_type {
    PACKET_TYPE_DATA = 0,
    PACKET_TYPE_ACK  = 1,
};

/**
 * @brief Định dạng 1 packet mesh, gửi nguyên khối byte qua BLE
 *        manufacturer data (không mã hoá/giải mã riêng).
 *
 * Ý nghĩa từng field thay đổi tuỳ theo `type`:
 *
 * | Field        | Khi type = DATA                            | Khi type = ACK (M4)                                  |
 * |--------------|---------------------------------------------|--------------------------------------------------------|
 * | src_id       | Node phát gói gốc, không đổi qua các hop     | Node vừa gửi ACK này (chính là mình)                   |
 * | dst_id       | Đích cuối cùng                               | Không dùng để xử lý, chỉ giữ lại cho log dễ đọc         |
 * | next_hop_id  | Trạm được phép xử lý gói ở hop hiện tại       | Node cần nhận ACK này (đang chờ ACK cho hop vừa gửi)   |
 * | prev_hop_id  | Node vừa truyền gói này ở hop hiện tại (M4)   | Không dùng                                              |
 * | ttl          | Giảm dần mỗi hop, chống loop                 | Luôn = 0 (ACK không bao giờ bị forward tiếp)           |
 * | seq          | Số hiệu của gói, không đổi qua các hop        | Echo lại đúng seq của gói DATA đang được xác nhận       |
 * | payload_len  | > 0                                           | Luôn = 0                                                |
 *
 * prev_hop_id là field mới của M4. Nó cần thiết vì next_hop_id/src_id
 * không đủ để biết "ai vừa truyền gói này cho mình" một khi gói đã đi
 * qua từ 2 hop trở lên: vd Node C nhận gói forward từ Node B, src_id
 * vẫn giữ nguyên là A (đúng quy ước không đổi từ M3), nhưng người vừa
 * phát sóng cho C thực sự là B — nếu ACK gửi nhầm về A thì A sẽ không
 * bao giờ nhận được, còn B thì không hề hay biết, gây ra 1 "mất gói
 * giả" trên đúng đoạn link (B-C) không hề có lỗi thật.
 */
struct __packed packet {
    uint8_t type;              /**< packet_type: DATA hoặc ACK */
    uint8_t src_id;             /**< Node id nguồn phát gói ban đầu (chỉ có ý nghĩa với DATA) */
    uint8_t dst_id;              /**< Node id đích cuối cùng */
    uint8_t next_hop_id;         /**< Node id được phép xử lý gói này ở hop hiện tại */
    uint8_t prev_hop_id;         /**< M4: node id vừa truyền gói này ở hop hiện tại — dùng để ACK đúng người */
    uint8_t ttl;                  /**< Hop limit — chống loop vô hạn nếu graph lỗi */
    uint8_t seq;                   /**< Số thứ tự, dùng để dedup và (M4) khớp ACK */
    uint8_t payload_len;            /**< Số byte thực sự dùng trong payload[] */
    uint8_t payload[PACKET_PAYLOAD_MAX_LEN]; /**< Nội dung, vd "Hello from A" */
};

#endif /* PACKET_H_ */