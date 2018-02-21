#include <algorithm>
#include <assert.h>
#include <unordered_set>

#include "board_simple.h"

/**
 *  Macro that executes the same processing on surrounding vertexes.
 */
#define forEach4Nbr(v_origin,v_nbr,block)             \
    int v_nbr;                                        \
    v_nbr = v_origin + 1;            block;           \
    v_nbr = v_origin - 1;            block;           \
    v_nbr = v_origin + EBSIZE;        block;          \
    v_nbr = v_origin - EBSIZE;        block;

#define forEach4Diag(v_origin,v_diag,block)           \
    int v_diag;                                       \
    v_diag = v_origin + EBSIZE + 1;    block;         \
    v_diag = v_origin + EBSIZE - 1;    block;         \
    v_diag = v_origin - EBSIZE + 1;    block;         \
    v_diag = v_origin - EBSIZE - 1;    block;        

#define forEach8Nbr(v_origin,v_nbr,d_nbr,d_opp,block)                                \
    int v_nbr; int d_nbr; int d_opp;                                                 \
    v_nbr = v_origin + EBSIZE;            d_nbr = 0; d_opp = 2;        block;        \
    v_nbr = v_origin + 1;                 d_nbr = 1; d_opp = 3;        block;        \
    v_nbr = v_origin - EBSIZE;            d_nbr = 2; d_opp = 0;        block;        \
    v_nbr = v_origin - 1;                 d_nbr = 3; d_opp = 1;        block;        \
    v_nbr = v_origin + EBSIZE - 1;        d_nbr = 4; d_opp = 6;        block;        \
    v_nbr = v_origin + EBSIZE + 1;        d_nbr = 5; d_opp = 7;        block;        \
    v_nbr = v_origin - EBSIZE + 1;        d_nbr = 6; d_opp = 4;        block;        \
    v_nbr = v_origin - EBSIZE - 1;        d_nbr = 7; d_opp = 5;        block;


BoardSimple::BoardSimple() {

    Clear();

}


BoardSimple::BoardSimple(const Board& other) {

    *this = other;

}


BoardSimple& BoardSimple::operator=(const Board& other) {

    my = other.my;
    her = other.her;
    std::memcpy(color, other.color, sizeof(color));
    std::memcpy(empty, other.empty, sizeof(empty));
    std::memcpy(empty_idx, other.empty_idx, sizeof(empty_idx));
    std::memcpy(stone_cnt, other.stone_cnt, sizeof(stone_cnt));
    empty_cnt = other.empty_cnt;
    ko = other.ko;

    std::memcpy(ren, other.ren, sizeof(ren));
    std::memcpy(next_ren_v, other.next_ren_v, sizeof(next_ren_v));
    std::memcpy(ren_idx, other.ren_idx, sizeof(ren_idx));
    move_cnt = other.move_cnt;
    move_history.clear();
    copy(other.move_history.begin(), other.move_history.end(), back_inserter(move_history));
    std::memcpy(ptn, other.ptn, sizeof(ptn));

    diff.clear();
    diff_cnt = 0;

    return *this;

}


BoardSimple& BoardSimple::operator=(const BoardSimple& other) {

    my = other.my;
    her = other.her;
    std::memcpy(color, other.color, sizeof(color));
    std::memcpy(empty, other.empty, sizeof(empty));
    std::memcpy(empty_idx, other.empty_idx, sizeof(empty_idx));
    std::memcpy(stone_cnt, other.stone_cnt, sizeof(stone_cnt));
    empty_cnt = other.empty_cnt;
    ko = other.ko;

    std::memcpy(ren, other.ren, sizeof(ren));
    std::memcpy(next_ren_v, other.next_ren_v, sizeof(next_ren_v));
    std::memcpy(ren_idx, other.ren_idx, sizeof(ren_idx));
    move_cnt = other.move_cnt;
    move_history.clear();
    copy(other.move_history.begin(), other.move_history.end(), back_inserter(move_history));
    std::memcpy(ptn, other.ptn, sizeof(ptn));

    diff.clear();
    copy(other.diff.begin(), other.diff.end(), back_inserter(diff));
    diff_cnt = other.diff_cnt;

    return *this;

}

