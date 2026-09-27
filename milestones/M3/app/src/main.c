/**
 * @file main.c
 * @brief Logic ứng dụng: TX thread tự sinh packet định kỳ và gọi
 *        Dijkstra để chọn next-hop; RX thread nhận packet, lọc theo
 *        next_hop_id, rồi hoặc xử lý (đích cuối) hoặc forward tiếp
 *        (trung gian). Đây là nơi duy nhất "hiểu" ý nghĩa của một gói
 *        mesh — transport.c chỉ biết gửi/nhận byte thô.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdio.h>
#include "transport.h"
#include "packet.h"
#include "routing.h"

#define TX_PERIOD_MS   1000
#define TTL_INITIAL    4    /**< Dư dả hop cho topology 3 node (tối đa 2 hop) */

/**
 * @brief Thread định kỳ tự sinh 1 packet DATA mỗi TX_PERIOD_MS, gửi luân
 *        phiên tới 2 node còn lại. Với mỗi lần gửi, tự chạy Dijkstra để
 *        điền next_hop_id — TX thread không biết đường đi đầy đủ, chỉ
 *        biết bước kế tiếp, đúng tính chất hop-by-hop routing.
 */
void tx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const uint8_t self_id = (uint8_t)CONFIG_APP_NODE_ID;
    uint8_t seq = 0;
    uint8_t target_idx = 0;

    /* Danh sách 2 node còn lại (khác self) để gửi luân phiên mỗi chu kỳ */
    uint8_t targets[ROUTING_NODE_COUNT - 1];
    int t = 0;
    for (uint8_t i = 0; i < ROUTING_NODE_COUNT; i++) {
        if (i != self_id) {
            targets[t++] = i;
        }
    }

    while (1) {
        uint8_t dst = targets[target_idx];
        target_idx = (target_idx + 1) % (ROUTING_NODE_COUNT - 1);

        uint8_t next_hop = routing_next_hop(self_id, dst);
        if (next_hop == ROUTING_INVALID_NODE) {
            /* Với graph cố định của M3 trường hợp này không xảy ra,
             * nhưng giữ guard để an toàn khi M4 đổi cost động. */
            printk("[TX] Khong co route toi Node %u, bo qua chu ky nay\n", dst);
            k_sleep(K_MSEC(TX_PERIOD_MS));
            continue;
        }

        struct packet pkt = {
            .type        = PACKET_TYPE_DATA,
            .src_id      = self_id,
            .dst_id      = dst,
            .next_hop_id = next_hop,
            .ttl         = TTL_INITIAL,
            .seq         = seq++,
        };
        snprintf((char *)pkt.payload, sizeof(pkt.payload), "Hello from %c",
                 (char)('A' + self_id));
        pkt.payload_len = (uint8_t)(strlen((char *)pkt.payload) + 1);

        int ret = transport_send((uint8_t *)&pkt, sizeof(pkt));
        if (ret == 0) {
            printk("[TX] Gui toi Node %u (next_hop=%u), seq=%u\n",
                   dst, next_hop, pkt.seq);
        } else {
            printk("[TX] LOI gui, seq=%u (ret=%d)\n", pkt.seq, ret);
        }

        k_sleep(K_MSEC(TX_PERIOD_MS));
    }
}

/**
 * @brief Thread xử lý mỗi packet nhận được. Ba nhánh xử lý theo thứ tự:
 *        (1) dedup theo (src_id, seq) — chống xử lý trùng do BLE tự phát
 *        lại adv trước khi payload kịp đổi; (2) lọc theo next_hop_id —
 *        đây là cơ chế bắt buộc gói phải đi đúng theo Dijkstra thay vì
 *        lợi dụng việc broadcast vật lý có thể "nghe" được từ xa; (3)
 *        nếu là đích cuối thì in ra, ngược lại thì forward tiếp.
 */
/* Dedup theo (src_id, seq, next_hop_id) thay vì chỉ (src_id, seq).
 * Lý do: seq KHÔNG đổi khi một gói bị forward qua nhiều chặng (đúng
 * thiết kế — seq là định danh của GÓI, không phải của CHẶNG), nhưng
 * next_hop_id THAY ĐỔI mỗi lần forward (node trung gian tự tính lại
 * Dijkstra). Nếu chỉ dedup theo (src_id, seq), gói forward tới đích
 * sẽ bị nhầm là "bản sao BLE tự phát lại" của chính gói gốc và bị
 * drop nhầm trước khi kịp kiểm tra next_hop_id — đây chính là bug
 * khiến M3 forward không bao giờ tới được đích cuối cùng. */
void rx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const uint8_t self_id = (uint8_t)CONFIG_APP_NODE_ID;
    uint8_t buf[APP_MSG_LEN];

