#include <algorithm>

#include "print.h"
#include "ladder.h"


/**
 *  Find the positions of the moves to escape meaninglessly
 *  and to capture stones in Laddar.
 */
bool SearchLadder(Board& b, std::vector<int>(&ladder_list)[2]) {

    // ladder_list[0]: the positions of the move to escape from Ladder.
    // ladder_list[1]: the positions of the move to take stones with Ladder.
    for (auto& i: ladder_list) i.clear();

    BoardSimple bs = b;

    for (int i = 0, n = bs.empty_cnt; i < n; ++i) {
        int v = bs.empty[i];
        // Check whether it is a move to escape from Ladder.
        if (IsWastefulEscape(bs, bs.my, v)) ladder_list[0].push_back(v);
        // Check whether it is a move to make stones in Atari and Ladder.
        if (IsSuccessfulCapture(bs, bs.my, v)) ladder_list[1].push_back(v);
    }

    // Return true if there are any ladder stones.
    return (ladder_list[0].size() + ladder_list[1].size()) > 0;

}


/**
 *  Return whether the move to escape on v is wasteful due to Ladder.
 */
bool IsWastefulEscape(BoardSimple& b, int pl, int v) {

    // Return false if number of pl's stones is 0 or that of empty is not 2.
    if (!b.ptn[v].IsAtari()            ||
        b.ptn[v].StoneCnt(pl) == 0     ||
        b.ptn[v].EmptyCnt() == 3       ||
        !b.IsLegal(pl, v)) 
    {
        return false;
    }

    std::vector<int> atr_ren_list;
    int my_color = pl + 2;
    bool is_atari = false;

    // 1. Check neighboring 4 positions.
    for (auto dv: VSHIFT) {
        int v_nbr = v + dv;        //VSHIFT = {21, 1, -21, -1}

        if (b.color[v_nbr] == my_color) {
            // Return false when the neighboring pl's Ren has
            // more than 4 liberty vertexes.
            if (b.ren[b.ren_idx[v_nbr]].lib_cnt >= 4) {
                return false;
            }
            // Check whether the pl's Ren is in Atari.
            else if (b.ren[b.ren_idx[v_nbr]].IsAtari()) {
                is_atari = true;
                atr_ren_list.push_back(b.ren_idx[v_nbr]);
            }
        }
    }

    if (is_atari) {
        // Remove duplicated indexes.
        sort(atr_ren_list.begin(), atr_ren_list.end());
        atr_ren_list.erase(unique(atr_ren_list.begin(),atr_ren_list.end()),atr_ren_list.end());

        // 2. Return true if any of the Rens in Atari is taken with Ladder.
        for (auto atr_ren_idx: atr_ren_list) {
            if (IsLadder(b, atr_ren_idx, 0)) {
                // Return true since the Ren will be captured.
                return true;
            }
        }
    }

    return false;

}

/**
 *  Return whether the move on v captures opponent's stones with Ladder.
 */
bool IsSuccessfulCapture(BoardSimple& b, int pl, int v) {

    // Return false if number of pl's stones or stones in pre-Atari is 0.
    int opp_pl = int(pl == 0);
    if (!b.ptn[v].IsPreAtari()         ||
        b.ptn[v].StoneCnt(opp_pl) == 0 ||
        !b.IsLegal(pl, v)) 
    {
        return false;
    }

    bool is_patr = false;
    std::vector<int> patr_ren_list;

    // 1. Check neighboring 4 positions.
    for (auto dv: VSHIFT) {
        int v_nbr = v + dv;
        // Opponent's stone with 2 liberty vertexes.
        if (b.color[v_nbr] == (opp_pl + 2) &&
            b.ren[b.ren_idx[v_nbr]].IsPreAtari())
        {
            patr_ren_list.push_back(b.ren_idx[v_nbr]);
            is_patr = true;
        }
    }

    // 2. Check whether the Ren can be captured by making it in Atari.
    if (is_patr) {
        // Remove duplicated indexes.
        sort(patr_ren_list.begin(), patr_ren_list.end());
        patr_ren_list.erase(unique(patr_ren_list.begin(),patr_ren_list.end()),patr_ren_list.end());

        for (auto patr_ren_idx: patr_ren_list) {

            int v_atr = patr_ren_idx;
            b.PlayLegal(v);
            bool is_ladder = IsLadder(b, v_atr, 0);
            b.Undo();
            if (is_ladder) return true;

        }
    }

    return false;

}