/**
 *  Initialize the board.
 */
void BoardSimple::Clear() {

    // if black's turn, (my, her) = (1, 0) else (0, 1).
    my = 1;
    her = 0;
    empty_cnt = 0;

    for (int i = 0; i < EBVCNT; ++i) {

        next_ren_v[i] = i;
        ren_idx[i] = i;

        ren[i].SetNull();

        int ex = etox[i];
        int ey = etoy[i];

        // outer baundary
        if (ex == 0 || ex == EBSIZE - 1 || ey == 0 || ey == EBSIZE - 1) {
            //Empty->0, Outboard->1, White->2, Black->3
            color[i] = 1;
            empty_idx[i] = VNULL;    //442
            ptn[i].SetNull();        //0xffffffff
        }
        // real board
        else {
            // empty vertex
            color[i] = 0;
            empty_idx[i] = empty_cnt;
            empty[empty_cnt] = i;
            ++empty_cnt;
        }

    }

    for (int i = 0; i < BVCNT; ++i) {

        int v = rtoe[i];

        ptn[v].Clear();    //0x00000000

        // Set colors around v.
        forEach8Nbr(v, v_nbr8, d_nbr, d_opp, {
            ptn[v].SetColor(d_nbr, color[v_nbr8]);
        });

    }

    stone_cnt[0] = stone_cnt[1] = 0;
    empty_cnt = BVCNT;
    ko = VNULL;
    move_cnt = 0;
    move_history.clear();

    diff.clear();
    diff_cnt = 0;

    memset(&color_ch, false, sizeof(color_ch));
    memset(&empty_ch, false, sizeof(empty_ch));
    memset(&empty_idx_ch, false, sizeof(empty_idx_ch));
    memset(&ren_ch, false, sizeof(ren_ch));
    memset(&next_ren_v_ch, false, sizeof(next_ren_v_ch));
    memset(&ren_idx_ch, false, sizeof(ren_idx_ch));
    memset(&ptn_ch, false, sizeof(ptn_ch));
}

/**
 *  Return whether pl's move on v is legal.
 */
bool BoardSimple::IsLegal(int pl, int v) const {

    assert(v <= PASS);

    if (v == PASS) return true;
    if (color[v] != 0 || v == ko) return false;

    return ptn[v].IsLegal(pl);

}

/**
 *  Return whether v is an eye shape for pl.
 */
bool BoardSimple::IsEyeShape(int pl, int v) const {

    assert(color[v] == 0);

    if (ptn[v].IsEnclosed(pl)) {

        // Counter of {empty, outer boundary, white, black} in diagonal positions.
        int diag_cnt[4] = {0, 0, 0, 0};

        for (int i = 4; i < 8; ++i) {
            //4=NW, 5=NE, 6=SE, 7=SW
            ++diag_cnt[ptn[v].ColorAt(i)];
        }

        // False eye if opponent's stones + outer boundary >= 2.
        int wedge_cnt = diag_cnt[int(pl==0) + 2] + int(diag_cnt[1] > 0);

        // Return true if an opponent's stone can be taken immediately.
        if (wedge_cnt == 2) {
            forEach4Diag(v, v_diag, {
                if (color[v_diag] == (int(pl==0) + 2)) {
                    if (ren[ren_idx[v_diag]].IsAtari() &&
                        ren[ren_idx[v_diag]].lib_atr != ko)
                    {
                        return true;
                    }
                }
            });
        }
        // Return true if it is not false eye.
        else return wedge_cnt < 2;
    }

    return false;
}

/**
 *  Return whether v is a false eye.
 */
