/**
 * @file main.c
 * @brief Logic ứng dụng tầng cao nhất — nơi "hiểu" ý nghĩa của một gói
 *        mesh. Từ M4, đây không còn là mô hình fire-and-forget đơn giản
 *        của M3 nữa: mọi lần gửi DATA (dù do TX thread tự sinh, hay do
 *        RX thread forward hộ) đều đi qua một "giao dịch có đảm bảo"
 *        (reliable transaction) — gửi, chờ ACK, hết giờ thì gửi lại tối
 *        đa 1 lần, và nếu vẫn không có ACK thì coi là mất gói và tăng
 *        cost của cạnh vừa dùng, để Dijkstra tự tránh nó ở lần gọi sau.
 *
 *        Điểm quan trọng nhất về mặt kiến trúc: toàn bộ giao dịch này do
 *        MỘT MÌNH RX thread quản lý, dưới dạng 1 máy trạng thái (state
 *        machine) không-blocking. TX thread không bao giờ tự gọi
 *        transport_send() cho gói DATA nữa — nó chỉ "nộp yêu cầu" cho RX
 *        thread rồi ngồi chờ kết quả. Lý do bắt buộc phải làm vậy: nếu
 *        để RX thread tự block chờ ACK (vd bằng k_sem_take), thì chính
 *        RX thread lại là thread duy nhất có khả năng đọc được gói ACK
 *        gửi tới — dẫn tới deadlock (đợi mãi một thứ mà chỉ có mình mới
 *        lấy ra được). Máy trạng thái không-blocking giải quyết đúng vấn
 *        đề này: RX thread luôn quay lại "nghe ngóng" sau mỗi bước nhỏ,
 *        dù đang ở giữa 1 giao dịch.
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

#define ACK_TIMEOUT_MS 3500 /**< Thời gian tối đa chờ ACK cho 1 lần gửi */
#define ACK_POLL_MS    500  /**< Chu kỳ "ngó lại" khi đang rảnh hoặc đang chờ ACK — không bao giờ block vô hạn */
#define MAX_RETRY      1    /**< Gửi lại tối đa 1 lần nếu hết giờ — tổng cộng tối đa 2 lần gửi/giao dịch */

#define SELF_ID ((uint8_t)CONFIG_APP_NODE_ID)

/** Các bước của 1 giao dịch gửi-có-đảm bảo, do RX thread tự chạy tuần
 *  tự. Không có bước nào block vô thời hạn, để RX thread luôn có cơ hội
 *  quay lại nghe gói mới dù đang ở giữa 1 giao dịch. */
enum tx_state {
    STATE_IDLE = 0,  /**< Không có giao dịch nào đang chạy — sẵn sàng nhận gói mới hoặc yêu cầu gửi mới */
    STATE_SEND,       /**< Vừa quyết định gửi (lần đầu hoặc gửi lại) — gọi transport_send() ngay ở bước này */
    STATE_WAIT_ACK,    /**< Đã gửi xong, đang chờ ACK trong giới hạn ACK_TIMEOUT_MS */
    STATE_RETRY,        /**< Hết giờ chờ ACK — quyết định gửi lại hay bỏ cuộc */
    STATE_MISS,          /**< Đã hết số lần gửi cho phép mà vẫn không có ACK — coi là mất gói, tăng cost link */
    STATE_SUCCESS,        /**< Vừa nhận đúng ACK đang chờ — hồi phục cost link về mức gốc */
};

/** Ai là người yêu cầu giao dịch này — quyết định lúc xong việc có cần
 *  báo kết quả ngược lại cho TX thread hay không. Node trung gian
 *  (forward hộ) không cần báo cho ai cả — đúng bản chất "ACK chỉ xác
 *  nhận 1 hop", không phải "xác nhận cả hành trình tới đích". */
enum request_origin {
    ORIGIN_TX,       /**< TX thread tự sinh gói mới muốn gửi */
    ORIGIN_FORWARD,  /**< RX thread nhận được 1 gói DATA cần forward hộ */
};

