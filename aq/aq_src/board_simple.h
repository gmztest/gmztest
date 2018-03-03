#pragma once

#include <vector>

#include "board_config.h"
#include "distance.h"
#include "pattern3x3.h"
#include "zobrist.h"
#include "board.h"


/**************************************************************
 *
 *  Class which stores difference of board from previous status.
 *  Inputs (index) or (index, original value).
 *
 ***************************************************************/
struct Diff {
    // Positions where color changes.
    std::vector<int> color;

    // (index, original value).
    std::vector<std::pair<int,int>> empty;

    // (index, original value).
    std::vector<std::pair<int,int>> empty_idx;

    // Previous position of Ko.
    int ko;

    // (index, original value).
    std::vector<std::pair<int,Ren>> ren;

    // (index, original value).
    std::vector<std::pair<int,int>> next_ren_v;

    // (index, original value).
    std::vector<std::pair<int,int>> ren_idx;

    // (index, original bf).
    std::vector<std::pair<int,int>> ptn;
};


/**************************************************************
 *
 *  Class of quick board.
 *
 *  since storing difference from the previous board,
 *  it is possible to undo recursively.
 *  This class is used when the probability is not needed,
 *  such as the ladder search.
 *
 ***************************************************************/
class BoardSimple {
private:

    bool color_ch[EBVCNT];
    bool empty_ch[BVCNT];
    bool empty_idx_ch[EBVCNT];
    bool ren_ch[EBVCNT];
    bool next_ren_v_ch[EBVCNT];
    bool ren_idx_ch[EBVCNT];
    bool ptn_ch[EBVCNT];

    void SetAtari(int v);
    void SetPreAtari(int v);
    void CancelAtari(int v);
    void CancelPreAtari(int v);
    void PlaceStone(int v);
    void RemoveStone(int v);
    void MergeRen(int v_base, int v_add);
    void RemoveRen(int v);
    bool IsSelfAtariNakade(int v) const;

public:

    // Turn index. (0: white, 1: black)
    // if black's turn, (my, her) = (1, 0) else (0, 1).
    int my, her;

    // Stone color.
    // empty->0, outer boundary->1, white->2, black->3
    int color[EBVCNT];

    // List of empty vertexes, containing their positions in range of [0, empty_cnt-1].
    // Ex. for (int i = 0; i < empty_cnt; ++i) v = empty[i]; ...
    int empty[BVCNT];

    // empty_idx[v] < empty_cnt ‚È‚ç‚Î v‚Í‹ó“_.
    // Empty-vertex index of each position.
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

    // 3x3 patterns.
    Pattern3x3 ptn[EBVCNT];

    // Information of Difference from initial status.
    std::vector<Diff> diff;

    // Number of stored Diff.
    int diff_cnt;
    
    BoardSimple();
    BoardSimple(const Board& other);
    BoardSimple& operator=(const Board& other);
    BoardSimple& operator=(const BoardSimple& other);
    void Clear();
    bool IsLegal(int pl, int v) const;
    bool IsEyeShape(int pl, int v) const;
    bool IsFalseEye(int v) const;
    bool IsSeki(int v) const;
    void PlayLegal(int v);
    void Undo();

};