bool BoardSimple::IsFalseEye(int v) const {

    assert(color[v] == 0);

    // Return false when empty vertexes adjoin.
    if (ptn[v].EmptyCnt() > 0) return false;

    // Return false when it is not enclosed by opponent's stones.
    if (!ptn[v].IsEnclosed(0) && !ptn[v].IsEnclosed(1)) return false;

    int pl = ptn[v].IsEnclosed(0) ? 0 : 1;
    // Counter of {empty, outer boundary, white, black} in diagonal positions.
    int diag_cnt[4] = {0, 0, 0, 0};
    for (int i = 4; i < 8; ++i) {
        //4=NW, 5=NE, 6=SE, 7=SW
        ++diag_cnt[ptn[v].ColorAt(i)];
    }

    // False eye if opponent's stones + outer boundary >= 2.
    int wedge_cnt = diag_cnt[int(pl==0) + 2] + int(diag_cnt[1] > 0);

    if (wedge_cnt == 2) {
        forEach4Diag(v, v_diag, {
            if (color[v_diag] == (int(pl==0) + 2)) {

                // Not false eye if an opponent's stone can be taken immediately.
                if (ren[ren_idx[v_diag]].IsAtari()) return false;

            }
        });
    }

    return wedge_cnt >= 2;

}

/**
 *  Return whether v is Seki.
 */
bool BoardSimple::IsSeki(int v) const {

    assert(color[v] == 0);

    // Return false when empty vertexes are more than 2 or
    // both stones are not in neighboring positions.
    if (!ptn[v].IsPreAtari()    ||
        ptn[v].EmptyCnt() > 1   ||
        ptn[v].StoneCnt(0) == 0 ||
        ptn[v].StoneCnt(1) == 0) 
    {
        return false;
    }

    int64 lib_bits_tmp[6] = {0,0,0,0,0,0};
    std::vector<int> nbr_ren_idxs;
    for (int i = 0; i < 4; ++i) {
        int v_nbr = v + VSHIFT[i];    // neighboring position

        // when white or black stone
        if (color[v_nbr] > 1) {

            // Return false when the liberty number is not 2 or the size if 1.
            if (!ptn[v].IsPreAtari(i)) return false;
            else if (ren[ren_idx[v_nbr]].size == 1 &&
                    ptn[v].StoneCnt(color[v_nbr] - 2) == 1) {
                for (int j = 0; j < 4; ++j) {
                    int v_nbr2 = v_nbr + VSHIFT[j];
                    if (v_nbr2 != v && color[v_nbr2] == 0) {
                        int nbr_cnt = ptn[v_nbr2].StoneCnt(color[v_nbr] - 2);
                        if (nbr_cnt == 1) return false;
                    }
                }
            }

            nbr_ren_idxs.push_back(ren_idx[v_nbr]);
        }
        else if (color[v_nbr] == 0) {
            lib_bits_tmp[etor[v_nbr]/64] |= (0x1ULL << (etor[v_nbr] % 64));
        }
    }

    //int64 lib_bits_tmp[6] = {0,0,0,0,0,0};
    for (auto nbr_idx: nbr_ren_idxs) {
        for (int i = 0; i < 6; ++i) {
            lib_bits_tmp[i] |= ren[nbr_idx].lib_bits[i];
        }
    }

    int lib_cnt = 0;
    for (auto lbt: lib_bits_tmp) {
        if (lbt != 0) {
            lib_cnt += (int)popcnt64(lbt);
        }
    }

    if (lib_cnt == 2) {

        // Check whether it is Self-Atari of Nakade.
        for (int i = 0; i < 6; ++i) {
            while (lib_bits_tmp[i] != 0) {
                int ntz = NTZ(lib_bits_tmp[i]);
                int v_seki = rtoe[ntz + i * 64];
                if (IsSelfAtariNakade(v_seki)) return false;

                lib_bits_tmp[i] ^= (0x1ULL << ntz);
            }
        }
        return true;
    }
    else if (lib_cnt == 3) {

        // Check whether Seki where both Rens have an eye.
        int eye_cnt = 0;
        for (int i = 0; i < 6; ++i) {
            while (lib_bits_tmp[i] != 0) {
                int ntz = NTZ(lib_bits_tmp[i]);
                int v_seki = rtoe[ntz + i * 64];
                if (IsEyeShape(0, v_seki) || IsEyeShape(1, v_seki)) ++eye_cnt;
                if (eye_cnt >= 2 || IsFalseEye(v_seki)) return true;

                lib_bits_tmp[i] ^= (0x1ULL << ntz);
            }
        }
    }

    return false;

}


