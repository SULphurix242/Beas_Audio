#include "../include/beas/beas_audio.h"
#include "../src/internal.h"
#include <cstdio>
int main() {
    printf("BaAudioContext: %zu\n", sizeof(BaAudioContext));
    printf("BaCluster: %zu\n", sizeof(BaCluster));
    printf("BaIRSet: %zu\n", sizeof(BaIRSet));
    printf("BaAmbientBus: %zu\n", sizeof(BaAmbientBus));
    printf("BaHRTFState: %zu\n", sizeof(BaHRTFState));
    printf("BaConvSlot: %zu\n", sizeof(BaConvSlot));
    printf("BaEQState: %zu\n", sizeof(BaEQState));
    printf("BaHRTFDatabase: %zu\n", sizeof(BaHRTFDatabase));
    printf("BaHRTFEntry: %zu\n", sizeof(BaHRTFEntry));
    printf("BaPerceptualSceneState: %zu\n", sizeof(BaPerceptualSceneState));
    return 0;
}