/** Trạng thái đầy đủ của giao dịch đang active. CHỈ CÓ ĐÚNG 1 GIAO DỊCH
 *  active tại một thời điểm trên mỗi node (đúng phương án "serialize
 *  hoàn toàn" đã chốt), nên không cần mảng hay mã định danh giao dịch
 *  riêng — chỉ cần đúng 1 biến toàn cục. */
struct pending_tx {
    struct packet pkt;
    enum request_origin origin;
    uint8_t attempt;   /**< Số lần đã gửi (tăng mỗi khi vào STATE_SEND) */
    int64_t deadline;  /**< Mốc thời gian tuyệt đối (k_uptime_get()) hết hạn chờ ACK hiện tại */
};

static enum tx_state g_state = STATE_IDLE;
static struct pending_tx g_pending;

/** "Hộp thư" 1 chỗ để TX thread gửi yêu cầu gửi gói mới cho RX thread.
 *  TX thread ghi vào đây rồi báo qua tx_request_sem. Việc này chỉ an
 *  toàn vì TX thread luôn chờ tx_result_sem của lần trước xong mới ghi
 *  yêu cầu kế tiếp — nên không bao giờ có chuyện 2 bên cùng đọc/ghi biến
 *  này một lúc. */
static struct packet g_tx_request_staging;
K_SEM_DEFINE(tx_request_sem, 0, 1);

/** RX thread báo kết quả giao dịch (chỉ khi origin là ORIGIN_TX) ngược
 *  lại cho TX thread qua semaphore này. TX thread block vô hạn ở đây là
 *  an toàn, vì TX thread không giữ vai trò đọc gói nào cả — không có
 *  nguy cơ deadlock như nếu để RX thread tự block chờ ACK. */
K_SEM_DEFINE(tx_result_sem, 0, 1);
static bool g_last_result_success;

/**
 * @brief Kiểm tra 1 gói vừa nhận có đúng là ACK mà giao dịch đang active
 *        chờ hay không. Chỉ cần so type + đúng người vừa ACK (chính là
 *        next-hop mình vừa gửi tới) + đúng seq, vì tại một thời điểm chỉ
 *        có tối đa 1 giao dịch đang chờ nên không cần thêm mã định danh
 *        giao dịch riêng.
 */
static bool is_matching_ack(const struct packet *pkt)
{
    return pkt->type == PACKET_TYPE_ACK
        && pkt->next_hop_id == SELF_ID
        && pkt->src_id == g_pending.pkt.next_hop_id
        && pkt->seq == g_pending.pkt.seq;
}

/**
 * @brief Gửi 1 gói ACK để xác nhận đã nhận 1 gói DATA cụ thể. Không đi
 *        qua máy trạng thái reliable — ACK không cần được bảo đảm bằng
 *        ACK khác (nếu ACK này bị mất, bên gửi DATA sẽ tự phát hiện qua
 *        timeout của chính nó và gửi lại DATA — đây chính là cơ chế
 *        chịu lỗi đã có sẵn, không cần thêm cơ chế riêng cho ACK).
 *
 * @param data_pkt Gói DATA vừa nhận, cần được xác nhận
 */
static void send_ack_for(const struct packet *data_pkt)
{
    struct packet ack = {
        .type        = PACKET_TYPE_ACK,
        .src_id      = SELF_ID,
        .dst_id      = data_pkt->dst_id,
        /* QUAN TRỌNG: phải gửi ACK về đúng người VỪA TRUYỀN gói này ở
         * hop hiện tại (prev_hop_id) — KHÔNG PHẢI về src_id gốc của
         * gói. Nếu gói đã qua forward (vd C nhận từ B, không phải trực
         * tiếp từ A), gửi nhầm ACK về A sẽ khiến A chờ mãi không có,
         * còn B (người thực sự cần biết) lại không hay biết gì, tạo ra
         * 1 "mất gói giả" trên đúng đoạn link không hề có lỗi. */
        .next_hop_id = data_pkt->prev_hop_id,
        .prev_hop_id = SELF_ID,
        .ttl         = 0,
        .seq         = data_pkt->seq,
        .payload_len = 0,
    };

    int ret = transport_send((uint8_t *)&ack, sizeof(ack));
    if (ret != 0) {
        printk("[RX] LOI gui ACK cho seq=%u: %d\n", data_pkt->seq, ret);
    }
}

