/**
 * @file routing.c
 * @brief Cài đặt Dijkstra generic O(N^2) — không hardcode tất cả các
 *        trường hợp riêng cho 3 node, để code tái sử dụng được thẳng khi
 *        M4 đổi cost động (chỉ cần gọi lại routing_set_cost(), không sửa
 *        gì trong hàm này).
 */

#include "routing.h"
#include <stdbool.h>
#include <string.h>

/** Ma trận kề — graph[i][j] là cost từ i đến j, ROUTING_COST_INF = không có cạnh. */
static uint8_t graph[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT];

void routing_set_cost(uint8_t node_a, uint8_t node_b, uint8_t cost)
{
    /* Graph vô hướng — cost phải đối xứng cả 2 chiều */
    graph[node_a][node_b] = cost;
    graph[node_b][node_a] = cost;
}

void routing_init(void)
{
    for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
        for (int j = 0; j < ROUTING_NODE_COUNT; j++) {
            graph[i][j] = (i == j) ? 0 : ROUTING_COST_INF;
        }
    }

    /* Topology cố định của M3: A=0, B=1, C=2. A-C không nối trực tiếp
     * để bắt buộc route A->C phải qua B (nếu không Dijkstra sẽ luôn
     * chọn đường thẳng và không chứng minh được gì về multi-hop). */
    routing_set_cost(0, 1, 1);
    routing_set_cost(1, 2, 1);
}

uint8_t routing_next_hop(uint8_t self_id, uint8_t dst_id)
{
    uint8_t dist[ROUTING_NODE_COUNT];
    uint8_t prev[ROUTING_NODE_COUNT];
    bool visited[ROUTING_NODE_COUNT];

    for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
        dist[i] = ROUTING_COST_INF;
        prev[i] = ROUTING_INVALID_NODE;
        visited[i] = false;
    }
    dist[self_id] = 0;

    /* Vòng lặp Dijkstra chuẩn: mỗi bước chọn node chưa thăm có dist nhỏ
     * nhất, "chốt" nó, rồi relax các cạnh kề. Ở quy mô 3 node thuật toán
     * này "over-kill" về hiệu năng nhưng được giữ nguyên dạng generic vì
     * sẽ chạy đúng y hệt khi M4 đổi cost động hoặc mở rộng số node. */
    for (int iter = 0; iter < ROUTING_NODE_COUNT; iter++) {
        uint8_t u = ROUTING_INVALID_NODE;
        uint8_t best = ROUTING_COST_INF;

        for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
            if (!visited[i] && dist[i] < best) {
                best = dist[i];
                u = i;
            }
        }
        if (u == ROUTING_INVALID_NODE) {
            break; /* Phần graph còn lại không tới được từ self_id */
        }
        visited[u] = true;

        for (int v = 0; v < ROUTING_NODE_COUNT; v++) {
            if (graph[u][v] == ROUTING_COST_INF) {
                continue;
            }
            /* Dùng uint16_t để cộng tạm thời, tránh wrap-around nếu
             * dist[u] + cost vượt quá 255 (không xảy ra ở M3 nhưng an
             * toàn hơn khi M4 có thể có graph lớn/cost lớn hơn). */
            uint16_t alt = (uint16_t)dist[u] + graph[u][v];
            if (alt < dist[v]) {
                dist[v] = (uint8_t)alt;
                prev[v] = u;
            }
        }
    }

    if (dst_id == self_id || dist[dst_id] == ROUTING_COST_INF) {
        return ROUTING_INVALID_NODE;
    }

    /* Truy ngược path từ dst_id về self_id qua mảng prev[]. Node đầu
     * tiên gặp được theo chiều ngược (path[path_len - 1]) chính là
     * next-hop theo chiều xuôi từ self_id. */
    uint8_t path[ROUTING_NODE_COUNT];
    int path_len = 0;
    uint8_t cur = dst_id;

    while (cur != self_id) {
        path[path_len++] = cur;
        cur = prev[cur];
    }

    return path[path_len - 1];
}