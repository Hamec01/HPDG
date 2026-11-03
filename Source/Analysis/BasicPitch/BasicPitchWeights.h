#pragma once

// Spotify Basic Pitch's ICASSP 2022 model (Apache-2.0, docs/THIRD-PARTY-NOTICES.txt), exported by
// tools/basic_pitch_export.py into BasicPitchWeights.cpp. Conv kernels keep keras' layout
// (kernel time, kernel frequency, in channels, out channels).
namespace bbg::basic_pitch
{
// nnAudio CQT2010v2 kernels of the top octave (36 bins x 256 taps), the per-bin kernel lengths of
// all 309 bins and the 256-tap half-band low-pass filter between octaves
extern const float kCqtKernelReal[9216];
extern const float kCqtKernelImag[9216];
extern const float kCqtLengths[309];
extern const float kLowpass[256];

extern const float k_batch_normalization_gamma[1];
extern const float k_batch_normalization_beta[1];
extern const float k_batch_normalization_moving_mean[1];
extern const float k_batch_normalization_moving_variance[1];

extern const float k_conv2d_1_kernel[7488]; // 3 x 39 x 8 x 8: contour features
extern const float k_conv2d_1_bias[8];
extern const float k_batch_normalization_2_gamma[8];
extern const float k_batch_normalization_2_beta[8];
extern const float k_batch_normalization_2_moving_mean[8];
extern const float k_batch_normalization_2_moving_variance[8];
extern const float k_contours_reduced_kernel[200]; // 5 x 5 x 8 x 1: contour (sigmoid)
extern const float k_contours_reduced_bias[1];

extern const float k_conv2d_2_kernel[1568]; // 7 x 7 x 1 x 32, frequency stride 3: note features
extern const float k_conv2d_2_bias[32];
extern const float k_conv2d_3_kernel[672]; // 7 x 3 x 32 x 1: note (sigmoid)
extern const float k_conv2d_3_bias[1];

extern const float k_conv2d_4_kernel[6400]; // 5 x 5 x 8 x 32, frequency stride 3: onset features
extern const float k_conv2d_4_bias[32];
extern const float k_batch_normalization_3_gamma[32];
extern const float k_batch_normalization_3_beta[32];
extern const float k_batch_normalization_3_moving_mean[32];
extern const float k_batch_normalization_3_moving_variance[32];
extern const float k_conv2d_5_kernel[297]; // 3 x 3 x 33 x 1: onset (sigmoid) over note + onset features
extern const float k_conv2d_5_bias[1];
} // namespace bbg::basic_pitch