/**
 * @brief Bắt đầu (hoặc bắt đầu lại, nếu là gửi lại) 1 lần gửi trong
 *        giao dịch đang active — gọi transport_send() rồi đặt deadline
 *        chờ ACK mới.
 */
static void do_send(void)
{
    g_pending.attempt++;

    int ret = transport_send((uint8_t *)&g_pending.pkt, sizeof(g_pending.pkt));
    if (ret != 0) {
        printk("[TX] LOI gui (lan %u) toi Node %u, seq=%u: %d\n",
               g_pending.attempt, g_pending.pkt.next_hop_id, g_pending.pkt.seq, ret);
    } else {
        printk("[TX] Gui (lan %u) toi Node %u, seq=%u\n",
               g_pending.attempt, g_pending.pkt.next_hop_id, g_pending.pkt.seq);
    }

    g_pending.deadline = k_uptime_get() + ACK_TIMEOUT_MS;
    g_state = STATE_WAIT_ACK;
}

/**
 * @brief Xử lý 1 vòng lặp khi đang ở STATE_WAIT_ACK — không bao giờ
 *        block quá ACK_POLL_MS, để nếu ACK không bao giờ tới (đúng kịch
 *        bản fault injection), RX thread vẫn tự kiểm tra deadline định
 *        kỳ thay vì bị treo vĩnh viễn.
 */
static void do_wait_ack(void)
{
    int64_t remaining = g_pending.deadline - k_uptime_get();

    if (remaining <= 0) {
        g_state = STATE_RETRY;
        return;
    }

    int32_t poll_ms = (remaining < ACK_POLL_MS) ? (int32_t)remaining : ACK_POLL_MS;
    uint8_t buf[APP_MSG_LEN];
    int ret = transport_recv(buf, sizeof(buf), K_MSEC(poll_ms));

    if (ret != 0) {
        /* Chưa có gói gì trong khoảng poll này — quay lại vòng lặp để
         * tự kiểm tra deadline ở lượt kế tiếp. */
        return;
    }

    struct packet pkt;
    memcpy(&pkt, buf, sizeof(pkt));

    if (is_matching_ack(&pkt)) {
        g_state = STATE_SUCCESS;
        return;
    }

    /* Gói này không phải ACK ta đang đợi — có thể là 1 gói DATA khác
     * vừa đến đúng lúc, hoặc ACK của 1 giao dịch đã kết thúc từ trước
     * tới muộn. Vì đã chọn phương án "serialize hoàn toàn", ta chấp
     * nhận bỏ qua gói này — đây là giới hạn đã biết trước của M4, không
     * phải lỗi. */
    printk("[RX] BAN cho ACK, bo qua 1 goi khac vua den\n");
}

/**
 * @brief Xử lý khi hết giờ chờ ACK ở 1 lần gửi — quyết định gửi lại hay
 *        coi là mất gói hẳn.
 */
static void do_retry(void)
{
    if (g_pending.attempt <= MAX_RETRY) {
        printk("[RX] Het gio cho ACK, thu gui lai (lan %u)\n", g_pending.attempt + 1);
        g_state = STATE_SEND;
    } else {
        g_state = STATE_MISS;
    }
}

/**
 * @brief Xử lý khi 1 giao dịch chính thức bị coi là mất gói (hết số lần
 *        gửi cho phép mà vẫn không có ACK) — đây là nơi duy nhất gọi
 *        routing_record_miss(), đúng ý tưởng cốt lõi của M4: mất gói ở
 *        tầng vận chuyển sẽ phản ánh ngược lại thành cost cao hơn ở
 *        tầng routing.
 */
