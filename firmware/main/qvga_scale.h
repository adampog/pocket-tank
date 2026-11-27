/* qvga_scale.h - the rectangle's 448 x 368 frame on a 320 x 240 glass (this fork's
 * 2.8" CYD and Waveshare Touch-LCD-2), and a touch on that glass back in the frame.
 *
 * Since the 0.4.3 merge these boards run the rectangle's own world - nothing in
 * common/ is laid out for 320 x 240 - and the display port (display_port_spi.c)
 * shows it scaled down: 7 frame px across for 5 on the glass, 23 down for 15, so
 * the whole frame fills the whole glass (the picture stands 9.5% wider than it is
 * tall against the 1.8's; nothing is cropped). The filter is the exact AREA
 * AVERAGE: every glass px is the mean of the frame px under it, weighted by how
 * much of each it covers, the three colours apart. Text is the reason: a 1 px
 * stroke of the 5x7 font never falls between two samples and vanishes, as it
 * would with a nearest-px pick - it thins to a grey that still reads.
 *
 * One file for the firmware and the simulator's preview (sim/qvga_shot.c), so the
 * pictures the sim writes are the ones the board shows. No ESP-IDF in here. */
#ifndef QVGA_SCALE_H
#define QVGA_SCALE_H
#include <stdint.h>
#include <stdbool.h>

#define QVGA_SRC_W 448
#define QVGA_SRC_H 368
#define QVGA_W     320
#define QVGA_H     240
/* 448 / 320 = 7 / 5 and 368 / 240 = 23 / 15: a glass px is 7 fifths of a frame px wide, 23 fifteenths tall */
#define QVGA_XN 7
#define QVGA_XD 5
#define QVGA_YN 23
#define QVGA_YD 15

typedef struct {
    bool     ready;
    uint16_t x0[QVGA_W]; uint8_t xw[QVGA_W][3];     /* per glass column: the first frame column under it, the cover of it and the next two (of 7) */
    uint16_t y0[QVGA_H]; uint8_t yw[QVGA_H][3];     /* per glass row: the same down the frame (of 23) */
    int      row_of[3];                             /* which frame row each kept line holds (-1: none) */
    uint16_t line[3][3][QVGA_W];                    /* three frame rows already averaged ACROSS: [slot][r, g, b][glass x], each 0..31*7 (63*7) */
} qvga_scale_t;

static inline void qvga_scale_init(qvga_scale_t *s) {
    for (int x = 0; x < QVGA_W; x++) {
        int a = x * QVGA_XN, b = a + QVGA_XN, i0 = a / QVGA_XD;        /* the glass px covers [a, b) in fifths of a frame px */
        s->x0[x] = (uint16_t)i0;
        for (int k = 0; k < 3; k++) {
            int lo = (i0 + k) * QVGA_XD, hi = lo + QVGA_XD;
            int w = (hi < b ? hi : b) - (lo > a ? lo : a);
            s->xw[x][k] = (uint8_t)(w > 0 && i0 + k < QVGA_SRC_W ? w : 0);
        }
    }
    for (int y = 0; y < QVGA_H; y++) {
        int a = y * QVGA_YN, b = a + QVGA_YN, i0 = a / QVGA_YD;
        s->y0[y] = (uint16_t)i0;
        for (int k = 0; k < 3; k++) {
            int lo = (i0 + k) * QVGA_YD, hi = lo + QVGA_YD;
            int w = (hi < b ? hi : b) - (lo > a ? lo : a);
            s->yw[y][k] = (uint8_t)(w > 0 && i0 + k < QVGA_SRC_H ? w : 0);
        }
    }
    s->row_of[0] = s->row_of[1] = s->row_of[2] = -1;
    s->ready = true;
}
/* a new frame: nothing kept from the last one */
static inline void qvga_scale_begin(qvga_scale_t *s) { if (!s->ready) qvga_scale_init(s); s->row_of[0] = s->row_of[1] = s->row_of[2] = -1; }