/**
 *  Set Atari of the Ren including v.
 */
inline void BoardSimple::SetAtari(int v) {

    assert(color[v] > 1);

    int v_atari = ren[ren_idx[v]].lib_atr;

    if (!ptn_ch[v_atari]) {
        diff[diff_cnt-1].ptn.push_back(std::make_pair(v_atari, ptn[v_atari].bf));
        ptn_ch[v_atari] = true;
    }

    ptn[v_atari].SetAtari(
        ren_idx[v_atari + EBSIZE]    ==     ren_idx[v],
        ren_idx[v_atari + 1]         ==     ren_idx[v],
        ren_idx[v_atari - EBSIZE]    ==     ren_idx[v],
        ren_idx[v_atari - 1]         ==     ren_idx[v]
    );

}

/**
 *  Set pre-Atari of the Ren including v.
 */
inline void BoardSimple::SetPreAtari(int v) {

    assert(color[v] > 1);

    int64 lib_bit = 0;
    for (int i = 0; i < 6; ++i) {
        lib_bit = ren[ren_idx[v]].lib_bits[i];
        while (lib_bit != 0) {
            int ntz = NTZ(lib_bit);
            int v_patr = rtoe[ntz + i * 64];

            if (!ptn_ch[v_patr]) {
                diff[diff_cnt-1].ptn.push_back(std::make_pair(v_patr, ptn[v_patr].bf));
                ptn_ch[v_patr] = true;
            }

            ptn[v_patr].SetPreAtari(
                ren_idx[v_patr + EBSIZE]    ==     ren_idx[v],
                ren_idx[v_patr + 1]         ==     ren_idx[v],
                ren_idx[v_patr - EBSIZE]    ==     ren_idx[v],
                ren_idx[v_patr - 1]         ==     ren_idx[v]
            );

            lib_bit ^= (0x1ULL << ntz);
        }
    }

}

/**
 *  Cancel Atari of the Ren including v.
 */
inline void BoardSimple::CancelAtari(int v) {

    assert(color[v] > 1);

    int v_atari = ren[ren_idx[v]].lib_atr;

    if (!ptn_ch[v_atari]) {
        diff[diff_cnt-1].ptn.push_back(std::make_pair(v_atari, ptn[v_atari].bf));
        ptn_ch[v_atari] = true;
    }

    ptn[v_atari].CancelAtari(
        ren_idx[v_atari + EBSIZE]     ==     ren_idx[v],
        ren_idx[v_atari + 1]          ==     ren_idx[v],
        ren_idx[v_atari - EBSIZE]     ==     ren_idx[v],
        ren_idx[v_atari - 1]          ==     ren_idx[v]
    );

}

/**
 *  Cancel pre-Atari of the Ren including v.
 */
inline void BoardSimple::CancelPreAtari(int v) {

    assert(color[v] > 1);

    int64 lib_bit = 0;
    for (int i = 0; i < 6; ++i) {
        lib_bit = ren[ren_idx[v]].lib_bits[i];
        while (lib_bit != 0) {
            int ntz = NTZ(lib_bit);
            int v_patr = rtoe[ntz + i * 64];

            if (!ptn_ch[v_patr]) {
                diff[diff_cnt-1].ptn.push_back(std::make_pair(v_patr, ptn[v_patr].bf));
                ptn_ch[v_patr] = true;
            }

            ptn[v_patr].CancelPreAtari(
                ren_idx[v_patr + EBSIZE]     ==     ren_idx[v],
                ren_idx[v_patr + 1]         ==     ren_idx[v],
                ren_idx[v_patr - EBSIZE]     ==     ren_idx[v],
                ren_idx[v_patr - 1]         ==     ren_idx[v]
            );

            lib_bit ^= (0x1ULL << ntz);
        }
    }

}