static void do_miss(void)
{
    uint8_t peer = g_pending.pkt.next_hop_id;

    routing_record_miss(SELF_ID, peer);
    printk("[RX] MAT GOI: lien ket Node %u <-> Node %u, seq=%u - cost moi = %u\n",
           SELF_ID, peer, g_pending.pkt.seq, routing_get_cost(SELF_ID, peer));

    if (g_pending.origin == ORIGIN_TX) {
        g_last_result_success = false;
        k_sem_give(&tx_result_sem);
    }
    /* Nếu origin là ORIGIN_FORWARD, không có ai đang chờ kết quả này —
     * node trung gian chỉ đơn giản dừng lại ở đây. Đúng bản chất ACK đã
     * định nghĩa ("hop nhận được", không phải "gói đã tới đích cuối
     * cùng"), nên M4 không có khái niệm "báo lỗi ngược về nguồn". */

    g_state = STATE_IDLE;
}

/**
 * @brief Xử lý khi giao dịch thành công (nhận đúng ACK đang chờ) — hồi
 *        phục cost của cạnh vừa dùng về mức gốc.
 */
static void do_success(void)
{
    uint8_t peer = g_pending.pkt.next_hop_id;

    routing_record_success(SELF_ID, peer);

    if (g_pending.origin == ORIGIN_TX) {
        g_last_result_success = true;
        k_sem_give(&tx_result_sem);
    }

    g_state = STATE_IDLE;
}

/**
 * @brief Xử lý 1 gói DATA vừa nhận được khi đang STATE_IDLE — giữ gần
 *        như nguyên vẹn logic của M3 (lọc next_hop, dedup, kiểm tra
 *        đích), chỉ thêm 2 điều mới của M4: fault injection và việc gửi
 *        ACK; forward giờ đi qua máy trạng thái thay vì gọi
 *        transport_send() ngay tại chỗ như M3.
 *
 * @param pkt            Gói DATA vừa nhận (đã copy từ buffer transport)
 * @param has_seen       Mảng dedup theo (src_id, next_hop_id), y hệt M3
 * @param last_seen_seq  Mảng seq gần nhất tương ứng, y hệt M3
 */