static inline int qvga_line(qvga_scale_t *s, const uint16_t *src, bool src_swapped, int sy) {
    int slot = sy % 3;
    if (s->row_of[slot] == sy) return slot;
    const uint16_t *p = src + sy * QVGA_SRC_W;
    uint16_t *R = s->line[slot][0], *G = s->line[slot][1], *B = s->line[slot][2];
    for (int x = 0; x < QVGA_W; x++) {
        const uint16_t *q = p + s->x0[x]; const uint8_t *w = s->xw[x];
        unsigned r = 0, g = 0, b = 0;
        for (int k = 0; k < 3; k++) {
            if (!w[k]) continue;
            unsigned v = q[k]; if (src_swapped) v = ((v << 8) | (v >> 8)) & 0xFFFFu;
            r += (v >> 11) * w[k]; g += ((v >> 5) & 63u) * w[k]; b += (v & 31u) * w[k];
        }
        R[x] = (uint16_t)r; G[x] = (uint16_t)g; B[x] = (uint16_t)b;
    }
    s->row_of[slot] = sy;
    return slot;
}
/* glass rows y0 .. y0 + rows - 1 of the frame src (QVGA_SRC_W x QVGA_SRC_H, stride QVGA_SRC_W; src_swapped: held high byte
 * first, render.h PT_FB_SWAP) into dst (QVGA_W px a row; dst_swapped: written high byte first, the panel's wire order).
 * Rows are asked for in rising order within a frame (qvga_scale_begin first). */
static inline void qvga_scale_rows(qvga_scale_t *s, const uint16_t *src, bool src_swapped, int y0, int rows, uint16_t *dst, bool dst_swapped) {
    const unsigned HALF = (QVGA_XN * QVGA_YN) / 2;                      /* the weights of a glass px add up to 7 * 23 = 161 */
    for (int y = y0; y < y0 + rows; y++) {
        const uint8_t *w = s->yw[y];
        int sl[3] = { 0, 0, 0 }; unsigned wy[3] = { w[0], w[1], w[2] };
        for (int k = 0; k < 3; k++) if (wy[k]) sl[k] = qvga_line(s, src, src_swapped, s->y0[y] + k);
        const uint16_t *R0 = s->line[sl[0]][0], *G0 = s->line[sl[0]][1], *B0 = s->line[sl[0]][2];
        const uint16_t *R1 = s->line[sl[1]][0], *G1 = s->line[sl[1]][1], *B1 = s->line[sl[1]][2];
        const uint16_t *R2 = s->line[sl[2]][0], *G2 = s->line[sl[2]][1], *B2 = s->line[sl[2]][2];
        uint16_t *o = dst + (y - y0) * QVGA_W;
        for (int x = 0; x < QVGA_W; x++) {
            unsigned r = R0[x] * wy[0] + R1[x] * wy[1] + R2[x] * wy[2];
            unsigned g = G0[x] * wy[0] + G1[x] * wy[1] + G2[x] * wy[2];
            unsigned b = B0[x] * wy[0] + B1[x] * wy[1] + B2[x] * wy[2];
            r = ((r + HALF) * 407u) >> 16; g = ((g + HALF) * 407u) >> 16; b = ((b + HALF) * 407u) >> 16;   /* / 161, rounded (407 / 65536) */
            if (r > 31) r = 31;
            if (g > 63) g = 63;
            if (b > 31) b = 31;
            unsigned v = (r << 11) | (g << 5) | b;
            o[x] = (uint16_t)(dst_swapped ? ((v << 8) | (v >> 8)) : v);
        }
    }
}
/* a point on the glass (upright, 0..319 x 0..239) -> the frame px under its middle */
static inline void qvga_glass_to_frame(int gx, int gy, float *fx, float *fy) {
    *fx = ((float)gx + 0.5f) * QVGA_XN / QVGA_XD - 0.5f;
    *fy = ((float)gy + 0.5f) * QVGA_YN / QVGA_YD - 0.5f;
    if (*fx < 0) *fx = 0;
    if (*fy < 0) *fy = 0;
    if (*fx > QVGA_SRC_W - 1) *fx = QVGA_SRC_W - 1;
    if (*fy > QVGA_SRC_H - 1) *fy = QVGA_SRC_H - 1;
}
#endif
