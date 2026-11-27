/* qvga_shot.c - (this fork) what the 320 x 240 boards show: a 448 x 368 picture the simulator wrote (any --*-shot's
 * PPM) through the SAME filter the board's display port runs (firmware/main/qvga_scale.h), as a 320 x 240 PPM.
 *
 *   cc -O2 -o qvga_shot qvga_shot.c            (make qvga_shot)
 *   ./qvga_shot in.ppm out.ppm [in2.ppm out2.ppm ...]
 *   ./qvga_shot --selftest                      the scaler's own checks (make check-qvga)
 *
 * A standalone program on purpose: nothing in sim/main.c or common/ knows these boards, so an upstream release
 * never conflicts here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/main/qvga_scale.h"

static uint16_t src[QVGA_SRC_W * QVGA_SRC_H], dst[QVGA_W * QVGA_H];
static qvga_scale_t sc;

static void scale(bool swapped_in, bool swapped_out, int stripe) {
    qvga_scale_begin(&sc);
    for (int y = 0; y < QVGA_H; y += stripe) qvga_scale_rows(&sc, src, swapped_in, y, QVGA_H - y < stripe ? QVGA_H - y : stripe, dst + y * QVGA_W, swapped_out);
}
static int convert(const char *in, const char *out) {
    FILE *f = fopen(in, "rb"); if (!f) { fprintf(stderr, "qvga_shot: cannot read %s\n", in); return 1; }
    int w = 0, h = 0, mx = 0;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3 || w != QVGA_SRC_W || h != QVGA_SRC_H || mx != 255) {
        fprintf(stderr, "qvga_shot: %s is not a %d x %d P6 PPM (%d x %d)\n", in, QVGA_SRC_W, QVGA_SRC_H, w, h); fclose(f); return 1; }
    fgetc(f);
    for (int i = 0; i < w * h; i++) { uint8_t p[3]; if (fread(p, 1, 3, f) != 3) { fclose(f); return 1; } src[i] = (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3); }
    fclose(f);
    scale(false, false, 20);
    f = fopen(out, "wb"); if (!f) { fprintf(stderr, "qvga_shot: cannot write %s\n", out); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", QVGA_W, QVGA_H);
    for (int i = 0; i < QVGA_W * QVGA_H; i++) { uint16_t v = dst[i]; uint8_t p[3] = { (uint8_t)((v >> 11) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) }; fwrite(p, 1, 3, f); }
    fclose(f);
    return 0;
}
#define FAIL(...) do { printf("FAIL: qvga: " __VA_ARGS__); printf("\n"); bad++; } while (0)
static int selftest(void) {
    int bad = 0;
    qvga_scale_init(&sc);
    /* 1. every glass px's weights cover exactly 7 fifths across and 23 fifteenths down, inside the frame */
    for (int x = 0; x < QVGA_W; x++) { int s = sc.xw[x][0] + sc.xw[x][1] + sc.xw[x][2]; int last = sc.x0[x] + (sc.xw[x][2] ? 2 : sc.xw[x][1] ? 1 : 0);
        if (s != QVGA_XN || last >= QVGA_SRC_W) { FAIL("column %d: weights %d (want %d), last frame column %d", x, s, QVGA_XN, last); break; } }
    for (int y = 0; y < QVGA_H; y++) { int s = sc.yw[y][0] + sc.yw[y][1] + sc.yw[y][2]; int last = sc.y0[y] + (sc.yw[y][2] ? 2 : sc.yw[y][1] ? 1 : 0);
        if (s != QVGA_YN || last >= QVGA_SRC_H) { FAIL("row %d: weights %d (want %d), last frame row %d", y, s, QVGA_YN, last); break; } }
    /* 2. every frame px is used, and to the same total (5 x 15): nothing cropped, nothing counted twice */
    { static int cx[QVGA_SRC_W], cy[QVGA_SRC_H];
      for (int x = 0; x < QVGA_W; x++) for (int k = 0; k < 3; k++) if (sc.xw[x][k]) cx[sc.x0[x] + k] += sc.xw[x][k];
      for (int y = 0; y < QVGA_H; y++) for (int k = 0; k < 3; k++) if (sc.yw[y][k]) cy[sc.y0[y] + k] += sc.yw[y][k];
      for (int x = 0; x < QVGA_SRC_W; x++) if (cx[x] != QVGA_XD) { FAIL("frame column %d is covered %d fifths (want %d)", x, cx[x], QVGA_XD); break; }
      for (int y = 0; y < QVGA_SRC_H; y++) if (cy[y] != QVGA_YD) { FAIL("frame row %d is covered %d fifteenths (want %d)", y, cy[y], QVGA_YD); break; } }
    /* 3. a flat colour stays that colour, every colour channel's extremes included, in both byte orders, at any stripe height */
    static const uint16_t FLAT[] = { 0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x1234, 0x8410, 0x0841 };
    for (unsigned c = 0; c < sizeof FLAT / sizeof *FLAT; c++) for (int sw = 0; sw < 4; sw++) for (int stripe = 1; stripe <= 20; stripe += 19) {
        uint16_t v = FLAT[c], in = (sw & 1) ? (uint16_t)(v << 8 | v >> 8) : v, want = (sw & 2) ? (uint16_t)(v << 8 | v >> 8) : v;
        for (int i = 0; i < QVGA_SRC_W * QVGA_SRC_H; i++) src[i] = in;
        scale(sw & 1, sw & 2, stripe);
        for (int i = 0; i < QVGA_W * QVGA_H; i++) if (dst[i] != want) { FAIL("flat %04x (in %s, out %s, stripes of %d): glass px %d is %04x", v, (sw & 1) ? "swapped" : "native", (sw & 2) ? "swapped" : "native", stripe, i, dst[i]); goto next; }
        next: ;
    }
    /* 4. a 1 px white line on black is never lost: every row and every column of the frame leaves light on the glass */
    for (int y = 0; y < QVGA_SRC_H; y++) { memset(src, 0, sizeof src); for (int x = 0; x < QVGA_SRC_W; x++) src[y * QVGA_SRC_W + x] = 0xFFFF;
        scale(false, false, 20); int lit = 0; for (int i = 0; i < QVGA_W * QVGA_H; i++) if (((dst[i] >> 5) & 63) >= 20) lit++;
        if (lit < QVGA_W) { FAIL("a white line on frame row %d leaves %d lit glass px (want a whole row, %d)", y, lit, QVGA_W); break; } }
    for (int x = 0; x < QVGA_SRC_W; x++) { memset(src, 0, sizeof src); for (int y = 0; y < QVGA_SRC_H; y++) src[y * QVGA_SRC_W + x] = 0xFFFF;
        scale(false, false, 20); int lit = 0; for (int i = 0; i < QVGA_W * QVGA_H; i++) if (((dst[i] >> 5) & 63) >= 20) lit++;
        if (lit < QVGA_H) { FAIL("a white line on frame column %d leaves %d lit glass px (want a whole column, %d)", x, lit, QVGA_H); break; } }
    /* 5. the picture's total light is kept (a random frame's mean green, to a rounding) */
    { unsigned seed = 12345; double sin_ = 0, sout = 0;
      for (int i = 0; i < QVGA_SRC_W * QVGA_SRC_H; i++) { seed = seed * 1103515245u + 12345u; src[i] = (uint16_t)(seed >> 16); sin_ += (src[i] >> 5) & 63; }
      scale(false, false, 20); for (int i = 0; i < QVGA_W * QVGA_H; i++) sout += (dst[i] >> 5) & 63;
      double a = sin_ / (QVGA_SRC_W * QVGA_SRC_H), b = sout / (QVGA_W * QVGA_H);
      if (a - b > 0.5 || b - a > 0.5) FAIL("mean green %.3f in, %.3f out", a, b); }
    /* 6. the touch map: the glass's corners reach the frame's corners (within the 1.4 px a glass px covers), every glass px's
          point lies inside the frame px it averages, and the map never runs backwards */
    { float fx, fy, px = -1, py = -1;
      qvga_glass_to_frame(0, 0, &fx, &fy); if (fx > 1 || fy > 1) FAIL("glass 0,0 -> frame %.2f,%.2f", fx, fy);
      qvga_glass_to_frame(QVGA_W - 1, QVGA_H - 1, &fx, &fy); if (fx < QVGA_SRC_W - 2 || fy < QVGA_SRC_H - 2 || fx > QVGA_SRC_W - 1 || fy > QVGA_SRC_H - 1) FAIL("glass 319,239 -> frame %.2f,%.2f", fx, fy);
      for (int x = 0; x < QVGA_W; x++) { qvga_glass_to_frame(x, 0, &fx, &fy); int last = sc.x0[x] + 2;
          if (fx <= px || fx < sc.x0[x] - 0.01f || fx > last) { FAIL("glass column %d -> frame x %.2f (its px %d..%d, the one before %.2f)", x, fx, sc.x0[x], last, px); break; } px = fx; }
      for (int y = 0; y < QVGA_H; y++) { qvga_glass_to_frame(0, y, &fx, &fy); int last = sc.y0[y] + 2;
          if (fy <= py || fy < sc.y0[y] - 0.01f || fy > last) { FAIL("glass row %d -> frame y %.2f (its px %d..%d, the one before %.2f)", y, fy, sc.y0[y], last, py); break; } py = fy; } }
    printf("selftest-qvga: %s (the weights, full cover, flat colours in both byte orders, 1 px lines kept, the light kept, the touch map)\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc < 3 || (argc - 1) % 2) { fprintf(stderr, "usage: qvga_shot in.ppm out.ppm [...] | --selftest\n"); return 2; }
    for (int i = 1; i + 1 < argc; i += 2) if (convert(argv[i], argv[i + 1])) return 1;
    return 0;
}