static void handle_incoming_data(struct packet pkt,
                                  bool has_seen[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT],
                                  uint8_t last_seen_seq[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT])
{
    if (pkt.src_id >= ROUTING_NODE_COUNT) {
        return; /* dữ liệu lạ, không thuộc topology hiện tại */
    }

    if (has_seen[pkt.src_id][pkt.next_hop_id]
        && pkt.seq == last_seen_seq[pkt.src_id][pkt.next_hop_id]) {
        return; /* đúng là BLE phát lại của CÙNG 1 cặp (nguồn, chặng), không phải gói mới */
    }
    has_seen[pkt.src_id][pkt.next_hop_id] = true;
    last_seen_seq[pkt.src_id][pkt.next_hop_id] = pkt.seq;

    if (pkt.next_hop_id != SELF_ID) {
        printk("[RX] BO QUA (khong phai next-hop): src=%u dst=%u next_hop=%u\n",
               pkt.src_id, pkt.dst_id, pkt.next_hop_id);
        return;
    }

    /* M4: fault injection — chỉ Node B (id=1) và chỉ khi bật qua
     * Kconfig mới có hành vi này. Điều kiện dùng prev_hop_id (người VỪA
     * truyền gói ở hop hiện tại), KHÔNG dùng src_id (người phát gói
     * gốc) — vì nếu dùng src_id, 1 gói có nguồn gốc từ A nhưng được C
     * forward tới B (đường vòng qua cạnh A-C mới) cũng sẽ bị rớt oan,
     * trong khi đoạn link thực sự cần mô phỏng lỗi là A-B, không phải
     * "mọi gói có nguồn gốc từ A". */
    if (IS_ENABLED(CONFIG_APP_FAULT_INJECTION)
        && SELF_ID == 1 && pkt.prev_hop_id == 0) {
        printk("[RX] FAULT INJECT: co tinh lam rot goi tu Node %u, seq=%u\n",
               pkt.prev_hop_id, pkt.seq);
        return; /* không ACK, không xử lý gì thêm — mô phỏng đúng 1 gói bị mất trên sóng */
    }

    send_ack_for(&pkt);

    if (pkt.dst_id == SELF_ID) {
        printk("[RX] NHAN DICH: tu Node %u, seq=%u, noi dung=\"%s\"\n",
               pkt.src_id, pkt.seq, pkt.payload);
        return;
    }

    /* Chưa phải đích cuối — phải forward tiếp. Từ M4, forward không còn
     * "gửi rồi quên" như M3 nữa, mà trở thành 1 giao dịch mới
     * (ORIGIN_FORWARD), để chính node trung gian này cũng theo dõi được
     * cost của link nó vừa dùng để forward. */
    if (pkt.ttl == 0) {
        printk("[RX] BO QUA (het TTL): src=%u dst=%u\n", pkt.src_id, pkt.dst_id);
        return;
    }
    pkt.ttl--;
    pkt.next_hop_id = routing_next_hop(SELF_ID, pkt.dst_id);
    if (pkt.next_hop_id == ROUTING_INVALID_NODE) {
        printk("[RX] BO QUA (khong co route toi dich): dst=%u\n", pkt.dst_id);
        return;
    }
    /* Node này giờ là người truyền gói ở hop kế tiếp — phải cập nhật
     * lại prev_hop_id thành chính mình, để node nhận kế tiếp biết gửi
     * ACK về đâu (xem giải thích chi tiết trong packet.h). */
    pkt.prev_hop_id = SELF_ID;

    printk("[RX] FORWARD: src=%u dst=%u qua Node %u -> next_hop=%u\n",
           pkt.src_id, pkt.dst_id, SELF_ID, pkt.next_hop_id);

    g_pending.pkt = pkt;
    g_pending.origin = ORIGIN_FORWARD;
    g_pending.attempt = 0;
    g_state = STATE_SEND;
}

/**
 * @brief Xử lý 1 vòng lặp khi đang STATE_IDLE — ưu tiên nhận yêu cầu
 *        gửi mới từ TX thread trước (nếu có), nếu không thì mới nghe
 *        gói từ transport.
 */
static void do_idle(bool has_seen[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT],
                     uint8_t last_seen_seq[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT])
{
    if (k_sem_take(&tx_request_sem, K_NO_WAIT) == 0) {
        g_pending.pkt = g_tx_request_staging;
        g_pending.origin = ORIGIN_TX;
        g_pending.attempt = 0;
        g_state = STATE_SEND;
        return;
    }

    uint8_t buf[APP_MSG_LEN];
    int ret = transport_recv(buf, sizeof(buf), K_MSEC(ACK_POLL_MS));
    if (ret != 0) {
        return; /* không có gì trong khoảng poll này, quay lại vòng lặp */
    }

    struct packet pkt;
    memcpy(&pkt, buf, sizeof(pkt));

    if (pkt.type == PACKET_TYPE_ACK) {
        /* Đang rảnh (IDLE) nghĩa là không có giao dịch nào đang chờ ACK
         * — đây chắc chắn là ACK đến muộn của 1 giao dịch đã kết thúc
         * từ trước, bỏ qua. */
        return;
    }

    handle_incoming_data(pkt, has_seen, last_seen_seq);
}

/**
 * @brief RX thread — từ M4 kiêm luôn vai trò "người quản lý giao dịch":
 *        vừa nhận gói vừa điều khiển máy trạng thái gửi-có-đảm bảo.
 *        Vòng lặp chính chỉ đơn giản là "làm 1 bước của state hiện tại
 *        rồi lặp lại" — mọi bước đều có giới hạn thời gian chờ
 *        (ACK_POLL_MS hoặc ngắn hơn), không bước nào block vô hạn.
 */
