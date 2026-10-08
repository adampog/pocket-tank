/* model_trailer.c - see model_trailer.h */
#include "model_trailer.h"
#include "version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "model";

static uint32_t crc_of(const model_trailer_t *t) {          /* FNV-1a over everything but the crc field */
    const uint8_t *b = (const uint8_t *)t; uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(model_trailer_t, crc); i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static size_t trailer_off(const esp_partition_t *mp) { return mp->size - MODEL_TRAILER_SIZE; }

bool model_trailer_read(const esp_partition_t *mp, model_trailer_t *out) {
    if (!mp || mp->size < 2 * MODEL_TRAILER_SIZE) return false;
    model_trailer_t t;
    if (esp_partition_read(mp, trailer_off(mp), &t, sizeof t) != ESP_OK) return false;
    if (t.magic != MODEL_TRAILER_MAGIC || t.crc != crc_of(&t) || t.model_len == 0 || t.model_len > trailer_off(mp)) return false;
    if (out) *out = t;
    return true;
}
bool model_trailer_write(const esp_partition_t *mp, model_trailer_t *t) {
    if (!mp) return false;
    t->magic = MODEL_TRAILER_MAGIC; t->version = 1; t->crc = crc_of(t);
    return esp_partition_erase_range(mp, trailer_off(mp), MODEL_TRAILER_SIZE) == ESP_OK
        && esp_partition_write(mp, trailer_off(mp), t, sizeof *t) == ESP_OK;
}
bool model_trailer_erase(const esp_partition_t *mp) {
    return mp && esp_partition_erase_range(mp, trailer_off(mp), MODEL_TRAILER_SIZE) == ESP_OK;
}
void model_sha256_hex(const void *p, size_t len, char out[65]) {
    uint8_t d[32];
    mbedtls_sha256_context c; mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    /* 64 KB steps: the flash cache walks the mapping in order, and the
       watchdog sees the gaps (the tank task is not running yet at boot) */
    for (size_t o = 0; o < len; o += 65536) mbedtls_sha256_update(&c, (const uint8_t *)p + o, len - o < 65536 ? len - o : 65536);
    mbedtls_sha256_finish(&c, d); mbedtls_sha256_free(&c);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
}
static bool hex_to_bytes(const char *hex, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) { unsigned v; if (sscanf(hex + i * 2, "%2x", &v) != 1) return false; out[i] = (uint8_t)v; }
    return true;
}

bool model_trailer_check(const esp_partition_t *mp, const void *map) {
    model_trailer_t t;
    if (model_trailer_read(mp, &t)) {
        ESP_LOGI(TAG, "trailer: model %s, %lu bytes - good", t.tag, (unsigned long)t.model_len);
        return true;
    }
    if (!map) return false;
    /* no trailer: is the body the model this build shipped with? */
    int64_t t0 = esp_timer_get_time();
    char hex[65]; model_sha256_hex(map, PT_MODEL_LEN, hex);
    ESP_LOGI(TAG, "no trailer: hashed the body (%lu bytes) in %lld ms", (unsigned long)PT_MODEL_LEN, (long long)((esp_timer_get_time() - t0) / 1000));
    if (strcmp(hex, PT_MODEL_SHA256) == 0) {
        memset(&t, 0, sizeof t);
        t.model_len = PT_MODEL_LEN; hex_to_bytes(PT_MODEL_SHA256, t.sha256, 32);
        snprintf(t.tag, sizeof t.tag, "%s", PT_MODEL_TAG);
        bool ok = model_trailer_write(mp, &t);
        ESP_LOGI(TAG, "the body is %s: trailer %s", PT_MODEL_TAG, ok ? "written" : "NOT written");
        return ok;
    }
    ESP_LOGW(TAG, "the body is not %s (sha256 %.16s...): running it as it is, no trailer", PT_MODEL_TAG, hex);
    return false;
}