/* Dedup theo cap (src_id, next_hop_id) thay vi chi src_id. Ly do: 1
 * node co the nghe duoc 2 LOAI goi khac nhau cung mang src_id giong
 * nhau gan nhu dong thoi — vi du goi nguon phat truc tiep (next_hop=X)
 * va goi da qua 1 lan forward cua chu ky truoc (next_hop=Y khac X).
 * Neu chi dung 1 o nho cho moi src_id, 2 loai goi nay se ghi de len
 * nhau, khien lan phat lai vat ly (BLE tu lap lai payload chua doi)
 * cua goi truoc bi hieu nham la goi moi — gay in trung NHAN DICH.
 * Tach dedup theo ca next_hop_id giai quyet dung goc van de nay. */
uint8_t last_seen_seq[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT];
bool has_seen[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT] = {{0}};

    while (1) {
        int ret = transport_recv(buf, sizeof(buf), K_FOREVER);
        if (ret != 0) {
            continue;
        }

        struct packet pkt;
        memcpy(&pkt, buf, sizeof(pkt));

        if (pkt.src_id >= ROUTING_NODE_COUNT) {
            continue; /* dữ liệu lạ, không thuộc topology hiện tại */
        }

        if (has_seen[pkt.src_id][pkt.next_hop_id]
            && pkt.seq == last_seen_seq[pkt.src_id][pkt.next_hop_id]) {
            continue; /* dung la BLE phat lai cua CUNG 1 cap (nguon, chang),
                       * khong phai goi moi */
        }
        has_seen[pkt.src_id][pkt.next_hop_id] = true;
        last_seen_seq[pkt.src_id][pkt.next_hop_id] = pkt.seq;

        if (pkt.next_hop_id != self_id) {
            /* Nghe được qua broadcast vật lý nhưng không phải lượt mình
             * theo logic Dijkstra — đây chính là bằng chứng routing
             * hoạt động ở tầng logic, độc lập với tầng vật lý. */
            printk("[RX] BO QUA (khong phai next-hop): src=%u dst=%u next_hop=%u\n",
                   pkt.src_id, pkt.dst_id, pkt.next_hop_id);
            continue;
        }

        if (pkt.dst_id == self_id) {
            printk("[RX] NHAN DICH: tu Node %u, seq=%u, noi dung=\"%s\"\n",
                   pkt.src_id, pkt.seq, pkt.payload);
            continue;
        }

        /* Là next-hop đúng như Dijkstra tính, nhưng chưa phải đích cuối
         * -> node này đóng vai trò trung gian, phải forward tiếp. */
        if (pkt.ttl == 0) {
            printk("[RX] BO QUA (het TTL): src=%u dst=%u\n", pkt.src_id, pkt.dst_id);
            continue;
        }
        pkt.ttl--;
        /* Tự chạy lại Dijkstra từ chính mình — đúng tính chất hop-by-hop,
         * không dùng lại next_hop cũ (vì nó là next_hop của node trước,
         * không phải của mình). */
        pkt.next_hop_id = routing_next_hop(self_id, pkt.dst_id);
        if (pkt.next_hop_id == ROUTING_INVALID_NODE) {
            printk("[RX] BO QUA (khong co route toi dich): dst=%u\n", pkt.dst_id);
            continue;
        }

        printk("[RX] FORWARD: src=%u dst=%u qua Node %u -> next_hop=%u\n",
               pkt.src_id, pkt.dst_id, self_id, pkt.next_hop_id);

        int fret = transport_send((uint8_t *)&pkt, sizeof(pkt));
        if (fret != 0) {
            printk("[RX] LOI forward: %d\n", fret);
        }
    }
}

/* Tạo thread ở trạng thái suspended (SYS_FOREVER_MS) — chỉ đánh thức
 * sau khi transport_init() (bao gồm bt_enable()) thành công, tránh
 * thread chạy trước khi BLE stack sẵn sàng. */
K_THREAD_DEFINE(tx_tid, 1024, tx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(rx_tid, 1024, rx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

int main(void)
{
    printk("Node ID = %d\n", CONFIG_APP_NODE_ID);

    /* Phải khởi tạo graph trước khi bất kỳ thread nào có thể gọi
     * routing_next_hop(), nếu không graph sẽ toàn giá trị 0 (mọi cạnh
     * cost=0) và Dijkstra sẽ cho kết quả sai mà không báo lỗi. */
    routing_init();

    int err = transport_init();
    if (err) {
        printk("Loi khoi tao transport: %d\n", err);
        return 0;
    }

    k_thread_start(tx_tid);
    k_thread_start(rx_tid);

    return 0;
}