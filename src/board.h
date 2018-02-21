#pragma once

#include <cstring>
#include <vector>
#include <nmmintrin.h>

#include "board_config.h"
#include "distance.h"
#include "pattern3x3.h"
#include "zobrist.h"

#ifdef _WIN32
    #define COMPILER_MSVC
#endif

#if defined (_M_IX86)
inline int popcnt32twice(int64 bb) {
        return _mm_popcnt_u32(unsigned int(bb)) + \
               _mm_popcnt_u32(unsigned int(bb >> 32));
    }
    #define popcnt64 popcnt32twice
#else
    #define popcnt64 _mm_popcnt_u64
#endif  /* defined (_M_IX86) */

/**
 *  Function for finding an unsigned 64-integer NTZ.
 *  Ex. 0x01011000 -> 3
 *      0x0 -> 64
 */
constexpr auto magic64 = 0x03F0A933ADCBD8D1ULL;
constexpr int ntz_table64[127] = {
    64,  0, -1,  1, -1, 12, -1,  2, 60, -1, 13, -1, -1, 53, -1,  3,
    61, -1, -1, 21, -1, 14, -1, 42, -1, 24, 54, -1, -1, 28, -1,  4,
    62, -1, 58, -1, 19, -1, 22, -1, -1, 17, 15, -1, -1, 33, -1, 43,
    -1, 50, -1, 25, 55, -1, -1, 35, -1, 38, 29, -1, -1, 45, -1,  5,
    63, -1, 11, -1, 59, -1, 52, -1, -1, 20, -1, 41, 23, -1, 27, -1,
    -1, 57, 18, -1, 16, -1, 32, -1, 49, -1, -1, 34, 37, -1, 44, -1,
    -1, 10, -1, 51, -1, 40, -1, 26, 56, -1, -1, 31, 48, -1, 36, -1,
     9, -1, 39, -1, -1, 30, 47, -1,  8, -1, -1, 46,  7, -1,  6,
};

inline constexpr int NTZ(int64 x) noexcept {
    return ntz_table64[static_cast<int64>(magic64*static_cast<int64>(x&-x))>>57];
}

extern const double response_w[4][2];
extern const double semeai_w[2][2];

#define USE_52FEATURE

/**************************************************************
 *
 *  Class of Ren.
 *
 *  Adjacent stones form a single stone string (=Ren).
 *  Neighboring empty vertexes are call as 'liberty',
 *  and the Ren is captured when all liberty is filled.
 *
 *  Positions of the liberty are held in a bitboard
 *  (64-bit integer x 6), which covers the real board (19x19).
 *  The liberty number decrease (increase) when a stone is
 *  placed on (removed from) a neighboring position.
 *
 ***************************************************************/
class Ren {
public:

    // Bitboard of liberty positions.
    // [0] -> 0-63, [1] -> 64-127, ..., [5] -> 320-360
    int64 lib_bits[6];

    int lib_atr;     // The liberty position in case of Atari.
    int lib_cnt;    // Number of liberty.
    int size;        // Number of stones.

    Ren() { Clear(); }
    Ren(const Ren& other) { *this = other; }
    Ren& operator=(const Ren& other) {
        lib_cnt = other.lib_cnt;
        size     = other.size;
        lib_atr = other.lib_atr;
        std::memcpy(lib_bits, other.lib_bits, sizeof(lib_bits));

        return *this;
    }

    // Initialize for stone.
    void Clear() {
        lib_cnt     = 0;
        size        = 1;
        lib_atr     = VNULL;
        lib_bits[0] = lib_bits[1] = lib_bits[2] = \
        lib_bits[3] = lib_bits[4] = lib_bits[5] = 0;
    }

    // Initialize for empty and outer boundary.
    void SetNull() {
        lib_cnt     = VNULL; //442
        size        = VNULL;
        lib_atr     = VNULL;
        lib_bits[0] = lib_bits[1] = lib_bits[2] = \
        lib_bits[3] = lib_bits[4] = lib_bits[5] = 0;
    }

    // Add liberty at v.
    void AddLib(int v) {
        if (size == VNULL) return;

        int bit_idx = etor[v] / 64;
        int64 bit_v = (0x1ULL << (etor[v] % 64));

        if (lib_bits[bit_idx] & bit_v) return;
        lib_bits[bit_idx] |= bit_v;
        ++lib_cnt;
        lib_atr = v;
    }

    // Delete liberty at v.
    void SubLib(int v) {
        if (size == VNULL) return;

        int bit_idx = etor[v] / 64;
        int64 bit_v = (0x1ULL << (etor[v] % 64));
        if (lib_bits[bit_idx] & bit_v) {
            lib_bits[bit_idx] ^= bit_v;
            --lib_cnt;

            if (lib_cnt == 1) {
                for (int i = 0; i < 6; ++i) {
                    if (lib_bits[i] != 0) {
                        lib_atr = rtoe[NTZ(lib_bits[i]) + i * 64];
                    }
                }
            }
        }
    }

    // Merge with another Ren.
    void Merge(const Ren& other) {
        lib_cnt = 0;
        for (int i = 0; i < 6; ++i) {
            lib_bits[i] |= other.lib_bits[i];
            if (lib_bits[i] != 0) {
                lib_cnt += (int)popcnt64(lib_bits[i]);
            }
        }
        if (lib_cnt == 1) {
            for (int i = 0; i < 6; ++i) {
                if (lib_bits[i] != 0) {
                    lib_atr = rtoe[NTZ(lib_bits[i]) + i * 64];
                }
            }
        }
        size += other.size;
    }

