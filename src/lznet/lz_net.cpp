/*
    This file is part of Leela Zero.
    Copyright (C) 2017 Gian-Carlo Pascutto

    Leela Zero is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Leela Zero is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "config.h"
#include "lz_net.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <boost/utility.hpp>
#include <boost/format.hpp>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif
#ifdef USE_MKL
#include <mkl.h>
#endif
#ifdef USE_OPENBLAS
#include <cblas.h>
#endif
#ifdef USE_OPENCL
#include "OpenCLScheduler.h"
#endif

#include "NNCache.h"
#include "Im2Col.h"
#include "Random.h"
#include "Utils.h"

using namespace Utils;

// Input + residual block tower
static std::vector<std::vector<float>> conv_weights;
static std::vector<std::vector<float>> conv_biases;
static std::vector<std::vector<float>> batchnorm_means;
static std::vector<std::vector<float>> batchnorm_stddivs;

// Policy head
static std::vector<float> conv_pol_w;
static std::vector<float> conv_pol_b;
static std::array<float, 2> bn_pol_w1;
static std::array<float, 2> bn_pol_w2;

static std::array<float, 261364> ip_pol_w;
static std::array<float, 362> ip_pol_b;

// Value head
static std::vector<float> conv_val_w;
static std::vector<float> conv_val_b;
static std::array<float, 1> bn_val_w1;
static std::array<float, 1> bn_val_w2;

static std::array<float, 92416> ip1_val_w;
static std::array<float, 256> ip1_val_b;

static std::array<float, 256> ip2_val_w;
static std::array<float, 1> ip2_val_b;

// Rotation helper
static std::array<std::array<int, 361>, 8> rotate_nn_idx_table;


void Network::process_bn_var(std::vector<float>& weights, const float epsilon) {
    for (auto&& w : weights) {
        w = 1.0f / std::sqrt(w + epsilon);
    }
}

std::vector<float> Network::winograd_transform_f(const std::vector<float>& f,
                                                 const int outputs,
                                                 const int channels) {
    // F(2x2, 3x3) Winograd filter transformation
    // transpose(G.dot(f).dot(G.transpose()))
    // U matrix is transposed for better memory layout in SGEMM
    auto U = std::vector<float>(WINOGRAD_TILE * outputs * channels);
    auto G = std::array<float, WINOGRAD_TILE>{1.0,  0.0,  0.0,
                                              0.5,  0.5,  0.5,
                                              0.5, -0.5,  0.5,
                                              0.0,  0.0,  1.0};
    auto temp = std::array<float, 12>{};

    for (auto o = 0; o < outputs; o++) {
        for (auto c = 0; c < channels; c++) {
            for (auto i = 0; i < 4; i++) {
                for (auto j = 0; j < 3; j++) {
                    auto acc = 0.0f;
                    for (auto k = 0; k < 3; k++) {
                        acc += G[i*3 + k] * f[o*channels*9 + c*9 + k*3 + j];
                    }
                    temp[i*3 + j] = acc;
                }
            }

            for (auto xi = 0; xi < 4; xi++) {
                for (auto nu = 0; nu < 4; nu++) {
                    auto acc = 0.0f;
                    for (int k = 0; k < 3; k++) {
                        acc += temp[xi*3 + k] * G[nu*3 + k];
                    }
                    U[xi * (4 * outputs * channels)
                      + nu * (outputs * channels)
                      + c * outputs
                      + o] = acc;
                }
            }
        }
    }

    return U;
}

std::vector<float> Network::zeropad_U(const std::vector<float>& U,
                                      const int outputs, const int channels,
                                      const int outputs_pad,
                                      const int channels_pad) {
    // Fill with zeroes
    auto Upad = std::vector<float>(WINOGRAD_TILE * outputs_pad * channels_pad);

    for (auto o = 0; o < outputs; o++) {
        for (auto c = 0; c < channels; c++) {
            for (auto xi = 0; xi < WINOGRAD_ALPHA; xi++) {
                for (auto nu = 0; nu < WINOGRAD_ALPHA; nu++) {
                    Upad[xi * (WINOGRAD_ALPHA * outputs_pad * channels_pad)
                         + nu * (outputs_pad * channels_pad)
                         + c * outputs_pad +
                          o] =
                    U[xi * (WINOGRAD_ALPHA * outputs * channels)
                      + nu * (outputs * channels)
                      + c * outputs
                      + o];
                }
            }
        }
    }

    return Upad;
}

std::pair<int, int>  Network::load_v1_network(std::ifstream& wtfile) {
    // Count size of the network
    myprintf("Detecting residual layers...");
    // We are version 1
    myprintf("v%d...", 1);
    // First line was the version number
    auto linecount = size_t{1};
    auto channels = 0;
    auto line = std::string{};
    while (std::getline(wtfile, line)) {
        auto iss = std::stringstream{line};
        // Third line of parameters are the convolution layer biases,
        // so this tells us the amount of channels in the residual layers.
        // (Provided they're all equally large - that's not actually required!)
        if (linecount == 2) {
            auto count = std::distance(std::istream_iterator<std::string>(iss),
                                       std::istream_iterator<std::string>());
            myprintf("%d channels...", count);
            channels = count;
        }
        linecount++;
    }
    // 1 format id, 1 input layer (4 x weights), 14 ending weights,
    // the rest are residuals, every residual has 8 x weight lines
    auto residual_blocks = linecount - (1 + 4 + 14);
    if (residual_blocks % 8 != 0) {
        myprintf("\nInconsistent number of weights in the file.\n");
        return {0, 0};
    }
    residual_blocks /= 8;
    myprintf("%d blocks.\n", residual_blocks);

    // Re-read file and process
    wtfile.clear();
    wtfile.seekg(0, std::ios::beg);

    // Get the file format id out of the way
    std::getline(wtfile, line);

    auto plain_conv_layers = 1 + (residual_blocks * 2);
    auto plain_conv_wts = plain_conv_layers * 4;
    linecount = 0;
    while (std::getline(wtfile, line)) {
        std::vector<float> weights;
        float weight;
        std::istringstream iss(line);
        while (iss >> weight) {
            weights.emplace_back(weight);
        }
        if (linecount < plain_conv_wts) {
            if (linecount % 4 == 0) {
                conv_weights.emplace_back(weights);
            } else if (linecount % 4 == 1) {
                // Redundant in our model, but they encode the
                // number of outputs so we have to read them in.
                conv_biases.emplace_back(weights);
            } else if (linecount % 4 == 2) {
                batchnorm_means.emplace_back(weights);
            } else if (linecount % 4 == 3) {
                process_bn_var(weights);
                batchnorm_stddivs.emplace_back(weights);
            }
        } else if (linecount == plain_conv_wts) {
            conv_pol_w = std::move(weights);
        } else if (linecount == plain_conv_wts + 1) {
            conv_pol_b = std::move(weights);
        } else if (linecount == plain_conv_wts + 2) {
            std::copy(begin(weights), end(weights), begin(bn_pol_w1));
        } else if (linecount == plain_conv_wts + 3) {
            process_bn_var(weights);
            std::copy(begin(weights), end(weights), begin(bn_pol_w2));
        } else if (linecount == plain_conv_wts + 4) {
            std::copy(begin(weights), end(weights), begin(ip_pol_w));
        } else if (linecount == plain_conv_wts + 5) {
            std::copy(begin(weights), end(weights), begin(ip_pol_b));
        } else if (linecount == plain_conv_wts + 6) {
            conv_val_w = std::move(weights);
        } else if (linecount == plain_conv_wts + 7) {
            conv_val_b = std::move(weights);
        } else if (linecount == plain_conv_wts + 8) {
            std::copy(begin(weights), end(weights), begin(bn_val_w1));
        } else if (linecount == plain_conv_wts + 9) {
            process_bn_var(weights);
            std::copy(begin(weights), end(weights), begin(bn_val_w2));
        } else if (linecount == plain_conv_wts + 10) {
            std::copy(begin(weights), end(weights), begin(ip1_val_w));
        } else if (linecount == plain_conv_wts + 11) {
            std::copy(begin(weights), end(weights), begin(ip1_val_b));
        } else if (linecount == plain_conv_wts + 12) {
            std::copy(begin(weights), end(weights), begin(ip2_val_w));
        } else if (linecount == plain_conv_wts + 13) {
            std::copy(begin(weights), end(weights), begin(ip2_val_b));
        }
        linecount++;
    }
    wtfile.close();

    return {channels, residual_blocks};
}

std::pair<int, int> Network::load_network_file(std::string filename) {
    auto wtfile = std::ifstream{filename};
    if (wtfile.fail()) {
        myprintf("Could not open weights file: %s\n", filename.c_str());
        return {0, 0};
    }

    // Read format version
    auto line = std::string{};
    auto format_version = -1;
    if (std::getline(wtfile, line)) {
        auto iss = std::stringstream{line};
        // First line is the file format version id
        iss >> format_version;
        if (iss.fail() || format_version != FORMAT_VERSION) {
            myprintf("Weights file is the wrong version.\n");
            return {0, 0};
        } else {
            assert(format_version == FORMAT_VERSION);
            return load_v1_network(wtfile);
        }
    }

    return {0, 0};
}

void Network::initialize(void) {
    // Prepare rotation table
    for (auto s = 0; s < 8; s++) {
        for (auto v = 0; v < 19 * 19; v++) {
            rotate_nn_idx_table[s][v] = rotate_nn_idx(v, s);
        }
    }

    // Load network from file
    size_t channels, residual_blocks;
    std::tie(channels, residual_blocks) = load_network_file(cfg_weightsfile);
    if (channels == 0) {
        exit(EXIT_FAILURE);
    }

    auto weight_index = size_t{0};
    // Input convolution
    // Winograd transform convolution weights
    conv_weights[weight_index] =
        winograd_transform_f(conv_weights[weight_index],
                             channels, INPUT_CHANNELS);
    weight_index++;

    // Residual block convolutions
    for (auto i = size_t{0}; i < residual_blocks * 2; i++) {
        conv_weights[weight_index] =
            winograd_transform_f(conv_weights[weight_index],
                                 channels, channels);
        weight_index++;
    }

#ifdef USE_OPENCL
    myprintf("Initializing OpenCL.\n");
    opencl.initialize(channels);

    for (auto & opencl_net : opencl.get_networks()) {
        auto tuners = opencl_net->getOpenCL().get_sgemm_tuners();

        auto mwg = tuners[0];
        auto kwg = tuners[2];
        auto vwm = tuners[3];

        weight_index = 0;

        size_t m_ceil = lcm(lcm(channels, mwg), vwm);
        size_t k_ceil = lcm(lcm(INPUT_CHANNELS, kwg), vwm);

        auto Upad = zeropad_U(conv_weights[weight_index],
                              channels, INPUT_CHANNELS,
                              m_ceil, k_ceil);

        // Winograd filter transformation changes filter size to 4x4
        opencl_net->push_convolve(WINOGRAD_ALPHA, INPUT_CHANNELS, channels, Upad);
        opencl_net->push_batchnorm(361, batchnorm_means[weight_index],
                                    batchnorm_stddivs[weight_index]);
        weight_index++;

        // residual blocks
        for (auto i = size_t{0}; i < residual_blocks; i++) {
            auto Upad1 = zeropad_U(conv_weights[weight_index],
                                   channels, channels,
                                   m_ceil, m_ceil);
            auto Upad2 = zeropad_U(conv_weights[weight_index + 1],
                                   channels, channels,
                                   m_ceil, m_ceil);
            opencl_net->push_residual(WINOGRAD_ALPHA, channels, channels,
                                      Upad1,
                                      batchnorm_means[weight_index],
                                      batchnorm_stddivs[weight_index],
                                      Upad2,
                                      batchnorm_means[weight_index + 1],
                                      batchnorm_stddivs[weight_index + 1]);
            weight_index += 2;
        }
    }
#endif
#ifdef USE_BLAS
#ifndef __APPLE__
#ifdef USE_OPENBLAS
    openblas_set_num_threads(1);
    myprintf("BLAS Core: %s\n", openblas_get_corename());
#endif
#ifdef USE_MKL
    //mkl_set_threading_layer(MKL_THREADING_SEQUENTIAL);
    mkl_set_num_threads(1);
    MKLVersion Version;
    mkl_get_version(&Version);
    myprintf("BLAS core: MKL %s\n", Version.Processor);
#endif
#endif
#endif
}

#ifdef USE_BLAS
void Network::winograd_transform_in(const std::vector<float>& in,
                                    std::vector<float>& V,
                                    const int C) {
    constexpr auto W = 19;
    constexpr auto H = 19;
    constexpr auto wtiles = (W + 1) / 2;
    constexpr auto P = wtiles * wtiles;

    for (auto ch = 0; ch < C; ch++) {
        for (auto block_y = 0; block_y < wtiles; block_y++) {
            for (auto block_x = 0; block_x < wtiles; block_x++) {

                // Tiles overlap by 2
                const auto yin = 2 * block_y - 1;
                const auto xin = 2 * block_x - 1;

                // Cache input tile and handle zero padding
                using WinogradTile =
                    std::array<std::array<float, WINOGRAD_ALPHA>, WINOGRAD_ALPHA>;
                WinogradTile x;

                for (auto i = 0; i < WINOGRAD_ALPHA; i++) {
                    for (auto j = 0; j < WINOGRAD_ALPHA; j++) {
                        if ((yin + i) >= 0 && (xin + j) >= 0
                            && (yin + i) < H && (xin + j) < W) {
                            x[i][j] = in[ch*(W*H) + (yin+i)*W + (xin+j)];
                        } else {
                            x[i][j] = 0.0f;
                        }
                    }
                }

                const auto offset = ch*P + block_y*wtiles + block_x;

                // Calculates transpose(B).x.B
                // B = [[ 1.0,  0.0,  0.0,  0.0],
                //      [ 0.0,  1.0, -1.0,  1.0],
                //      [-1.0,  1.0,  1.0,  0.0],
                //      [ 0.0,  0.0,  0.0, -1.0]]

                WinogradTile T1, T2;

                T1[0][0] = x[0][0] - x[2][0];
                T1[0][1] = x[0][1] - x[2][1];
                T1[0][2] = x[0][2] - x[2][2];
                T1[0][3] = x[0][3] - x[2][3];
                T1[1][0] = x[1][0] + x[2][0];
                T1[1][1] = x[1][1] + x[2][1];
                T1[1][2] = x[1][2] + x[2][2];
                T1[1][3] = x[1][3] + x[2][3];
                T1[2][0] = x[2][0] - x[1][0];
                T1[2][1] = x[2][1] - x[1][1];
                T1[2][2] = x[2][2] - x[1][2];
                T1[2][3] = x[2][3] - x[1][3];
                T1[3][0] = x[1][0] - x[3][0];
                T1[3][1] = x[1][1] - x[3][1];
                T1[3][2] = x[1][2] - x[3][2];
                T1[3][3] = x[1][3] - x[3][3];

                T2[0][0] = T1[0][0] - T1[0][2];
                T2[0][1] = T1[0][1] + T1[0][2];
                T2[0][2] = T1[0][2] - T1[0][1];
                T2[0][3] = T1[0][1] - T1[0][3];
                T2[1][0] = T1[1][0] - T1[1][2];
                T2[1][1] = T1[1][1] + T1[1][2];
                T2[1][2] = T1[1][2] - T1[1][1];
                T2[1][3] = T1[1][1] - T1[1][3];
                T2[2][0] = T1[2][0] - T1[2][2];
                T2[2][1] = T1[2][1] + T1[2][2];
                T2[2][2] = T1[2][2] - T1[2][1];
                T2[2][3] = T1[2][1] - T1[2][3];
                T2[3][0] = T1[3][0] - T1[3][2];
                T2[3][1] = T1[3][1] + T1[3][2];
                T2[3][2] = T1[3][2] - T1[3][1];
                T2[3][3] = T1[3][1] - T1[3][3];

                for (auto i = 0; i < WINOGRAD_ALPHA; i++) {
                    for (auto j = 0; j < WINOGRAD_ALPHA; j++) {
                        V[(i*WINOGRAD_ALPHA + j)*C*P + offset] = T2[i][j];
                    }
                }
            }
        }
    }
}

void Network::winograd_sgemm(const std::vector<float>& U,
                             std::vector<float>& V,
                             std::vector<float>& M,
                             const int C, const int K) {
    constexpr auto P = (19 + 1) * (19 + 1) / WINOGRAD_ALPHA;

    for (auto b = 0; b < WINOGRAD_TILE; b++) {
        auto offset_u = b * K * C;
        auto offset_v = b * C * P;
        auto offset_m = b * K * P;

        cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                    K, P, C,
                    1.0f,
                    &U[offset_u], K,
                    &V[offset_v], P,
                    0.0f,
                    &M[offset_m], P);
    }
}

void Network::winograd_transform_out(const std::vector<float>& M,
                                     std::vector<float>& Y,
                                     const int K) {
    constexpr auto W = 19;
    constexpr auto H = 19;
    constexpr auto wtiles = (W + 1) / 2;
    constexpr auto P = wtiles * wtiles;

    for (auto k = 0; k < K; k++) {
        for (auto block_x = 0; block_x < wtiles; block_x++) {
            for (auto block_y = 0; block_y < wtiles; block_y++) {

                const auto x = 2 * block_x;
                const auto y = 2 * block_y;

                const auto b = block_y * wtiles + block_x;
                std::array<float, WINOGRAD_TILE> temp_m;
                for (auto xi = 0; xi < WINOGRAD_ALPHA; xi++) {
                    for (auto nu = 0; nu < WINOGRAD_ALPHA; nu++) {
                        temp_m[xi*WINOGRAD_ALPHA + nu] =
                            M[xi*(WINOGRAD_ALPHA*K*P) + nu*(K*P)+ k*P + b];
                    }
                }

                // Calculates transpose(A).temp_m.A
                //    A = [1.0,  0.0],
                //        [1.0,  1.0],
                //        [1.0, -1.0],
                //        [0.0, -1.0]]

                auto o11 =
                    temp_m[0*4 + 0] + temp_m[0*4 + 1] + temp_m[0*4 + 2] +
                    temp_m[1*4 + 0] + temp_m[1*4 + 1] + temp_m[1*4 + 2] +
                    temp_m[2*4 + 0] + temp_m[2*4 + 1] + temp_m[2*4 + 2];

                auto o12 =
                    temp_m[0*4 + 1] - temp_m[0*4 + 2] - temp_m[0*4 + 3] +
                    temp_m[1*4 + 1] - temp_m[1*4 + 2] - temp_m[1*4 + 3] +
                    temp_m[2*4 + 1] - temp_m[2*4 + 2] - temp_m[2*4 + 3];

                auto o21 =
                    temp_m[1*4 + 0] + temp_m[1*4 + 1] + temp_m[1*4 + 2] -
                    temp_m[2*4 + 0] - temp_m[2*4 + 1] - temp_m[2*4 + 2] -
                    temp_m[3*4 + 0] - temp_m[3*4 + 1] - temp_m[3*4 + 2];

                auto o22 =
                    temp_m[1*4 + 1] - temp_m[1*4 + 2] - temp_m[1*4 + 3] -
                    temp_m[2*4 + 1] + temp_m[2*4 + 2] + temp_m[2*4 + 3] -
                    temp_m[3*4 + 1] + temp_m[3*4 + 2] + temp_m[3*4 + 3];

                Y[k*(H*W) + (y)*W + (x)] = o11;
                if (x + 1 < W) {
                    Y[k*(H*W) + (y)*W + (x+1)] = o12;
                }
                if (y + 1 < H) {
                    Y[k*(H*W) + (y+1)*W + (x)] = o21;
                    if (x + 1 < W) {
                        Y[k*(H*W) + (y+1)*W + (x+1)] = o22;
                    }
                }
            }
        }
    }
}

void Network::winograd_convolve3(const int outputs,
                                 const std::vector<float>& input,
                                 const std::vector<float>& U,
                                 std::vector<float>& V,
                                 std::vector<float>& M,
                                 std::vector<float>& output) {

    constexpr unsigned int filter_len = WINOGRAD_ALPHA * WINOGRAD_ALPHA;
    const auto input_channels = U.size() / (outputs * filter_len);

    winograd_transform_in(input, V, input_channels);
    winograd_sgemm(U, V, M, input_channels, outputs);
    winograd_transform_out(M, output, outputs);
}

template<unsigned int filter_size>
void convolve(size_t outputs,
              const std::vector<net_t>& input,
              const std::vector<float>& weights,
              const std::vector<float>& biases,
              std::vector<float>& output) {
    // fixed for 19x19
    constexpr unsigned int width = 19;
    constexpr unsigned int height = 19;
    constexpr unsigned int board_squares = width * height;
    constexpr unsigned int filter_len = filter_size * filter_size;
    const auto input_channels = weights.size() / (biases.size() * filter_len);
    const auto filter_dim = filter_len * input_channels;
    assert(outputs * board_squares == output.size());

    std::vector<float> col(filter_dim * width * height);
    im2col<filter_size>(input_channels, input, col);

    // Weight shape (output, input, filter_size, filter_size)
    // 96 22 3 3
    // outputs[96,19x19] = weights[96,22x3x3] x col[22x3x3,19x19]
    // C←αAB + βC
    // M Number of rows in matrices A and C.
    // N Number of columns in matrices B and C.
    // K Number of columns in matrix A; number of rows in matrix B.
    // lda The size of the first dimention of matrix A; if you are
    // passing a matrix A[m][n], the value should be m.
    //    cblas_sgemm(CblasRowMajor, TransA, TransB, M, N, K, alpha, A, lda, B,
    //                ldb, beta, C, N);

    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                // M        N            K
                outputs, board_squares, filter_dim,
                1.0f, &weights[0], filter_dim,
                &col[0], board_squares,
                0.0f, &output[0], board_squares);

    for (unsigned int o = 0; o < outputs; o++) {
        for (unsigned int b = 0; b < board_squares; b++) {
            output[(o * board_squares) + b] =
                biases[o] + output[(o * board_squares) + b];
        }
    }
}

template<unsigned int inputs,
         unsigned int outputs,
         size_t W, size_t B>
void innerproduct(const std::vector<float>& input,
                  const std::array<float, W>& weights,
                  const std::array<float, B>& biases,
                  std::vector<float>& output) {
    assert(B == outputs);

    cblas_sgemv(CblasRowMajor, CblasNoTrans,
                // M     K
                outputs, inputs,
                1.0f, &weights[0], inputs,
                &input[0], 1,
                0.0f, &output[0], 1);

    auto lambda_ReLU = [](float val) { return (val > 0.0f) ?
                                       val : 0.0f; };

    for (unsigned int o = 0; o < outputs; o++) {
        float val = biases[o] + output[o];
        if (outputs == 256) {
            val = lambda_ReLU(val);
        }
        output[o] = val;
    }
}

template <size_t spatial_size>
void batchnorm(size_t channels,
               std::vector<float>& data,
               const float* means,
               const float* stddivs,
               const float* eltwise = nullptr) {
    
    auto lambda_ReLU = [](float val) { return (val > 0.0f) ?
                                       val : 0.0f; };

    for (auto c = size_t{0}; c < channels; ++c) {
        auto mean = means[c];
        auto scale_stddiv = stddivs[c];

        if (eltwise == nullptr) {
            // Classical BN
            auto arr = &data[c * spatial_size];
            for (auto b = size_t{0}; b < spatial_size; b++) {
                arr[b] = lambda_ReLU(scale_stddiv * (arr[b] - mean));
            }
        } else {
            // BN + residual add
            auto arr = &data[c * spatial_size];
            auto res = &eltwise[c * spatial_size];
            for (auto b = size_t{0}; b < spatial_size; b++) {
                arr[b] = lambda_ReLU(res[b] +
                                     (scale_stddiv * (arr[b] - mean)));
            }
        }
    }
}

void Network::forward_cpu(std::vector<float>& input,
                          std::vector<float>& output) {
    // Input convolution
    constexpr int width = 19;
    constexpr int height = 19;
    constexpr int tiles = (width + 1) * (height + 1) / 4;
    // Calculate output channels
    const auto output_channels = conv_biases[0].size();
    // Assumes that residual blocks are identical and have same
    // number of inputs and outputs
    const auto input_channels = output_channels;
    auto conv_out = std::vector<float>(output_channels * width * height);

    auto V = std::vector<float>(WINOGRAD_TILE * input_channels * tiles);
    auto M = std::vector<float>(WINOGRAD_TILE * output_channels * tiles);

    winograd_convolve3(output_channels, input, conv_weights[0], V, M, conv_out);
    batchnorm<361>(output_channels, conv_out,
                   batchnorm_means[0].data(),
                   batchnorm_stddivs[0].data());

    // Residual tower
    auto conv_in = std::vector<float>(output_channels * width * height);
    auto res = std::vector<float>(output_channels * width * height);
    for (auto i = size_t{1}; i < conv_weights.size(); i += 2) {
        auto output_channels = conv_biases[i].size();
        std::swap(conv_out, conv_in);
        std::copy(begin(conv_in), end(conv_in), begin(res));
        winograd_convolve3(output_channels, conv_in,
                           conv_weights[i], V, M, conv_out);
        batchnorm<361>(output_channels, conv_out,
                       batchnorm_means[i].data(),
                       batchnorm_stddivs[i].data());

        output_channels = conv_biases[i + 1].size();
        std::swap(conv_out, conv_in);
        winograd_convolve3(output_channels, conv_in,
                           conv_weights[i + 1], V, M, conv_out);
        batchnorm<361>(output_channels, conv_out,
                       batchnorm_means[i + 1].data(),
                       batchnorm_stddivs[i + 1].data(),
                       res.data());
    }
    std::copy(begin(conv_out), end(conv_out), begin(output));
}

template<typename T>
T relative_difference(T a, T b) {
    // Handle NaN
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<T>::max();
    }
    // Handle sign difference
    if (((a < 0) != (b < 0)) && (a != 0) && (b != 0)) {
        return std::numeric_limits<T>::max();
    }
    a = std::fabs(a);
    b = std::fabs(b);

    // Handle underflow
    constexpr float small_number = 1e-3f;
    a = std::max(a, small_number);
    b = std::max(b, small_number);

    return std::max(fabs((a - b) / a), fabs((a - b) / b));
}

void compare_net_outputs(std::vector<float>& data,
                         std::vector<float>& ref) {
    // We accept an error up to 5%, but output values
    // smaller than 1/1000th are "rounded up" for the comparison.
    constexpr float relative_error = 5e-2f;
    for (auto idx = size_t{0}; idx < data.size(); ++idx) {
        auto err = relative_difference(data[idx], ref[idx]);
        if (err > relative_error) {
            printf("Error in OpenCL calculation: expected %f got %f "
                   "(error=%f%%)\n", ref[idx], data[idx], err * 100.0);
            printf("Update your GPU drivers or reduce the amount of games "
                   "played simultaneously.\n");
            throw std::runtime_error("OpenCL self-check mismatch.");
        }
    }
}
#endif

void Network::softmax(const std::vector<float>& input,
                      std::vector<float>& output,
                      float temperature) {
    assert(&input != &output);

    auto alpha = *std::max_element(begin(input),
                                   begin(input) + output.size());
    alpha /= temperature;

    auto denom = 0.0f;
    auto helper = std::vector<float>(output.size());
    for (auto i = size_t{0}; i < output.size(); i++) {
        auto val   = std::exp((input[i]/temperature) - alpha);
        helper[i]  = val;
        denom     += val;
    }
    for (auto i = size_t{0}; i < output.size(); i++) {
        output[i] = helper[i] / denom;
    }
}

int Network::rotate_nn_idx(const int vertex, int symmetry) {
    assert(vertex >= 0 && vertex < 19*19);
    assert(symmetry >= 0 && symmetry < 8);
    int x = vertex % 19;
    int y = vertex / 19;
    int newx;
    int newy;

    if (symmetry >= 4) {
        std::swap(x, y);
        symmetry -= 4;
    }

    if (symmetry == 0) {
        newx = x;
        newy = y;
    } else if (symmetry == 1) {
        newx = x;
        newy = 19 - y - 1;
    } else if (symmetry == 2) {
        newx = 19 - x - 1;
        newy = y;
    } else {
        assert(symmetry == 3);
        newx = 19 - x - 1;
        newy = 19 - y - 1;
    }

    int newvtx = (newy * 19) + newx;
    assert(newvtx >= 0 && newvtx < 19*19);
    return newvtx;
}

void Network::get_policy_moves(std::vector<FeedTensor>& ft_list,
                               std::vector<std::array<double,EBVCNT>>& prob_list,
                               int rotation,
                               bool skip_cache) {
    int ft_cnt = (int)ft_list.size();

    for (int i = 0; i < ft_cnt; ++i) {
        Prob move_prob;
        NNPlanes planes;
        tensor_to_plane(ft_list[i], planes);

        if (!skip_cache) {
            if (NNCache::get_NNCache().lookup_policy(planes, move_prob)) {
                prob_list.push_back(move_prob);
            } 
        }

        if (rotation != 8) {
            assert(rotation >= 0 && rotation <= 7);
            move_prob = get_policy_internal(ft_list[i], planes, rotation);
            prob_list.push_back(move_prob);
        } else {
            assert(rotation == 8);
            auto rand_rot = Random::get_Rng().randfix<8>();
            move_prob = get_policy_internal(ft_list[i], planes, rand_rot);
            prob_list.push_back(move_prob);
        }

        NNCache::get_NNCache().insert_policy(planes, move_prob);
    }
}

Network::Prob Network::get_policy_internal(const FeedTensor ft, 
                                           NNPlanes& planes, 
                                           int rotation) {
    assert(rotation >= 0 && rotation <= 7);
    assert(INPUT_CHANNELS == planes.size());
    constexpr int width = 19;
    constexpr int height = 19;
    const auto convolve_channels = conv_pol_w.size() / conv_pol_b.size();
    std::vector<net_t> input_data;
    std::vector<net_t> output_data(convolve_channels * width * height);
    std::vector<float> policy_data(2 * width * height);
    std::vector<float> value_data(1 * width * height);
    std::vector<float> policy_out((width * height) + 1);
    std::vector<float> softmax_data((width * height) + 1);
    std::vector<float> winrate_data(256);
    std::vector<float> winrate_out(1);
    // Data layout is input_data[(c * height + h) * width + w]
    input_data.reserve(INPUT_CHANNELS * width * height);
    for (int c = 0; c < INPUT_CHANNELS; ++c) {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                auto rot_idx = rotate_nn_idx_table[rotation][h * 19 + w];
                input_data.emplace_back(net_t(planes[c][rot_idx]));
            }
        }
    }
#ifdef USE_OPENCL
    opencl.forward(input_data, output_data);
#elif defined(USE_BLAS) && !defined(USE_OPENCL)
    forward_cpu(input_data, output_data);
#endif
#ifdef USE_OPENCL_SELFCHECK
    // Both implementations are available, self-check the OpenCL driver by
    // running both with a probability of 1/2000.
    if (Random::get_Rng().randfix<SELFCHECK_PROBABILITY>() == 0) {
        auto cpu_output_data = std::vector<float>(output_data.size());
        forward_cpu(input_data, cpu_output_data);
        compare_net_outputs(output_data, cpu_output_data);
    }
#endif
    // We calculate both network heads on the CPU. They are irregular
    // and have a much lower compute densitity than the residual layers,
    // which means they don't get much - if any - speedup from being on the
    // GPU. See issue #185.

    // Get the moves
    convolve<1>(2, output_data, conv_pol_w, conv_pol_b, policy_data);
    batchnorm<361>(2, policy_data, bn_pol_w1.data(), bn_pol_w2.data());
    innerproduct<2*361, 362>(policy_data, ip_pol_w, ip_pol_b, policy_out);
    softmax(policy_out, softmax_data, cfg_softmax_temp);
    std::vector<float>& outputs = softmax_data;

    Prob move_prob;
    move_prob.fill(0.0);

    for (int j = 0; j < BVCNT; ++j) {
        int v = rtoe[rotate_nn_idx_table[rotation][j]];
        float val = outputs[j];
        move_prob[v] = val;

        if (ft.feature[j][LADDERESC] != 0 && DistEdge(v) > 2) move_prob[v] *= 0.001;
    }

    return move_prob;
}

void Network::get_value_moves(std::vector<FeedTensor>& ft_list,
                              std::vector<float>& eval_list,
                              int rotation,
                              bool skip_cache) {
    int ft_cnt = (int)ft_list.size();

    for (int i = 0; i < ft_cnt; ++i) {
        float value;
        NNPlanes planes;
        tensor_to_plane(ft_list[i], planes);

        if (!skip_cache) {
            if (NNCache::get_NNCache().lookup_value(planes, value)) {
                eval_list.push_back(value);
            } 
        }

        if (rotation != 8) {
            assert(rotation >= 0 && rotation <= 7);
            value = get_value_internal(planes, rotation);
            eval_list.push_back(value);
        } else {
            assert(rotation == 8);
            auto rand_rot = Random::get_Rng().randfix<8>();
            value = get_value_internal(planes, rand_rot);
            eval_list.push_back(value);
        }

        NNCache::get_NNCache().insert_value(planes, value);
    }
}

float Network::get_value_internal(NNPlanes& planes, int rotation) {
    assert(rotation >= 0 && rotation <= 7);
    assert(INPUT_CHANNELS == planes.size());

    constexpr int width = 19;
    constexpr int height = 19;
    const auto convolve_channels = conv_pol_w.size() / conv_pol_b.size();
    std::vector<net_t> input_data;
    std::vector<net_t> output_data(convolve_channels * width * height);
    std::vector<float> policy_data(2 * width * height);
    std::vector<float> value_data(1 * width * height);
    std::vector<float> policy_out((width * height) + 1);
    std::vector<float> softmax_data((width * height) + 1);
    std::vector<float> winrate_data(256);
    std::vector<float> winrate_out(1);
    // Data layout is input_data[(c * height + h) * width + w]
    input_data.reserve(INPUT_CHANNELS * width * height);
    for (int c = 0; c < INPUT_CHANNELS; ++c) {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                auto rot_idx = rotate_nn_idx_table[rotation][h * 19 + w];
                input_data.emplace_back(net_t(planes[c][rot_idx]));
            }
        }
    }
#ifdef USE_OPENCL
    opencl.forward(input_data, output_data);
#elif defined(USE_BLAS) && !defined(USE_OPENCL)
    forward_cpu(input_data, output_data);
#endif
#ifdef USE_OPENCL_SELFCHECK
    // Both implementations are available, self-check the OpenCL driver by
    // running both with a probability of 1/2000.
    if (Random::get_Rng().randfix<SELFCHECK_PROBABILITY>() == 0) {
        auto cpu_output_data = std::vector<float>(output_data.size());
        forward_cpu(input_data, cpu_output_data);
        compare_net_outputs(output_data, cpu_output_data);
    }
#endif
    // We calculate both network heads on the CPU. They are irregular
    // and have a much lower compute densitity than the residual layers,
    // which means they don't get much - if any - speedup from being on the
    // GPU. See issue #185.

    // Now get the score
    convolve<1>(1, output_data, conv_val_w, conv_val_b, value_data);
    batchnorm<361>(1, value_data, bn_val_w1.data(), bn_val_w2.data());
    innerproduct<361, 256>(value_data, ip1_val_w, ip1_val_b, winrate_data);
    innerproduct<256, 1>(winrate_data, ip2_val_w, ip2_val_b, winrate_out);

    // Sigmoid
    // auto winrate_sig = (1.0f + std::tanh(winrate_out[0])) / 2.0f;
    // Not sure whether the range shouid be changed
    float eval_value = std::tanh(winrate_out[0]);

    return eval_value;
}

void Network::tensor_to_plane(const FeedTensor ft, NNPlanes& planes) {
    planes.resize(INPUT_CHANNELS);
    BoardPlane& black_to_move = planes[2 * INPUT_MOVES];
    BoardPlane& white_to_move = planes[2 * INPUT_MOVES + 1];

    const bool blacks_move = (ft.feature[0][COLOR] == 1);
    const int black_offset = blacks_move ? 0 : INPUT_MOVES;
    const int white_offset = blacks_move ? INPUT_MOVES : 0;
    const int gmz_black_offset = blacks_move ? 0 : 1;
    const int gmz_white_offset = blacks_move ? 1 : 0;

    if (blacks_move) {
        black_to_move.set();
    } else {
        white_to_move.set();
    }

    for (int j = 0; j < BVCNT; ++j) {
        for (int k = 0; k < INPUT_MOVES; ++k) {
            if (ft.feature[j][gmz_black_offset + 2*k] != 0.0) {
                planes[black_offset + k][j] = true;
            }
            if (ft.feature[j][gmz_white_offset + 2*k] != 0.0) {
                planes[white_offset + k][j] = true;
            }
        }
    }
}


void Network::debug_heatmap(const FeedTensor ft, Prob move_prob) {
    bool show_tensor = false;
    bool show_plane = false;
    bool show_dmap = false;
    bool show_rmap = true;
    bool show_value = true;
    bool show_cache = true;

    NNPlanes planes;
    tensor_to_plane(ft, planes);
    
    if (show_tensor) {
        for (int a = 0; a < 17; ++a) {
            myprintf("Tensor %d\n", a);
            for (int b = 17; b >= 0; --b) {
                for (int c = 0; c < 19; ++c) {
                    float mv = ft.feature[b*19+c][a];
                    if (mv > 0.1) {
                        myprintf("\033[41m%3.1f\033[0m", mv);
                    } else {
                        myprintf("%3.1f", mv);
                    }
                    myprintf(" ");
                }
                myprintf("\n");
            }
            myprintf("\n");
        }
    }
    
    if (show_plane) {
        for (int a = 0; a < 18; ++a) {
            myprintf("Plane %d\n", a);
            for (int b = 18; b >= 0; --b) {
                for (int c = 0; c < 19; ++c) {
                    float mv = planes[a][b*19+c];
                    if (mv > 0.1) {
                        myprintf("\033[41m%3.1f\033[0m", mv);
                    } else {
                        myprintf("%3.1f", mv);
                    }
                    myprintf(" ");
                }
                myprintf("\n");
            }
            myprintf("\n");
        }
    }

    if (show_dmap) {
        Prob direct_heatmap = get_policy_internal(ft, planes, 0);
        myprintf("Direct Heatmap\n");
        for (int i = 18; i >= 0; --i) {
            for (int j = 0; j < 19; ++j) {
                float mp = direct_heatmap[rtoe[i * 19 + j]];
                if (mp > 0.05) {
                    myprintf("\033[41m%3.1f\033[0m", mp);
                } else {
                    myprintf("%3.1f", mp);
                }
                myprintf(" ");
            }
            myprintf("\n");
        }
        myprintf("\n");
    }

    if (show_rmap) {
        myprintf("Rotated Heatmap                             ");
        myprintf("Ladder Feature\n");
        myprintf("   A B C D E F G H J K L M N O P Q R S T    ");
        myprintf("   A B C D E F G H J K L M N O P Q R S T\n");
        for (int i = 18; i >= 0; --i) {
            myprintf("%2d ", i + 1);
            for (int j = 0; j < 19; ++j) {
                int mp = 10 * move_prob[rtoe[i * 19 + j]];
                if (mp > 0.5) {
                    myprintf("%d ", mp);
                } else {
                    myprintf(". ");
                }
            }
            myprintf("   %2d ", i + 1);
            for (int k =0; k < 19; ++k) {
                float lde = ft.feature[i*19+k][LADDERESC];
                float ldc = ft.feature[i*19+k][LADDERCAP];
                if (lde > 0.5) {
                    myprintf("X ");
                } else if (ldc > 0.5) {
                    myprintf("O ");
                } else {
                    myprintf(". ");
                }
            }
            myprintf("\n");
        }
        myprintf("\n");
    }

    if (show_value) {
        float value = get_value_internal(planes, 0);
        myprintf("Original Value: %5.2f\n", value);
    }
    
    if (show_cache) {
        NNCache::get_NNCache().dump_stats();
    }

}