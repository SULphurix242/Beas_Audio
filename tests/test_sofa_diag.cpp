// Quick SOFA reader diagnostic — prints failure point
#include <cstdio>
#include "hrtf/sofa_reader.h"
int main() {
    BaSofaData sofa;
    int e = sofa_parse("D:/BEAS/Beas_Audio/tests/test_hrtf.sofa", &sofa);
    std::printf("sofa_parse returned: %d\n", e);
    if (e == 0) {
        std::printf("  positions=%u, ir_length=%u, sr=%u\n",
            sofa.num_positions, sofa.ir_length, sofa.sample_rate);
        std::printf("  positions[0] = %.2f, %.2f, %.2f\n",
            sofa.positions[0], sofa.positions[1], sofa.positions[2]);
        std::printf("  ir_left[0..3] = %.4f, %.4f, %.4f, %.4f\n",
            sofa.ir_left[0], sofa.ir_left[1], sofa.ir_left[2], sofa.ir_left[3]);
        std::printf("  ir_right[0..3] = %.4f, %.4f, %.4f, %.4f\n",
            sofa.ir_right[0], sofa.ir_right[1], sofa.ir_right[2], sofa.ir_right[3]);
        sofa_data_free(&sofa);
    }
    return 0;
}
