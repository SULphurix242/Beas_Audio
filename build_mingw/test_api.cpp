#include "beas/beas_audio.h"
int main() {
    BaConfig cfg = {48000,128,BA_PATH_EXPLICIT_OBJECTS,16,2,0,0,0,0,0};
    BaAudioContext* ctx=0;
    ba_create(&cfg, &ctx);
    if(ctx)ba_destroy(ctx);
    return 0;
}
