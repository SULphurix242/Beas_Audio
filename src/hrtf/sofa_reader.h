// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — self-contained HDF5/SOFA file reader
//
// Parses AES69 SOFA files (HDF5-based) with no external dependencies.
// Supports:
//   - HDF5 superblock versions 0, 1 (via root symbol table) and 2, 3 (via direct root address)
//   - Object header versions 1 and 2 (v2 identified by 'OHDR' signature)
//   - Contiguous storage only (no chunking/compression — standard for SOFA)
//   - IEEE_F32LE and IEEE_F64LE data types
//   - SIMPLE dataspace (1D, 2D, 3D)
//   - Group navigation via symbol table (v1) or link messages (v2)
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SOFA data extracted from file ------------------------------------ */

typedef struct {
    uint32_t  num_positions;     // M
    uint32_t  ir_length;         // N samples per IR
    uint32_t  sample_rate;       // Hz

    // Heap-allocated arrays, freed by sofa_data_free().
    // positions[i*3+0] = azimuth   (radians)
    // positions[i*3+1] = elevation (radians)
    // positions[i*3+2] = radius    (metres)
    float*    positions;         // [num_positions * 3]

    float*    ir_left;           // [num_positions * ir_length]
    float*    ir_right;          // [num_positions * ir_length]

    char      conventions[64];   // SOFA convention string (e.g. "SimpleFreeFieldHRIR")
    char      room_type[32];     // Room type string
    int       data_type;         // 0 = FIR (time-domain), 1 = TF (frequency-domain)
} BaSofaData;

/* ---- API --------------------------------------------------------------- */

// Parse a SOFA (.sofa) file. Returns 0 on success, non-zero on error.
// On success, out contains heap-allocated data; caller must free with sofa_data_free().
int  sofa_parse(const char* path, BaSofaData* out);

// Free data returned by sofa_parse().
void sofa_data_free(BaSofaData* data);

#ifdef __cplusplus
}
#endif