void rx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    /* Dedup theo cặp (src_id, next_hop_id) — y hệt cơ chế đã chốt ở M3,
     * không đổi gì ở M4. */
    uint8_t last_seen_seq[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT];
    bool has_seen[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT] = {{0}};

    while (1) {
        switch (g_state) {
        case STATE_IDLE:
            do_idle(has_seen, last_seen_seq);
            break;
        case STATE_SEND:
            do_send();
            break;
        case STATE_WAIT_ACK:
            do_wait_ack();
            break;
        case STATE_RETRY:
            do_retry();
            break;
        case STATE_MISS:
            do_miss();
            break;
        case STATE_SUCCESS:
            do_success();
            break;
        }
    }
}

/**
 * @brief TX thread — từ M4 không còn tự gửi trực tiếp nữa, mà "nộp yêu
 *        cầu" cho RX thread rồi chờ kết quả. Nhờ vậy traffic tự sinh
 *        (không phải forward) cũng được đo cost đúng như traffic
 *        forward — đây chính là lỗ hổng đã phát hiện và sửa trong lúc
 *        thiết kế: nếu chỉ RX thread mới đo cost, cost của 1 cạnh sẽ
 *        chỉ tăng khi có ai đó forward qua nó, không tăng khi chính
 *        node đó là nguồn phát.
 */
void tx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint8_t seq = 0;
    uint8_t target_idx = 0;

    /* Danh sách 2 node còn lại (khác self) để gửi luân phiên mỗi chu kỳ */
    uint8_t targets[ROUTING_NODE_COUNT - 1];
    int t = 0;
    for (uint8_t i = 0; i < ROUTING_NODE_COUNT; i++) {
        if (i != SELF_ID) {
            targets[t++] = i;
        }
    }

    while (1) {
        uint8_t dst = targets[target_idx];
        target_idx = (target_idx + 1) % (ROUTING_NODE_COUNT - 1);

        uint8_t next_hop = routing_next_hop(SELF_ID, dst);
        if (next_hop == ROUTING_INVALID_NODE) {
            printk("[TX] Khong co route toi Node %u, bo qua chu ky nay\n", dst);
            k_sleep(K_MSEC(TX_PERIOD_MS));
            continue;
        }

        struct packet pkt = {
            .type        = PACKET_TYPE_DATA,
            .src_id      = SELF_ID,
            .dst_id      = dst,
            .next_hop_id = next_hop,
            .prev_hop_id = SELF_ID, /* Ở hop đầu tiên, người vừa "truyền" chính là mình */
            .ttl         = TTL_INITIAL,
            .seq         = seq++,
        };
        snprintf((char *)pkt.payload, sizeof(pkt.payload), "Hello from %c",
                 (char)('A' + SELF_ID));
        pkt.payload_len = (uint8_t)(strlen((char *)pkt.payload) + 1);

        /* Nộp yêu cầu cho RX thread rồi chờ kết quả — không tự gọi
         * transport_send() nữa (khác M3). */
        g_tx_request_staging = pkt;
        k_sem_give(&tx_request_sem);
        k_sem_take(&tx_result_sem, K_FOREVER);

        printk("[TX] Ket qua gui toi Node %u qua next_hop=%u, seq=%u: %s\n",
               dst, next_hop, pkt.seq, g_last_result_success ? "THANH CONG" : "MAT GOI");

        k_sleep(K_MSEC(TX_PERIOD_MS));
    }
}

/* Tạo thread ở trạng thái suspended (SYS_FOREVER_MS) — chỉ đánh thức
 * sau khi transport_init() (bao gồm bt_enable()) thành công, tránh
 * thread chạy trước khi BLE stack sẵn sàng. Không đổi gì so với M3. */
K_THREAD_DEFINE(tx_tid, 1024, tx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(rx_tid, 1024, rx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

int main(void)
{
    printk("Node ID = %d\n", CONFIG_APP_NODE_ID);

    /* Phải khởi tạo graph trước khi bất kỳ thread nào có thể gọi
     * routing_next_hop(), nếu không graph sẽ toàn giá trị 0 và Dijkstra
     * sẽ cho kết quả sai mà không báo lỗi. */
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