    // Return whether this Ren is captured.
    bool IsCaptured() const { return lib_cnt == 0; }

    // Return whether this Ren is Atari.
    bool IsAtari() const { return lib_cnt == 1; }

    // Return whether this Ren is pre-Atari.
    bool IsPreAtari() const { return lib_cnt == 2; }

    // Return the liberty position of Ren in Atari.
    int GetAtari() const { return lib_atr; }

};


/*********************************************************************************
 *
 *  Class of board.
 *
 *  The data structure of the board is treated as the 1D-coordinate system of 441 points.
 *  A position 'v' has information of "stone color", "3x3 pattern", "Ren" and
 *  "next position of its Ren".
 *
 *  1. stone color: color[v]
 *     empty->0, outer boundary->1, white->2, black->3.
 *
 *  2. 3x3 pattern: ptn[v]
 *     Pattern3x3 class which has information of surrounding stones and liberty.
 *     This is used to calculate legal judgment etc quickly by bit operation.
 *
 *  3. Ren: ren[ren_idx[v]]
 *     The ren index at v is stored in ren_idx[v]. When Rens are merged,
 *     their ren indexes are unified to either index.
 *     Ren class is used for managing stone counts and liberty.
 *
 *  4. next position of its Ren: next_ren_v[v]
 *     This is used for operation around the ren (3x3 pattern update etc.).
 *     Since position information circulates, it can be referred to from any
 *     vertex. (Ex. v0->v1->...->v7->v0)
 *     For a ren of size 1, next_ren_v[v] = v.
 *
 *********************************************************************************/
class Board {
private:
    // Flag indicating whether 3x3 pattern has been updated.
    bool is_ptn_updated[EBVCNT];

    // List of (position, previous value of bf) of updated patterns.
    std::vector<std::pair<int,int>> updated_ptns;

    void AddUpdatedPtn(int v);
    void SetAtari(int v);
    void SetPreAtari(int v);
    void CancelAtari(int v);
    void CancelPreAtari(int v);
    void PlaceStone(int v);
    void RemoveStone(int v);
    void MergeRen(int v_base, int v_add);
    void RemoveRen(int v);
    bool IsSelfAtariNakade(int v) const;
    bool IsSelfAtari(int pl, int v) const;
    bool Semeai2(std::vector<int>& patr_rens, std::vector<int>& her_libs);
    bool Semeai3(std::vector<int>& lib3_rens, std::vector<int>& her_libs);
    void UpdatePrevPtn(int v);
    void SubPrevPtn();
    void AddProb(int pl, int v, double add_prob);
    void UpdateProbAll();
    void AddProbDist(int v);
    void SubProbDist();

public:

    // Turn index. (0: white, 1: black)
    // if black's turn, (my, her) = (1, 0) else (0, 1).
    int my, her;

    // Stone color.
    // empty->0, outer boundary->1, white->2, black->3
    int color[EBVCNT];

    // History of color information
    // prev_color[n]: color at (n+1) moves before
    int prev_color[7][EBVCNT];

    // List of empty vertexes, containing their positions in range of [0, empty_cnt-1].
    // Ex. for (int i = 0; i < empty_cnt; ++i) v = empty[i]; ...
    int empty[BVCNT];

    // Empty vertex index of each position.
    // if empty_idx[v] < empty_cnt, v is empty.
    int empty_idx[EBVCNT];

    // [0]: number of white stones  [1]: number of black stones.
    int stone_cnt[2];

    // Number of empty vertexes.
    int empty_cnt;

    // Position of the illegal move of Ko.
    int ko;

    // Ren index.
    int ren_idx[EBVCNT];

    // Ren corresponding to the ren index.
    // Ex. ren[ren_idx[v]]
    Ren ren[EBVCNT];

    // Next position of another stone in the Ren.
    int next_ren_v[EBVCNT];

    // Number of the moves.
    int move_cnt;

    // History of the moves.
    std::vector<int> move_history;

    // [0]: white's previous move [1]: black's previous move.
    int prev_move[2];

    // Previous position of illegal move of Ko.
    int prev_ko;

    // List of stones removed in the current move.
    std::vector<int> removed_stones;

    // Probability of each vertex.
    double prob[2][EBVCNT];

    // 3x3 patterns.
    Pattern3x3 ptn[EBVCNT];

    // Twelve-point patterns around last and two moves before moves.
    Pattern3x3 prev_ptn[2];

    // The probability value of the response pattern that was just updated
    double prev_ptn_prob;

    // Reflex move, such as Nakade or save stones in Atari.
    int response_move[4];
    std::vector<int> semeai_move[2];

    // Number of pass. (for Japanese rule)
    int pass_cnt[2];

    // Sum of probability for each rank.
    double sum_prob_rank[2][BSIZE];

    Board();
    Board(const Board& other);
    Board& operator=(const Board& other);
    void Clear();
    bool IsLegal(int pl, int v) const;
    bool IsEyeShape(int pl, int v) const;
    bool IsFalseEye(int v) const;
    bool IsSeki(int v) const;
    void PlayLegal(int v);
    void ReplaceProb(int pl, int v, double new_prob);
    void RecalcProbAll();
    void AddProbPtn12();
    int SelectRandomMove();
    int SelectMove();
    bool IsMimicGo();

};