/**
 *  Return whether the Ren including v_atr is captured when it escapes.
 */
bool IsLadder(BoardSimple& b, int v_atr, int depth) {

    int pl = b.color[v_atr] - 2;
    int her_color = int(pl == 0) + 2;
    int v_esc = b.ren[b.ren_idx[v_atr]].lib_atr;

    if (depth >= 128 || b.my != pl) return false;

    // 1. Check whether surrounding stones can be taken.
    std::vector<int> v_cap_list;
    int v_tmp = v_atr;
    do {
        for (auto dv: VSHIFT) {
            if (b.color[v_tmp + dv] == her_color &&
                b.ren[b.ren_idx[v_tmp + dv]].IsAtari() &&
                b.IsLegal(b.my, b.ren[b.ren_idx[v_tmp + dv]].lib_atr))
            {
                v_cap_list.push_back(b.ren[b.ren_idx[v_tmp + dv]].lib_atr);
            }
        }
        v_tmp = b.next_ren_v[v_tmp];
    } while (v_tmp != v_atr);

    // 2. Search after capturing the surrounding stones.
    if (!v_cap_list.empty()) {
        // Remove duplicated indexes.
        sort(v_cap_list.begin(), v_cap_list.end());
        v_cap_list.erase(unique(v_cap_list.begin(),v_cap_list.end()),v_cap_list.end());

        for (auto v_cap: v_cap_list) {
            // Capture opponent's stones.
            b.PlayLegal(v_cap);
            if (b.ren[b.ren_idx[v_atr]].lib_cnt > 2) {
                // Return false when number of liberty > 2.
                b.Undo();
                return false;
            } else {
                int libs[2] = {v_esc, v_cap};
                bool is_ladder = false;
                for (auto lib: libs) {
                    if (b.IsLegal(b.my, lib)) {
                        b.PlayLegal(lib);
                        // Recursive search.
                        is_ladder |= IsLadder(b, v_atr, depth + 1);
                        b.Undo();
                    }
                }

                b.Undo();
                if (!is_ladder) return false; // Successfully escape.
            }
        }
    }

    // 3. Escape to v_esc.
    if (!b.IsLegal(b.my, v_esc)) {
        return true;
    } else {
        b.PlayLegal(v_esc);
    }

    // 4. Count liberty vertexes after placing the stone.
    int atr_ren_idx = b.ren_idx[v_atr];
    bool is_ladder = false;
    if (b.ren[atr_ren_idx].lib_cnt <= 1) {
        is_ladder = true;    // Captured.
    } else if (b.ren[atr_ren_idx].lib_cnt > 2) {
        is_ladder = false;     // Survives.
    } else {
        int libs[2];
        int lib_cnt = 0;
        int64 lib_bit;
        for (int i = 0; i < 6; ++i) {
            lib_bit = b.ren[atr_ren_idx].lib_bits[i];
            while (lib_bit != 0) {
                if (lib_cnt == 2) break;

                int ntz = NTZ(lib_bit);
                libs[lib_cnt] = rtoe[ntz + i * 64];
                ++lib_cnt;

                lib_bit ^= (0x1ULL << ntz);
            }
        }

        // 5. Play the move making the Ren in Atari.
        for (auto lib: libs) {
            if (!b.IsLegal(b.my, lib)) continue;
            b.PlayLegal(lib);
            // Recursive search.
            is_ladder = IsLadder(b, v_atr, depth + 1);
            b.Undo();
            if (is_ladder) break;
        }
    }

    b.Undo();
    return is_ladder;
}
