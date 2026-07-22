#!/usr/bin/env python3
"""Generate a minimal test SOFA file for HRTF loading tests."""
import h5py
import numpy as np
import sys
import os

def gen_test_sofa(path, num_positions=3, ir_length=64, sample_rate=48000):
    """Create a minimal SimpleFreeFieldHRIR SOFA file."""

    # Generate test IR data: simple decaying sinusoids per position
    # Left ear: impulse at sample 0, right ear: delayed impulse (ITD simulation)
    t = np.arange(ir_length, dtype=np.float64)
    ir = np.zeros((num_positions, ir_length, 2), dtype=np.float64)

    for p in range(num_positions):
        az = p * 30.0  # 0, 30, 60 degrees
        delay = p * 3   # increasing ITD
        for n in range(ir_length):
            env = np.exp(-n / (ir_length * 0.15))
            # Left ear
            if n < ir_length:
                ir[p, n, 0] = env * (1.0 + 0.5 * np.cos(0.02 * n))
            # Right ear (delayed, attenuated)
            dn = n - delay
            if dn >= 0 and dn < ir_length:
                ir[p, dn, 1] = env * (1.0 - 0.3 * np.cos(0.02 * n))

    # Normalise so max absolute value is 1.0
    scale = np.max(np.abs(ir))
    if scale > 0:
        ir /= scale

    # Source positions in spherical degrees (az, el, radius)
    positions = np.zeros((num_positions, 3), dtype=np.float64)
    for p in range(num_positions):
        positions[p, 0] = p * 30.0    # azimuth: 0, 30, 60
        positions[p, 1] = 0.0         # elevation: 0
        positions[p, 2] = 1.0         # radius: 1m

    # Create HDF5 file with SOFA conventions — force v2 superblock
    with h5py.File(path, 'w', libver='latest') as f:
        # Root attributes
        f.attrs['SOFAConventions'] = 'SimpleFreeFieldHRIR'
        f.attrs['SOFAConventionsVersion'] = '2.0'
        f.attrs['DataType'] = 'FIR'
        f.attrs['RoomType'] = 'free field'
        f.attrs['DatabaseName'] = 'Beas Test HRTF'

        # Listener position
        f.create_dataset('ListenerPosition', data=np.zeros((1, 3), dtype=np.float64))
        f['ListenerPosition'].attrs['Type'] = 'cartesian'
        f['ListenerPosition'].attrs['Units'] = 'metre'

        # Source positions
        f.create_dataset('SourcePosition', data=positions)
        f['SourcePosition'].attrs['Type'] = 'spherical'
        f['SourcePosition'].attrs['Units'] = 'degree, degree, metre'

        # Data group
        f.create_group('Data')

        # IR data
        f.create_dataset('Data/IR', data=ir)
        f['Data/IR'].attrs['DataChannel'] = 'L,R'
        f['Data/IR'].attrs['DataType'] = 'FIR'
        f['Data/IR'].attrs['SamplingRate'] = sample_rate

        # Sampling rate
        f.create_dataset('Data/SamplingRate', data=np.float64(sample_rate))

    return path


if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else 'test_hrtf.sofa'
    num = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    ir_len = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    sr = int(sys.argv[4]) if len(sys.argv) > 4 else 48000
    gen_test_sofa(path, num, ir_len, sr)
    print(f'Generated SOFA test file: {path}')
    print(f'  Positions: {num}, IR length: {ir_len}, SR: {sr}')
    print(f'  File size: {os.path.getsize(path)} bytes')
