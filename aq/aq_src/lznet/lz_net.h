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


#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

#include "config.h"

#include <array>
#include <bitset>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fstream>

#include "../feed_tensor.h"

extern int cfg_sym_idx;
extern bool cfg_sgemm_exhaustive;
extern std::string cfg_weightsfile;
const float cfg_softmax_temp = 1.0f;

class Network {
public:
    using BoardPlane = std::bitset<19*19>;
    using NNPlanes = std::vector<BoardPlane>;
    using Prob = std::array<double, EBVCNT>;

    static void get_policy_moves(std::vector<FeedTensor>& ft_list,
                                 std::vector<std::array<double,EBVCNT>>& prob_list,
                                 int rotation = 8,
                                 bool skip_cache = false);
    static void get_value_moves(std::vector<FeedTensor>& ft_list,
                                std::vector<float>& eval_list,
                                int rotation = 8,
                                bool skip_cache = false);

    // File format version
    static constexpr auto FORMAT_VERSION = 1;
    static constexpr auto INPUT_MOVES = 8;
    static constexpr auto INPUT_CHANNELS = 2 * INPUT_MOVES + 2;

    // Winograd filter transformation changes 3x3 filters to 4x4
    static constexpr auto WINOGRAD_ALPHA = 4;
    static constexpr auto WINOGRAD_TILE = WINOGRAD_ALPHA * WINOGRAD_ALPHA;

    static void initialize();

    static void softmax(const std::vector<float>& input,
                        std::vector<float>& output,
                        float temperature = 1.0f);

    static void debug_heatmap(const FeedTensor ft, Prob move_prob);

private:
    static std::pair<int, int> load_v1_network(std::ifstream& wtfile);
    static std::pair<int, int> load_network_file(std::string filename);
    static void process_bn_var(std::vector<float>& weights,
                               const float epsilon = 1e-5f);

    static std::vector<float> winograd_transform_f(const std::vector<float>& f,
        const int outputs, const int channels);
    static std::vector<float> zeropad_U(const std::vector<float>& U,
        const int outputs, const int channels,
        const int outputs_pad, const int channels_pad);
    static void winograd_transform_in(const std::vector<float>& in,
                                      std::vector<float>& V,
                                      const int C);
    static void winograd_transform_out(const std::vector<float>& M,
                                       std::vector<float>& Y,
                                       const int K);
    static void winograd_convolve3(const int outputs,
                                   const std::vector<float>& input,
                                   const std::vector<float>& U,
                                   std::vector<float>& V,
                                   std::vector<float>& M,
                                   std::vector<float>& output);
    static void winograd_sgemm(const std::vector<float>& U,
                               std::vector<float>& V,
                               std::vector<float>& M, const int C, const int K);
    static int rotate_nn_idx(const int vertex, int symmetry);
#if defined(USE_BLAS)
    static void forward_cpu(std::vector<float>& input,
                            std::vector<float>& output);
#endif
    static float get_value_internal(NNPlanes& planes, int rotation);
    static Prob get_policy_internal(const FeedTensor ft, NNPlanes& planes, int rotation);
    static void tensor_to_plane(const FeedTensor ft, NNPlanes& planes);
};

#endif