/**
 *  Place a stone on position v.
 */
inline void BoardSimple::PlaceStone(int v) {

    assert(color[v] == 0);

    // 1. Update 3x3 patterns around v.
    diff[diff_cnt-1].color.push_back(v);
    color[v] = my + 2;

    forEach8Nbr(v, v_nbr8, d_nbr, d_opp, {
        if (!ptn_ch[v_nbr8]) {
            diff[diff_cnt-1].ptn.push_back(std::make_pair(v_nbr8, ptn[v_nbr8].bf));
            ptn_ch[v_nbr8] = true;
        }
        ptn[v_nbr8].SetColor(d_opp, color[v]);
    });

    // 2. Update stone number and probability at v.
    ++stone_cnt[my];
    --empty_cnt;
    if (!empty_idx_ch[empty[empty_cnt]]) {
        diff[diff_cnt-1].empty_idx.push_back(std::make_pair(empty[empty_cnt], empty_idx[empty[empty_cnt]]));
        empty_idx_ch[empty[empty_cnt]] = true;
    }
    empty_idx[empty[empty_cnt]] = empty_idx[v];
    if (!empty_ch[empty_idx[v]]) {
        diff[diff_cnt-1].empty.push_back(std::make_pair(empty_idx[v], empty[empty_idx[v]]));
        empty_ch[empty_idx[v]] = true;
    }
    empty[empty_idx[v]] = empty[empty_cnt];

    // 3. Update Ren index including v.
    if (!ren_idx_ch[v]) {
        diff[diff_cnt-1].ren_idx.push_back(std::make_pair(v, ren_idx[v]));
        ren_idx_ch[v] = true;
    }
    ren_idx[v] = v;
    if (!ren_ch[ren_idx[v]]) {
        diff[diff_cnt-1].ren.push_back(std::make_pair(ren_idx[v], ren[ren_idx[v]]));
        ren_ch[ren_idx[v]] = true;
    }
    ren[ren_idx[v]].Clear();

    // 4. Update liberty on neighboring positions.
    forEach4Nbr(v, v_nbr, {
        // Add liberty when v_nbr is empty.
        if (color[v_nbr] == 0) {
            ren[ren_idx[v]].AddLib(v_nbr);
        }
        // Subtract liberty in other cases.
        else {
            if (!ren_ch[ren_idx[v_nbr]]) {
                diff[diff_cnt-1].ren.push_back(std::make_pair(ren_idx[v_nbr], ren[ren_idx[v_nbr]]));
                ren_ch[ren_idx[v_nbr]] = true;
            }
            ren[ren_idx[v_nbr]].SubLib(v);
        }
    });

}

/**
 *  Remove the stone on the position v.
 */
inline void BoardSimple::RemoveStone(int v) {

    assert(color[v] > 1);

    // 1. Update 3x3 patterns around v.
    diff[diff_cnt-1].color.push_back(v);
    color[v] = 0;
    if (!ptn_ch[v]) {
        diff[diff_cnt-1].ptn.push_back(std::make_pair(v, ptn[v].bf));
        ptn_ch[v] = true;
    }
    ptn[v].ClearAtari();
    ptn[v].ClearPreAtari();
    forEach8Nbr(v, v_nbr8, d_nbr, d_opp, {
        if (!ptn_ch[v_nbr8]) {
            diff[diff_cnt-1].ptn.push_back(std::make_pair(v_nbr8, ptn[v_nbr8].bf));
            ptn_ch[v_nbr8] = true;
        }
        ptn[v_nbr8].SetColor(d_opp, 0);
    });

    // 2. Update stone number and probability at v.
    --stone_cnt[her];
    if (!empty_idx_ch[v]) {
        diff[diff_cnt-1].empty_idx.push_back(std::make_pair(v, empty_idx[v]));
        empty_idx_ch[v] = true;
    }
    empty_idx[v] = empty_cnt;
    if (!empty_ch[empty_cnt]) {
        diff[diff_cnt-1].empty.push_back(std::make_pair(empty_cnt, empty[empty_cnt]));
        empty_ch[empty_cnt] = true;
    }
    empty[empty_cnt] = v;
    ++empty_cnt;
    if (!ren_idx_ch[v]) {
        diff[diff_cnt-1].ren_idx.push_back(std::make_pair(v, ren_idx[v]));
        ren_idx_ch[v] = true;
    }
    ren_idx[v] = v;

}

