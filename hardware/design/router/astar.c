/* astar.c — grid maze router core for gen_pcb.py (2 routing layers + vias).
 *
 * Grid: L layers x H rows x W cols, index = (l*H + y)*W + x.
 *   blocked[i]  != 0 : centre of cell i is too close to foreign copper for this net's track
 *   viaok[y*W+x]!= 0 : a via of this net may be centred here (checked on every layer by the caller)
 *   src[i], dst[i]   : multi-source / multi-target sets
 *   lcost[l]         : per-step cost multiplier for layer l (x10 fixed point: 10 = 1.0)
 *   hist[i]          : extra cost for entering cell i (congestion history, rip-up and reroute)
 * Moves: 8-neighbour in-layer (10 / 14 x lcost/10), layer change at a viaok cell (via_cost).
 * Heuristic: octile distance to the bounding box of the targets (admissible with min lcost).
 * Returns the number of cells in out[] (target -> source order), 0 = no path, -1 = out of memory.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t f; int32_t i; } node_t;

static void heap_push(node_t *h, int *n, node_t v)
{
    int k = (*n)++;
    while (k > 0) {
        int p = (k - 1) / 2;
        if (h[p].f <= v.f) break;
        h[k] = h[p]; k = p;
    }
    h[k] = v;
}

static node_t heap_pop(node_t *h, int *n)
{
    node_t top = h[0], last = h[--(*n)];
    int k = 0;
    for (;;) {
        int c = 2 * k + 1;
        if (c >= *n) break;
        if (c + 1 < *n && h[c + 1].f < h[c].f) c++;
        if (h[c].f >= last.f) break;
        h[k] = h[c]; k = c;
    }
    h[k] = last;
    return top;
}

int astar(int W, int H, int L, const uint8_t *blocked, const uint8_t *viaok, const uint8_t *src, const uint8_t *dst,
          const int32_t *lcost, int32_t via_cost, const uint16_t *hist, int32_t *out, int max_out, int64_t max_expand)
{
    const int N = W * H * L, WH = W * H;
    int32_t *g = malloc(sizeof(int32_t) * N);
    int32_t *par = malloc(sizeof(int32_t) * N);
    uint8_t *closed = calloc(N, 1);
    int cap = 1 << 20, hn = 0;
    node_t *heap = malloc(sizeof(node_t) * cap);
    if (!g || !par || !closed || !heap) { free(g); free(par); free(closed); free(heap); return -1; }
    int bx0 = W, by0 = H, bx1 = -1, by1 = -1;
    for (int i = 0; i < N; i++) {
        g[i] = INT32_MAX; par[i] = -1;
        if (dst[i]) {
            int p = i % WH, x = p % W, y = p / W;
            if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y;
        }
    }
    int32_t lmin = 1 << 30;
    for (int l = 0; l < L; l++) if (lcost[l] < lmin) lmin = lcost[l];
#define HEUR(ii) ({ int _p = (ii) % WH, _x = _p % W, _y = _p / W; \
        int _dx = _x < bx0 ? bx0 - _x : (_x > bx1 ? _x - bx1 : 0); \
        int _dy = _y < by0 ? by0 - _y : (_y > by1 ? _y - by1 : 0); \
        int _mn = _dx < _dy ? _dx : _dy, _mx = _dx < _dy ? _dy : _dx; \
        (int32_t)(((int64_t)(10 * (_mx - _mn) + 14 * _mn) * lmin) / 10); })
    for (int i = 0; i < N; i++) {
        if (src[i] && !blocked[i]) {
            g[i] = 0;
            node_t v = { HEUR(i), i };
            if (hn >= cap) { cap *= 2; heap = realloc(heap, sizeof(node_t) * cap); }
            heap_push(heap, &hn, v);
        }
    }
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    static const int DC[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };
    int found = -1;
    int64_t expanded = 0;
    while (hn > 0) {
        node_t cur = heap_pop(heap, &hn);
        int i = cur.i;
        if (closed[i]) continue;
        closed[i] = 1;
        if (dst[i]) { found = i; break; }
        if (++expanded > max_expand) break;
        int l = i / WH, p = i % WH, x = p % W, y = p / W;
        for (int k = 0; k < 8; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int j = l * WH + ny * W + nx;
            if (blocked[j] || closed[j]) continue;
            if (k >= 4) {   /* no diagonal corner cutting */
                if (blocked[l * WH + y * W + nx] || blocked[l * WH + ny * W + x]) continue;
            }
            int32_t ng = g[i] + DC[k] * lcost[l] / 10 + (hist ? hist[j] : 0);
            if (ng < g[j]) {
                g[j] = ng; par[j] = i;
                node_t v = { ng + HEUR(j), j };
                if (hn >= cap) { cap *= 2; heap = realloc(heap, sizeof(node_t) * cap); }
                heap_push(heap, &hn, v);
            }
        }
        if (viaok[p]) {
            for (int nl = 0; nl < L; nl++) {
                if (nl == l) continue;
                int j = nl * WH + p;
                if (blocked[j] || closed[j]) continue;
                int32_t ng = g[i] + via_cost;
                if (ng < g[j]) {
                    g[j] = ng; par[j] = i;
                    node_t v = { ng + HEUR(j), j };
                    if (hn >= cap) { cap *= 2; heap = realloc(heap, sizeof(node_t) * cap); }
                    heap_push(heap, &hn, v);
                }
            }
        }
    }
    int n = 0;
    if (found >= 0) {
        for (int i = found; i >= 0 && n < max_out; i = par[i]) out[n++] = i;
    }
    free(g); free(par); free(closed); free(heap);
    return n;
}
