
#include "beas/beas_audio.h"
extern "C" void ba_hrtf_generate_math(BaHRTFDatabase*,uint32_t,int,void*,int);
int main() {
    BaHRTFDatabase db;
    ba_hrtf_generate_math(&db, 48000, 32, 0, 0);
    return 0;
}