/**
 *  Merge the Rens including v_base and v_add.
 *  Replace index of the Ren including v_add.
 */
inline void BoardSimple::MergeRen(int v_base, int v_add) {

    // 1. Merge of Ren class.
    if (!ren_ch[ren_idx[v_base]]) {
        diff[diff_cnt-1].ren.push_back(std::make_pair(ren_idx[v_base], ren[ren_idx[v_base]]));
        ren_ch[ren_idx[v_base]] = true;
    }
    if (!ren_ch[ren_idx[v_add]]) {
        diff[diff_cnt-1].ren.push_back(std::make_pair(ren_idx[v_add], ren[ren_idx[v_add]]));
        ren_ch[ren_idx[v_add]] = true;
    }
    ren[ren_idx[v_base]].Merge(ren[ren_idx[v_add]]);

    // 2. Replace ren_idx of the Ren including v_add.
    int v_tmp = v_add;
    do {
        if (!ren_idx_ch[v_tmp]) {
            diff[diff_cnt-1].ren_idx.push_back(std::make_pair(v_tmp, ren_idx[v_tmp]));
            ren_idx_ch[v_tmp] = true;
        }
        ren_idx[v_tmp] = ren_idx[v_base];
        v_tmp = next_ren_v[v_tmp];
    } while (v_tmp != v_add);

    // 3. Swap positions of next_ren_v.
    //
    //    (before)
    //    v_base: 0->1->2->3->0
    //    v_add : 4->5->6->4
    //    (after)
    //    v_base: 0->5->6->4->1->2->3->0
    if (!next_ren_v_ch[v_base]) {
        diff[diff_cnt-1].next_ren_v.push_back(std::make_pair(v_base, next_ren_v[v_base]));
        next_ren_v_ch[v_base] = true;
    }
    if (!next_ren_v_ch[v_add]) {
        diff[diff_cnt-1].next_ren_v.push_back(std::make_pair(v_add, next_ren_v[v_add]));
        next_ren_v_ch[v_add] = true;
    }
    std::swap(next_ren_v[v_base], next_ren_v[v_add]);

}

/**
 *  Remove the Ren including v.
 */
inline void BoardSimple::RemoveRen(int v) {

    // 1. Remove all stones of the Ren.
    int v_tmp = v;
    do {
        RemoveStone(v_tmp);
        v_tmp = next_ren_v[v_tmp];
    } while (v_tmp != v);

    // 2. Update liberty of neighboring Rens.
    std::vector<int> may_patr_list;     
    do {
        forEach4Nbr(v_tmp, v_nbr, {
            if (color[v_nbr] >= 2) {

                // Cancel Atari or pre-Atari because liberty positions are added.
                // Final status of Atari or pre-Atari will be calculated in PlayLegal().
                if (ren[ren_idx[v_nbr]].IsAtari()) {
                    CancelAtari(v_nbr);
                    may_patr_list.push_back(ren_idx[v_nbr]);
                }
                else if (ren[ren_idx[v_nbr]].IsPreAtari()) CancelPreAtari(v_nbr);

            }
            if (!ren_ch[ren_idx[v_nbr]]) {
                diff[diff_cnt-1].ren.push_back(std::make_pair(ren_idx[v_nbr], ren[ren_idx[v_nbr]]));
                ren_ch[ren_idx[v_nbr]] = true;
            }
            ren[ren_idx[v_nbr]].AddLib(v_tmp);
        });

        int v_next = next_ren_v[v_tmp];
        if (!next_ren_v_ch[v_tmp]) {
            diff[diff_cnt-1].next_ren_v.push_back(std::make_pair(v_tmp, next_ren_v[v_tmp]));
            next_ren_v_ch[v_tmp] = true;
        }
        next_ren_v[v_tmp] = v_tmp;
        v_tmp = v_next;
    } while (v_tmp != v);

    // 3. Update liberty of Neighboring Rens.

    // Remove duplicated indexes.
    sort(may_patr_list.begin(), may_patr_list.end());
    may_patr_list.erase(unique(may_patr_list.begin(),may_patr_list.end()),may_patr_list.end());
    for (auto mpl_idx : may_patr_list) {

        // Update ptn[] when liberty number is 2.
        if (ren[mpl_idx].IsPreAtari()) {
            SetPreAtari(mpl_idx);
        }

    }

}

