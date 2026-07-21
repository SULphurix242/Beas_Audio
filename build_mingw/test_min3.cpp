
#include "beas/beas_audio.h"
int main() {
    BaConfig cfg;
    cfg.sample_rate = 48000;
    cfg.buffer_size = 128;
    cfg.path = BA_PATH_EXPLICIT_OBJECTS;
    cfg.max_objects = 16;
    cfg.tier = 2;
    cfg.alloc = 0;
    cfg.free = 0;
    cfg.hrtf_path = 0;
    cfg.ear_fir_path = 0;
    cfg.eq_band_count = 0;
    BaAudioContext* ctx = 0;
    ba_create(&cfg, &ctx);
    if (ctx) ba_destroy(ctx);
    return 0;
}

