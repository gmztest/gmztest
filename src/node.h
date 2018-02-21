#pragma once

#include <atomic>
#include <memory>

#include "board.h"
#include "feed_tensor.h"


/**************************************************************
 *
 *  Class of branch leading to child node,
 *  which contains number of rollout execution and wins etc.
 *
 *  The parent node has all legal boards that it derives as
 *  branches to child nodes.
 *  To avoid false recognition due to confluence, the move is
 *  selected by the winning rate of the child branches.
 *
 ***************************************************************/
class Child {

public:
    std::atomic<int> move;                // Move to the child board.
    std::atomic<int> rollout_cnt;         // Number of rollout execution.
    std::atomic<int> value_cnt;           // Number of board evaluation.
    std::atomic<double> rollout_win;      // Sum of rollout wins.
    std::atomic<double> value_win;        // Sum of evaluation value.
    std::atomic<double> prob;             // Probability of the move.
    std::atomic<bool> is_next;            // Whether the child node exists.
    std::atomic<int> next_idx;            // Index of the child node.
    std::atomic<int64> next_hash;         // Board hash of the child node.
    std::atomic<double> value;            // Evaluation value of the child board.
    std::atomic<bool> is_value_eval;      // Whether the child board has been evaluated.

    Child() {
        move = PASS;
        rollout_cnt = 0;
        value_cnt = 0;
        rollout_win = 0.0;
        value_win = 0.0;
        prob = 0.0;
        is_next = false;
        next_idx = 0;
        next_hash = 0;
        value = 0.0;
        is_value_eval = false;
    }

    Child(const Child& other) { *this = other; }

    Child& operator=(const Child& rhs) {
        move.store(rhs.move.load());
        rollout_cnt.store(rhs.rollout_cnt.load());
        value_cnt.store(rhs.value_cnt.load());
        rollout_win.store(rhs.rollout_win.load());
        value_win.store(rhs.value_win.load());
        prob.store(rhs.prob.load());
        is_next.store(rhs.is_next.load());
        next_idx.store(rhs.next_idx.load());
        next_hash.store(rhs.next_hash.load());
        value.store(rhs.value.load());
        is_value_eval.store(rhs.is_value_eval.load());
        return *this;
    }

    bool operator<(const Child& rhs) const { return prob < rhs.prob; }
    bool operator>(const Child& rhs) const { return prob > rhs.prob; }

};


/**************************************************************
 *
 *  Class of nodes of the search tree,
 *  which contains child branches and probability distribution. 
 *  Each node is managed in the transposition table.
 *
 ***************************************************************/
class Node {

public:
    std::atomic<int> pl;                         // Turn index.
    std::atomic<int> move_cnt;                   // Move count.
    std::atomic<int> child_cnt;                  // Number of child branches.
    Child children[BVCNT+1];                     // Array of child branches.
    std::atomic<double> prob[EBVCNT];            // Probability with the policy network.
    std::atomic<double> prob_roll[2][EBVCNT];    // Probability for rollout.
    std::atomic<int> prob_order[BVCNT + 1];      // Ordered index of the children.
    std::atomic<int> total_game_cnt;             // Total number of search.
    std::atomic<int> rollout_cnt;                // Number of rollout execution.
    std::atomic<int> value_cnt;                  // Number of board evaluation.
    std::atomic<double> rollout_win;             // Sum of rollout wins.
    std::atomic<double> value_win;               // Sum of evaluation value.
    std::atomic<int64> hash;                     // Board hash of the node.
    std::atomic<int> prev_ptn[2];                // 12-point pattern of previous and 2 moves before moves.
    std::atomic<int> prev_move[2];               // Previous and 2 moves before moves.
    std::atomic<bool> is_visit;                  // Whether this node is searched under the current root node.
    std::atomic<bool> is_creating;               // Whether this node is creating.
    std::atomic<bool> is_policy_eval;            // Whether probability has been evaluated by the policy network.

    Node() { Node::Clear(); }
    Node(const Node& other) { *this = other; }

    Node& operator=(const Node& rhs) {
        pl.store(rhs.pl.load());
        move_cnt.store(rhs.move_cnt.load());
        child_cnt.store(rhs.child_cnt.load());
        for (int i = 0; i < child_cnt; ++i) children[i] = rhs.children[i];
        for (int i = 0; i < EBVCNT; ++i) prob[i].store(rhs.prob[i].load());
        for (int i = 0; i < EBVCNT; ++i) prob_roll[0][i].store(rhs.prob_roll[0][i].load());
        for (int i = 0; i < EBVCNT; ++i) prob_roll[1][i].store(rhs.prob_roll[1][i].load());
        for (int i = 0; i < BVCNT+1; ++i) prob_order[i].store(rhs.prob_order[i].load());
        total_game_cnt.store(rhs.total_game_cnt.load());
        rollout_cnt.store(rhs.rollout_cnt.load());
        value_cnt.store(rhs.value_cnt.load());
        rollout_win.store(rhs.rollout_win.load());
        value_win.store(rhs.value_win.load());
        hash.store(rhs.hash.load());
        for (int i = 0; i < 2; ++i) prev_ptn[i].store(rhs.prev_ptn[i].load());
        for (int i = 0; i < 2; ++i) prev_move[i].store(rhs.prev_move[i].load());
        is_visit.store(rhs.is_visit.load());
        is_creating.store(rhs.is_creating.load());
        is_policy_eval.store(rhs.is_policy_eval.load());
        return *this;
    }

    void Clear() {
        pl = 0;
        move_cnt = 0;
        child_cnt = 0;
        for (auto& i:prob) i = 0.0;
        for (auto& i:prob_roll[0]) i = 0.0;
        for (auto& i:prob_roll[1]) i = 0.0;
        for (int i = 0; i < BVCNT+1; ++i) prob_order[i] = i;
        total_game_cnt = 0;
        rollout_cnt = 0;
        value_cnt = 0;
        rollout_win = 0.0;
        value_win = 0.0;
        hash = 0;
        for (auto& i:prev_ptn) i = 0xffffffff;
        for (auto& i:prev_move) i = VNULL;
        is_visit = false;
        is_creating = false;
        is_policy_eval = false;
    }

};

struct PolicyEntry {
    int node_idx;
    FeedTensor ft;

    PolicyEntry() { node_idx = 0; }
};

struct ValueEntry {
    int node_idx[128];
    int child_idx[128];
    int depth;
    int request_cnt;
    FeedTensor ft;

    ValueEntry() { depth = 0; request_cnt = 1; }

    bool operator==(const ValueEntry &rhs) const {
        if (depth != rhs.depth) return false;
        if (memcmp(node_idx, rhs.node_idx, depth * 4) == 0 &&
            memcmp(child_idx, rhs.child_idx, depth * 4) == 0) return true;
        return false;
    }
};