/**
 *  Returns whether move on position v is self-Atari and forms Nakade.
 *  Used for checking whether Hourikomi is effective in Seki.
 */
inline bool BoardSimple::IsSelfAtariNakade(int v) const {

    // Check whether it will be Nakade shape when size of urrounding Ren is 2~4.
    std::vector<int> checked_idx[2];
        int64 space_hash[2] = {zobrist.hash[0][0][EBVCNT/2], zobrist.hash[0][0][EBVCNT/2]};
        bool under5[2] = {true, true};
        int64 lib_bits[2][6] = {{0,0,0,0,0,0}, {0,0,0,0,0,0}};

        forEach4Nbr(v,v_nbr,{
            if (color[v_nbr] >= 2) {
                int pl = color[v_nbr] - 2;
                if (ren[ren_idx[v_nbr]].size < 5) {
                    if (find(checked_idx[pl].begin(), checked_idx[pl].end(), ren_idx[v_nbr])
                        == checked_idx[pl].end())
                    {
                        checked_idx[pl].push_back(ren_idx[v_nbr]);

                        for (int i = 0; i < 6; ++i)
                            lib_bits[pl][i] |= ren[ren_idx[v_nbr]].lib_bits[i];

                        int v_tmp = v_nbr;
                        do {
                            // Calculate Zobrist Hash relative to the center position.
                            space_hash[pl] ^= zobrist.hash[0][0][v_tmp - v + EBVCNT/2];
                            forEach4Nbr(v_tmp, v_nbr2, {
                                if (color[v_nbr2] == int(pl == 0) + 2) {
                                    if (ren[ren_idx[v_nbr2]].lib_cnt != 2) {
                                        under5[pl] = false;
                                        break;
                                    } else {
                                        for (int i = 0; i < 6; ++i)
                                            lib_bits[pl][i] |= ren[ren_idx[v_nbr2]].lib_bits[i];
                                    }
                                }
                            });

                            v_tmp = next_ren_v[v_tmp];
                        } while (v_tmp != v_nbr);
                    }
                }
                else under5[pl] = false;
            }
        });

        if (under5[0] && nakade.vital.find(space_hash[0]) != nakade.vital.end())
        {
            int lib_cnt = 0;
            for (auto lb:lib_bits[0]) lib_cnt += (int)popcnt64(lb);
            if (lib_cnt == 2) return true;
        }
        else if (under5[1] && nakade.vital.find(space_hash[1]) != nakade.vital.end())
        {
            int lib_cnt = 0;
            for (auto lb:lib_bits[1]) lib_cnt += (int)popcnt64(lb);
            if (lib_cnt == 2) return true;
        }

        return false;

}

/**
 *  Update the board with the move on position v.
 *  It is necessary to confirm in advance whether the move is legal.
 */
void BoardSimple::PlayLegal(int v) {

    assert(v <= PASS);
    assert(v == PASS || color[v] == 0);
    assert(v != ko);

    // 1. Initialize difference information.
    memset(&color_ch, false, sizeof(color_ch));
    memset(&empty_ch, false, sizeof(empty_ch));
    memset(&empty_idx_ch, false, sizeof(empty_idx_ch));
    memset(&ren_ch, false, sizeof(ren_ch));
    memset(&next_ren_v_ch, false, sizeof(next_ren_v_ch));
    memset(&ren_idx_ch, false, sizeof(ren_idx_ch));
    memset(&ptn_ch, false, sizeof(ptn_ch));
    Diff df;
    df.ko = ko;
    diff.push_back(df);
    ++diff_cnt;

    // 2. Update history.
    int prev_empty_cnt = empty_cnt;
    bool is_in_eye = ptn[v].IsEnclosed(her);
    ko = VNULL;
    move_history.push_back(v);
    ++move_cnt;

    if (v == PASS) {
        // Exchange the turn indexes.
        my = int(my == 0);
        her = int(her == 0);
        return;
    }

    // 3. Place a stone.
    PlaceStone(v);

    // 4. Merge the stone with other Rens.
    int my_color = my + 2;

    forEach4Nbr(v, v_nbr1, {

        // a. When v_nbr1 is my stone color and another Ren.
        if (color[v_nbr1] == my_color && ren_idx[v_nbr1] != ren_idx[v]) {

            // b. Cancel pre-Atari when it becomes in Atari.
            if (ren[ren_idx[v_nbr1]].lib_cnt == 1) CancelPreAtari(v_nbr1);

            // c. Merge them with the larger size of Ren as the base.
            if (ren[ren_idx[v]].size > ren[ren_idx[v_nbr1]].size) {
                MergeRen(v, v_nbr1);
            }
            else MergeRen(v_nbr1, v);

        }

    });

    // 5. Reduce liberty of opponent's stones.
    int her_color = int(my == 0) + 2;

    forEach4Nbr(v, v_nbr2, {

        // If an opponent stone.
        if (color[v_nbr2] == her_color) {
            switch(ren[ren_idx[v_nbr2]].lib_cnt)
            {
            case 0:
                RemoveRen(v_nbr2);
                break;
            case 1:
                SetAtari(v_nbr2);
                break;
            case 2:
                SetPreAtari(v_nbr2);
                break;
            default:
                break;
            }
        }

    });

    // 6. Update Ko.
    if (is_in_eye && prev_empty_cnt == empty_cnt) {
        ko = empty[empty_cnt - 1];
    }

    // 7. Update Atari/pre-Atari of the Ren including v.
    switch(ren[ren_idx[v]].lib_cnt) 
    {
    case 1:
        SetAtari(v);
        break;
    case 2:
        SetPreAtari(v);
        break;
    default:
        break;
    }

    // 8. Exchange the turn indexes.
    my = int(my == 0);
    her = int(her == 0);

}

/**
 *  Return the board phase to the previous state.
 */
void BoardSimple::Undo() {

    Diff* df = &diff[diff_cnt-1];
    for (auto& v: df->color) {
        if (color[v] == 0) {
            // Position where the stone was removed.
            color[v] = my + 2;
            --empty_cnt;
            ++stone_cnt[my];
        } else {
            // Position where the stone was placed.
            color[v] = 0;
            ++empty_cnt;
            --stone_cnt[her];
        }
    }
    ko = df->ko;

    for (auto& em: df->empty) {
        empty[em.first] = em.second;
    }
    for (auto& ei: df->empty_idx) {
        empty_idx[ei.first] = ei.second;
    }
    for (auto& r: df->ren) {
        ren[r.first] = r.second;
    }
    for (auto& nrv: df->next_ren_v) {
        next_ren_v[nrv.first] = nrv.second;
    }
    for (auto& ri: df->ren_idx) {
        ren_idx[ri.first] = ri.second;
    }
    for (auto& pt: df->ptn) {
        ptn[pt.first].bf = pt.second;
    }

    my = int(my == 0);
    her = int(her == 0);
    move_history.pop_back();
    --move_cnt;

    diff.pop_back();
    --diff_cnt;

}


#undef forEach4Nbr
#undef forEach4Diag
#undef forEach8Nbr
