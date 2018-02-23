#include <algorithm>
#include <thread>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <fstream>

#include "search.h"

using std::string;
using std::cerr;
using std::endl;


double cfg_main_time = 0;
double cfg_byoyomi = 5;
double cfg_emer_time = 15;
int cfg_thread_cnt = 4;
int cfg_gpu_cnt = 1;
double cfg_komi = 7.5;

bool self_match = false;
bool is_master = false;
bool is_worker = false;
int cfg_worker_cnt = 1;
bool cfg_mimic = false;
bool never_resign = false;
int cfg_sym_idx = 8;
bool cfg_rollout = true;
bool cfg_debug = false;
std::string resume_sgf_path = "";
std::string cfg_weightsfile = "";

#ifdef _WIN32
    std::string spl_str = "\\";
    #undef max
    #undef min
#else
    std::string spl_str = "/";
#endif

/**
 *  Repeat weak CAS for floating-type addition.
 */
template<typename T>
T FetchAdd(std::atomic<T> *obj, T arg) {
    T expected = obj->load();
    while (!atomic_compare_exchange_weak(obj, &expected, expected + arg))
        ;
    return expected;
};


Tree::Tree() {

    Tree::Clear();

}


void Tree::Clear() {

#ifdef CPU_ONLY
    thread_cnt = 2;
    gpu_cnt = 1;
    expand_cnt = 64;
#else
    thread_cnt = (cfg_thread_cnt > cfg_gpu_cnt) ? cfg_thread_cnt : cfg_gpu_cnt + 1;
    gpu_cnt = cfg_gpu_cnt;
    expand_cnt = 12;
#endif // CPU_ONLY

    main_time = cfg_main_time;
    byoyomi = cfg_byoyomi;
    komi = cfg_komi;
    sym_idx = cfg_sym_idx;

    vloss_cnt = 3;
    if (cfg_rollout) {
        lambda = 0.7;
    } else {
        lambda = 1.0;
    }
    cp = 3.0;
    policy_temp = 0.7;

    Tree::InitBoard();

    node.clear();
    node.resize(node_limit);

    log_file = NULL;
    use_dirichlet_noise = false;
    stop_think = false;

#if 1
    std::ifstream fin;
    std::stringstream ss;
    ss << "opening_book.bin";
    fin.open(ss.str(), std::ios::in | std::ios::binary);

    if (!fin.fail()) {
        int ob_cnt = 0;
        fin.read((char*)&ob_cnt, sizeof(int));
        int64 bh; int nv;
        for (int i = 0; i < ob_cnt; ++i) {
            int mv_cnt = 0;
            fin.read((char*)&mv_cnt, sizeof(int));
            fin.read((char*)&bh, sizeof(int64));
            for (int j = 0; j < mv_cnt; ++j) {
                fin.read((char*)&nv, sizeof(int));
                book[bh].insert(nv);
            }
        }
    }
    fin.close();  
#endif
}


void Tree::InitBoard() {

    for (auto& nd: node) nd.Clear();
    node_hash_list.clear();
    node_cnt = 0;
    node_depth = 0;
    root_node_idx = 0;
    value_que.clear();
    policy_que.clear();
    value_que_cnt = 0;
    policy_que_cnt = 0;
    eval_policy_cnt = 0;
    eval_value_cnt = 0;

    lgr.Clear();
    stat.Clear();

    left_time = main_time;
    extension_cnt = 0;
    move_cnt = 0;
    node_move_cnt = 0;

}


/**
 *  Add new entry to policy_que.
 */
void Tree::AddPolicyQue(int node_idx, Board& b) {

    PolicyEntry pe;
    pe.node_idx = node_idx;
    pe.ft.Set(b, PASS);
    {
        std::lock_guard<std::mutex> lock(mtx_pque);
        policy_que.push_back(pe);
        ++policy_que_cnt;
    }

}


/**
 *  Add new entry to value_que.
 */
void Tree::AddValueQue(std::vector<std::pair<int,int>>& upper_list, Board& b) {

    ValueEntry ve;
    ve.depth = std::min(128, (int)upper_list.size());
    auto ritr = upper_list.rbegin();
    for (int i = 0, n = ve.depth; i < n; ++i) {
        ve.node_idx[i] = ritr->first;
        ve.child_idx[i] = ritr->second;
        ++ritr;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_vque);
        auto itr = find(value_que.begin(), value_que.end(), ve);
        if (itr != value_que.end()) {
            itr->request_cnt++;
            return;
        }
    }

    ve.ft.Set(b, PASS);

    {
        std::lock_guard<std::mutex> lock(mtx_vque);
        auto itr = find(value_que.begin(), value_que.end(), ve);
        if (itr == value_que.end()) {
            value_que.push_back(ve);
            ++value_que_cnt;
        } else {
            itr->request_cnt++;
        }
    }

}


/**
 *  Create a new Node and returns the index.
 */
int Tree::CreateNode(Board& b) {

    // Calculate board hash.
    int64 hash_b = BoardHash(b);
    int node_idx;

    {
        Node *pn;
        {
            std::lock_guard<std::mutex> lock(mtx_node);

            if (node_hash_list.find(hash_b) != node_hash_list.end()) {

                // Return -1 if another thread is creating this node.
                if (node[node_hash_list[hash_b]].is_creating) return -1;

                // Return the index if the key is already registered.
                else {
                    // Confirm whether the board hashes are the same.
                    if (node[node_hash_list[hash_b]].hash == hash_b &&
                        node[node_hash_list[hash_b]].move_cnt == b.move_cnt)
                    {
                        return node_hash_list[hash_b];
                    }
                }
            }

            node_idx = int(hash_b % (int64)node_limit);
            node_idx = std::max(0, std::min(node_idx, node_limit - 1));
            pn = &node[node_idx];

            // Update node_idx if another node is registered or been creating.
            while (pn->child_cnt != 0 || pn->is_creating) {
                ++node_idx;
                if (node_idx >= node_limit) node_idx = 0;
                pn = &node[node_idx];
            }

            node_hash_list[hash_b] = node_idx;
            pn->is_creating = true;
            ++node_cnt;
        }

        pn->child_cnt = 0;
        for (int i = 0; i < BVCNT+1; ++i) pn->prob_order[i] = i;
        pn->total_game_cnt = 0;
        pn->rollout_cnt = 0;
        pn->value_cnt = 0;
        pn->rollout_win = 0.0;
        pn->value_win = 0.0;
        pn->is_visit = false;
        pn->is_policy_eval = false;
        pn->hash = hash_b;
        pn->pl = b.my;
        pn->move_cnt = b.move_cnt;
        pn->prev_move[0] = b.prev_move[b.her];
        pn->prev_move[1] = b.prev_move[b.my];

        pn->prev_ptn[0] = b.prev_ptn[0].bf;
        pn->prev_ptn[1] = b.prev_ptn[1].bf;

        int my = b.my;
        int her = b.her;
        double sum_prob = 0.0;

        int prev_move_[3] = {b.prev_move[her], b.prev_move[my], PASS};
        if (b.move_cnt >= 3) prev_move_[2] = b.move_history[b.move_cnt - 3];

        for (int i = 0; i < EBVCNT; ++i) {
            pn->prob_roll[my][i] = 0;
            pn->prob_roll[her][i] = 0;
            pn->prob[i] = 0;
        }

        for (int i = 0, n = b.empty_cnt; i < n; ++i) {
            int v = b.empty[i];

            double my_dp = 1.0;
            double her_dp = 1.0;
            my_dp *= prob_dist[0][DistBetween(v, prev_move_[0])][0];
            my_dp *= prob_dist[1][DistBetween(v, prev_move_[1])][0];
            her_dp *= prob_dist[0][DistBetween(v, prev_move_[1])][0];
            her_dp *= prob_dist[1][DistBetween(v, prev_move_[2])][0];
            Pattern3x3 ptn12 = b.ptn[v];
            ptn12.SetColor(8, b.color[std::min(EBVCNT - 1, (v + EBSIZE * 2))]);
            ptn12.SetColor(9, b.color[v + 2]);
            ptn12.SetColor(10, b.color[std::max(0, (v - EBSIZE * 2))]);
            ptn12.SetColor(11, b.color[v - 2]);
            if (prob_ptn12.find(ptn12.bf) != prob_ptn12.end()) {
                my_dp *= prob_ptn12[ptn12.bf][my];
                her_dp *= prob_ptn12[ptn12.bf][her];
            }

            double my_prob = b.prob[my][v] * my_dp;
            pn->prob_roll[my][v] = my_prob;
            pn->prob_roll[her][v] = b.prob[her][v] * her_dp;

            if (b.IsLegal(b.my,v)        &&
                !b.IsEyeShape(b.my,v)    &&
                !b.IsSeki(v))
            {
                pn->prob[v] = my_prob;
                sum_prob += my_prob;
            }

            //pn->prob_roll[my][v] = b.prob[my][v];
            //pn->prob_roll[her][v] = b.prob[her][v];

            //sum_prob += b.prob[my][v];
        }

        if (sum_prob != 0) {
            double inv_sum = 0.2 / sum_prob;
            for (int i = 0, n = b.empty_cnt; i < n; ++i) {
                int v = b.empty[i];
                if (pn->prob[v] != 0.0) {
                    pn->prob[v] = (double)pn->prob[v] * inv_sum;
                }
            }
        }

        std::vector<std::pair<double, int>> prob_list;
        for (int i = 0; i < b.empty_cnt; ++i) {
            int v = b.empty[i];
            if (!b.IsLegal(b.my,v)    ||
                b.IsEyeShape(b.my,v)  ||
                b.IsSeki(v))     continue;

            prob_list.push_back(std::make_pair((double)pn->prob[v], v));
        }
        std::sort(prob_list.begin(), prob_list.end(), std::greater<std::pair<double,int>>());

        for (int i = 0, n = (int)prob_list.size(); i < n; ++i) {
            Child new_child;
            new_child.move = prob_list[i].second;
            new_child.prob = prob_list[i].first;

            // Register the child.
            pn->children[i] = new_child;
            pn->child_cnt++;
        }

        // PASS
        Child new_child;
        pn->children[pn->child_cnt] = new_child;
        pn->child_cnt++;

        pn->is_creating = false;
    }

    AddPolicyQue(node_idx, b);

    // Return the Node index.
    return node_idx;

}


/**
 *  Update probability of node with that evaluated
 *  by the policy network.
 */
void Tree::UpdateNodeProb(int node_idx, std::array<double, EBVCNT>& prob_list) {

    // 1. Replace probability of node[node_idx] with prob_list.
    Node* pn = &node[node_idx];
    for (int i = 0; i < BVCNT; ++i) {
        int v = rtoe[i];
        pn->prob[v] = prob_list[v];
    }

    // 2. Update prob_order after sorting.
    int child_cnt = pn->child_cnt.load();
    std::vector<std::pair<double, int>> prob_idx_pair;
    for (int i = 0, n = child_cnt-1; i < n; ++i) {
        pn->children[i].prob = (double)pn->prob[pn->children[i].move];
        prob_idx_pair.push_back(std::make_pair((double)pn->children[i].prob, i));
    }
    prob_idx_pair.push_back(std::make_pair(0.0, child_cnt-1));
    std::sort(prob_idx_pair.begin(), prob_idx_pair.end(), std::greater<std::pair<double, int>>());
    for (int i = 0, n = child_cnt; i < n; ++i) {
        pn->prob_order[i] = prob_idx_pair[i].second;
    }

    // 3. Register LGR move in lgr.policy.
    if (lambda != 1.0) {
        std::array<int,4> lgr_seed = {pn->prev_ptn[0], pn->prev_move[0], pn->prev_ptn[1], pn->prev_move[1]};
        if (pn->children[pn->prob_order[0]].move < PASS)
        {
            std::lock_guard<std::mutex> lock(mtx_lgr);
            lgr.policy[pn->pl][lgr_seed] = pn->children[pn->prob_order[0]].move;
        }
    }
    pn->is_policy_eval = true;
}


/**
 *  Collect all indexes of nodes under node[node_idx].
 */
int Tree::CollectNodeIndex(int node_idx, int depth, std::unordered_set<int>& node_list) {

    int max_depth = depth;
    ++depth;

    node_list.insert(node_idx);
    Node* pn = &node[node_idx];
    pn->is_visit = false;

    if (pn->child_cnt == 0) return max_depth;
    else if (depth > 720)   return max_depth;

    for (int i = 0, n = pn->child_cnt; i < n; ++i) {
        Child* pc = &pn->children[i];
        if (pc->is_next &&
            ((int64)pc->next_hash == (int64)node[(int)pc->next_idx].hash))
        {
            int prev_move = pn->prev_move[0];
            int next_move = pc->move;
            if (!(prev_move == PASS && next_move == PASS) &&
                node_list.find((int)pc->next_idx) == node_list.end())
            {
                // Call recursively if next node exits.
                int tmp_depth = CollectNodeIndex((int)pc->next_idx, depth, node_list);
                if (tmp_depth > max_depth) max_depth = tmp_depth;
            }
        }
    }
    return max_depth;
}


/**
 *  Delete indexes to reduce node usage rate. (30%-60%)
 */
void Tree::DeleteNodeIndex(int node_idx) {

    // 1. Do not delete nodes if node utilization is less than 50%.
    if (node_cnt < 0.5 * node_limit) return;

    // 2. Find indexes connecting to the root node.
    std::unordered_set<int> under_root;
    CollectNodeIndex(node_idx, 0, under_root);

    // 3. Update the oldest move count of the nodes until the node
    //    usage becomes 20% or less.
    std::unordered_set<int> node_list(under_root);

    if ((int)node_list.size() < 0.2 * node_limit) {
        for (int i = 0, n = move_cnt-node_move_cnt; i < n; ++i) {
            ++node_move_cnt;

            for (int j = 0; j < node_limit; ++j) {
                if (node[j].move_cnt >= node_move_cnt) {
                    node_list.insert(j);
                }
            }

            if ((int)node_list.size() < 0.3 * node_limit) {
                break;
            } else {
                node_list.clear();
                node_list = under_root;
            }
        }
    }

    // 4. Delete old node not in node_list.
    {
        std::lock_guard<std::mutex> lock(mtx_node);
        for (int i = 0; i < node_limit; ++i) {
            if (node[i].child_cnt != 0) {
                if (node_list.find(i) == node_list.end()) {
                    node_hash_list.erase((int64)node[i].hash);

                    node[i].Clear();
                    --node_cnt;
                } else { 
                    node[i].is_visit = false;
                }
            }
        }
    }

    // 5. Remove entries from policy_que.
    std::deque<PolicyEntry> remain_pque;
    for (auto i:policy_que) {
        if (node_list.find(i.node_idx) != node_list.end()) {
            remain_pque.push_back(i);
        }
    }
    policy_que.swap(remain_pque);
    policy_que_cnt = (int)policy_que.size();

    // 6. Remove entries from value_que.
    std::deque<ValueEntry> remain_vque;
    for (auto i: value_que) {
        if (node_list.find(i.node_idx[0]) != node_list.end()) {

            bool is_remain = true;

            for (int j = 1, j_max = i.depth-1; j < j_max; ++j) {
                if (node_list.find(i.node_idx[j]) == node_list.end())
                {
                    is_remain = false;
                    break;
                }
            }

            if (is_remain) {
                i.depth = std::max(1, i.depth-1);
                remain_vque.push_back(i);
            }

            //remain_vque.push_back(i);
        }
    }
    value_que.swap(remain_vque);
    value_que_cnt = (int)value_que.size();

    if (node_cnt < 0) node_cnt = 1;
}

/**
 *  Update the root node with the input board.
 */
int Tree::UpdateRootNode(Board&b) {

    move_cnt = b.move_cnt;
    int node_idx = CreateNode(b);

    if (root_node_idx != node_idx) DeleteNodeIndex(node_idx);
    root_node_idx = node_idx;

    return node_idx;

}


/**
 *  Proceed to a child node from the parent node.
 *  Create new node if there is no corresponding child node.
 *  At the leaf node, rollout and evaluation are performed,
 *  and the results are returned.
 */
double Tree::SearchBranch(Board& b, int node_idx, double& value_result,
                          std::vector<std::pair<int,int>>& serch_route, 
                          LGR& lgr_, Statistics& stat_)
{

    Node *pn = &node[node_idx];
    Child *pc;
    bool use_rollout = (lambda != 1.0);

    // 1. Choose the move with the highest action value.
    int max_idx = 0;
    double max_avalue = -128;

    double pn_rollout_rate = (pn->rollout_cnt == 0) ? 0.0 : (double)pn->rollout_win/((double)pn->rollout_cnt);
    double pn_value_rate = (pn->value_cnt == 0) ? 0.0 : (double)pn->value_win/((double)pn->value_cnt);
    double pn_root_game = sqrt((double)pn->total_game_cnt);

    double rollout_cnt, rollout_win, value_cnt, value_win;
    double rollout_rate, value_rate, game_cnt, rate, action_value;


    for (int i = 0, n = (int)pn->child_cnt; i < n; ++i) {

        // a. Search in descending order of probability.
        int child_idx = pn->prob_order[i];
        pc = &pn->children[child_idx];

        rollout_cnt = (double)pc->rollout_cnt;
        value_cnt = (double)pc->value_cnt;
        rollout_win = (double)pc->rollout_win;
        value_win = (double)pc->value_win;

        // b. Calculate winning rate of this move.
        if (rollout_cnt == 0)    rollout_rate = pn_rollout_rate;
        else                     rollout_rate = rollout_win / rollout_cnt;
        if (value_cnt == 0)      value_rate = pn_value_rate;
        else                     value_rate = value_win / value_cnt;

        rate = (1-lambda) * rollout_rate + lambda * value_rate;

        // c. Calculate action value.
        game_cnt = use_rollout ? (double)pc->rollout_cnt : (double)pc->value_cnt;
        action_value = rate + cp * pc->prob * pn_root_game / (1 + game_cnt);

        // d. Update max_idx.
        if (action_value > max_avalue) {
            max_avalue = action_value;
            max_idx = child_idx;
        }

    }

    // 2. Search for the move with the maximum action value.
    pc = &pn->children[max_idx];
    int next_idx = pc->next_idx;
    bool is_next = pc->is_next;
    if (next_idx < 0             ||
        node_limit <= next_idx   ||
        pc->next_hash != node[next_idx].hash)
    {
        next_idx = 0;
        is_next = false;
    }
    Node* npn = &node[next_idx]; // Next pointer of the node.

    serch_route.push_back(std::make_pair(node_idx, max_idx));
    int next_move = pc->move;
    int prev_move = b.prev_move[b.her];
    // Bias of winning rate that corrects result of (0, +/-1) to (-0.5, +0.5).
    double win_bias = (b.my == 0) ? -0.5 : 0.5;

    // 3. Update LGR of policy.
    if (use_rollout && !pn->is_visit && pn->is_policy_eval)
    {
        // Update when searching first after becoming the current root node.
        pn->is_visit = true;
        int max_prob_idx = pn->prob_order[0];

        if (pn->children[max_prob_idx].move < PASS)
        {
            std::array<int,4> lgr_seed = {pn->prev_ptn[0],
                                          pn->prev_move[0],
                                          pn->prev_ptn[1],
                                          pn->prev_move[1]};
            lgr_.policy[pn->pl][lgr_seed] = pn->children[max_prob_idx].move;
        }
    }

    // 4. Check if rollout is necessary.
    bool need_rollout = false;
    int pc_game_cnt = use_rollout? (int)pc->rollout_cnt : (int)pc->value_cnt;
    if ((!is_next && pc_game_cnt < expand_cnt)    ||
        (next_move == PASS && prev_move == PASS)      ||
        (b.move_cnt > 720)                        ||
        (pn->child_cnt <= 1 && pc->is_next && npn->child_cnt <= 1))
    {
        need_rollout = true;
    }

    // 5. Check whether the next node can be expanded.
    bool expand_node = false;
    if (!is_next && !need_rollout)
    {
        // New node is not chreated when the transposition table is filled by 85%.
        if (node_cnt < 0.85 * node_limit /*&& pc->is_value_eval*/) expand_node = true;
        else need_rollout = true;
    }

    // 6. Play next_mvoe.
    b.PlayLegal(next_move);

    // 7. Expand the next node.
    if (expand_node) {
        int next_idx_exp = CreateNode(b);
        if (next_idx_exp < 0 || next_idx_exp >= node_limit) need_rollout = true;
        else {
            npn = &node[next_idx_exp];
            pc->next_idx = next_idx_exp;
            pc->next_hash = (int64)npn->hash;

            //npn->total_game_cnt += use_rollout? (int)pc->rollout_cnt : (int)pc->value_cnt;
            npn->rollout_cnt += (int)pc->rollout_cnt;
            npn->value_cnt += (int)pc->value_cnt;
            // Reverse evaluation value since turn changes.
            FetchAdd(&npn->rollout_win, -(double)pc->rollout_win);
            FetchAdd(&npn->value_win, -(double)pc->value_win);

            pc->is_next = true;
            is_next = true;
        }
    }

    // 8. Add virtual loss.
    if (use_rollout) {
        FetchAdd(&pc->rollout_win, -(double)vloss_cnt);
        pc->rollout_cnt += vloss_cnt;
        pn->total_game_cnt += vloss_cnt;
    } else {
        FetchAdd(&pc->value_win, -(double)vloss_cnt);
        pc->value_cnt += vloss_cnt;
        pn->total_game_cnt += vloss_cnt;
    }

    // 9. Roll out if it is the leaf node, otherwise proceed to the next node.
    double rollout_result = 0.0;
    if (need_rollout)
    {
        // a-1. Add into the queue if the board is not evaluated.
        value_result = 0;
        if (pc->is_value_eval) {
            value_result = (double)pc->value;
        } else {
            AddValueQue(serch_route, b);
        }

        if (use_rollout) {
            // b. Roll out and normalize the result to [-1.0, 1.0].
            rollout_result = -2.0 * ((double)PlayoutLGR(b, lgr_, komi) + win_bias);
        }
    } else {
        // a-2. Proceed to the next node and reverse the results.
        rollout_result = -SearchBranch(b, (int)pc->next_idx, value_result, serch_route, lgr_, stat_);
        value_result *= -1.0;
    }

    // 10. Subtract virtual loss and update results.
    if (use_rollout) {
        FetchAdd(&pc->rollout_win, (double)vloss_cnt + rollout_result);
        pc->rollout_cnt += 1 - vloss_cnt;
        FetchAdd(&pn->rollout_win, rollout_result);
        pn->rollout_cnt += 1;
        pn->total_game_cnt += 1 - vloss_cnt;
    } else {
        FetchAdd(&pc->value_win, (double)vloss_cnt);
        pc->value_cnt += -vloss_cnt;
        pn->total_game_cnt += 1 - vloss_cnt;
    }

    if (value_result != 0) {
        FetchAdd(&pc->value_win, value_result);
        pc->value_cnt += 1;
        FetchAdd(&pn->value_win, value_result);
        pn->value_cnt += 1;
    }

    return rollout_result;

}


void PrintLog(std::ofstream* log_file, const char* output_text, ...) {

    va_list args;
    char buf[1024];

    va_start(args, output_text);
    vsprintf(buf, output_text, args);
    va_end(args);    

    fprintf(stderr, buf);
    if (log_file != NULL) { *log_file << buf; }
}

void Tree::SortChildren(Node* pn, std::vector<Child*>& child_list) {

    std::vector<std::pair<int, int>> game_cnt_list;
    for (int i = 0; i < pn->child_cnt; ++i) {
        int game_cnt = (lambda != 1.0) ? (int)pn->children[i].rollout_cnt : (int)pn->children[i].value_cnt;
        game_cnt_list.push_back(std::make_pair(game_cnt, i));
    }
    std::sort(game_cnt_list.begin(), game_cnt_list.end(), std::greater<std::pair<int, int>>());

    child_list.clear();
    for (int i = 0; i < pn->child_cnt; ++i) {
        Child* pc = &pn->children[game_cnt_list[i].second];
        child_list.push_back(pc);
    }
}

double Tree::BranchRate(Child* pc) {

    double winning_rate;
    if (pc->value_cnt == 0) {
        winning_rate = pc->rollout_win/std::max(1.0, (double)pc->rollout_cnt);
    } else if (pc->rollout_cnt == 0) {
        winning_rate = pc->value_win/std::max(1.0, (double)pc->value_cnt);
    } else {
        winning_rate = (1-lambda)*pc->rollout_win/std::max(1.0, (double)pc->rollout_cnt)
                + lambda*pc->value_win/std::max(1.0, (double)pc->value_cnt);
    }
    winning_rate = (winning_rate + 1) / 2;    // [0.0, 1.0]

    return winning_rate;
}

std::string CoordinateString(int v) {

    string str_v;

    if (v == PASS) str_v = "PASS";
    else if (v < 0 || v > PASS || DistEdge(v) == 0) str_v = "VNULL";
    else {
        string str_x = "ABCDEFGHJKLMNOPQRST";
        str_v = str_x[etox[v] - 1];
        str_v += std::to_string(etoy[v]);
    }

    return str_v;
}

/**
 *  Repeat searching for the best move.
 */
int Tree::SearchTree(Board& b, double time_limit, double& win_rate,
                     bool is_errout, bool is_ponder)
{

    // 1. Update root node.
    if (b.move_cnt == 0) Tree::InitBoard();
    int node_idx = CreateNode(b);
    bool is_root_changed = (root_node_idx != node_idx);
    root_node_idx = node_idx;
    move_cnt = b.move_cnt;
    eval_policy_cnt = 0;
    eval_value_cnt = 0;

    // 2. Return pass if there is no legal move.
    Node *pn = &node[root_node_idx];
    if (pn->child_cnt <= 1) {
        if (is_errout) {
            std::stringstream ss;
            PrintChildInfo(root_node_idx, PASS, ss, false);
            PrintLog(log_file, "%s", ss.str().c_str());
        }
        
        win_rate = 0.5;
        return PASS;
    }

    // 3. Return joseki if exists.
    if (!is_ponder && move_cnt < 32 && book.find(BoardHash(b)) != book.end()) {
        std::vector<int> moves;
        for (auto& m:book[BoardHash(b)]) moves.push_back(m);

        int rnd_idx = int(moves.size()) * mt_double(mt_32);
        int next_move = moves[rnd_idx];
        if (b.IsLegal(b.my, next_move)) {
            if (is_errout) {
                std::stringstream ss;
                PrintChildInfo(root_node_idx, next_move, ss, false);
                PrintLog(log_file, "%s", ss.str().c_str());
            }

            win_rate = 0.5;
            return next_move;
        }
    }

    // 4. Adjust lambda to progress.
    if (cfg_rollout) {
        lambda = 0.8 - 0.4 * std::min(1.0, std::max(0.0, ((double)b.move_cnt - 160) / (360 - 160)));
    } else {
        lambda = 1.0;
    }
    // cp = 0.4 + 0.6 * std::min(1.0, std::max(0.0, ((double)b.move_cnt - 0) / (16 - 0)));
    cp = 0.4;

    bool use_rollout = (lambda != 1.0);

    // 5. If the root node is not evaluated, evaluate the probability.
    if (!pn->is_policy_eval || cfg_debug) {
        std::vector<std::array<double,EBVCNT>> prob_list;
        FeedTensor ft;
        ft.Set(b, PASS);
        std::vector<FeedTensor> ft_list;
        ft_list.push_back(ft);

        Network::get_policy_moves(ft_list, prob_list, cfg_sym_idx);
        UpdateNodeProb(root_node_idx, prob_list[0]);

        if (cfg_debug) {
            Network::debug_heatmap(ft_list[0], prob_list[0]);
        }
    }

    // 6. Sort child nodes in descending order of search count.
    std::vector<Child*> rc;
    SortChildren(pn, rc);

    // 7. Calculate the winning percentage of pc0.
    win_rate = BranchRate(rc[0]);
    int rc0_game_cnt = use_rollout? (int)rc[0]->rollout_cnt : (int)rc[0]->value_cnt;
    int rc1_game_cnt = use_rollout? (int)rc[1]->rollout_cnt : (int)rc[1]->value_cnt;

    // 8-1. Return best move without searching when time is running out.
    if (!is_ponder &&
        time_limit == 0.0 &&
        byoyomi == 0.0 &&
        left_time < cfg_emer_time)
    {
        // a. Return pass if the previous move is pass in Japanese rule.
        if (japanese_rule && b.prev_move[b.her] == PASS) return PASS;

        // b. Return the move with highest probability if total game count is less than 1000.
        if (rc0_game_cnt < 1000) {
            int v = pn->children[pn->prob_order[0]].move;
            if (is_errout) {
                PrintLog(log_file, "move cnt=%d: emagency mode: left time=%.1f[sec], move=%s, prob=.1%f[%%]\n",
                         b.move_cnt + 1, (double)left_time, CoordinateString(v).c_str(), pn->children[pn->prob_order[0]].prob * 100);
            }
            win_rate = 0.5;
            return v;
        }
    }
    // 8-2. Parallel search.
    else
    {
        // a. Check the nodes that exist under root and erase others
        if (is_root_changed) DeleteNodeIndex(root_node_idx);

        bool stand_out =
                !is_ponder &&
                (rc0_game_cnt > 50000 && rc0_game_cnt > 100 * rc1_game_cnt) &&
                ((double)left_time > byoyomi || byoyomi == 0);
        bool enough_game = pn->total_game_cnt > 500000;
        bool almost_win =
                !is_ponder &&
                pn->total_game_cnt > 10000 &&
                (win_rate < 0.05 || win_rate > 0.95);

        if (stand_out || enough_game || almost_win) {
            // Skip search.
            if (is_errout) {
                PrintLog(log_file, "move cnt=%d: left time=%.1f[sec]\n%d[nodes]\n",
                         b.move_cnt + 1, (double)left_time, node_cnt);
            }
        } else {
            const auto t1 = std::chrono::system_clock::now();
            int prev_game_cnt = pn->total_game_cnt;
            Statistics prev_stat = stat;

            double thinking_time = time_limit;
            bool can_extend = false;

            // b. Calculate maximum thinking time
            if (!is_ponder) {
                if (time_limit == 0.0) {
                    if (main_time == 0.0) {
                        // Set byoyomi if the main time is 0.
#ifdef OnlineMatch
                        thinking_time = std::max(byoyomi - 3, 0.1);
#else
                        thinking_time = std::max(byoyomi, 0.1);
#endif
                        can_extend = (extension_cnt > 0);
                    } else {
                        if (left_time < byoyomi * 2.0) {
#ifdef OnlineMatch
                            thinking_time = std::max(byoyomi - 3.0, 1.0); // Take 3sec margin.
#else
                            thinking_time = std::max(byoyomi - 1.0, 1.0); // Take 1sec margin.
#endif
                            can_extend = (extension_cnt > 0);
                        } else {
                            // Calculate from remaining time if sudden death,
                            // otherwise set that of 1-1.5 times of byoyomi.
                            thinking_time = std::max(
                                        left_time/(55.0 + std::max(50.0 - b.move_cnt, 0.0)),
                                        byoyomi * (1.5 - (double)std::max(50.0 - b.move_cnt, 0.0) / 100)
                                        );
                            // Do not extend thinking time if the remaining time is 10% or less.
                            can_extend = (left_time > main_time * 0.15) || (byoyomi >= 10);
                        }
                    }

                }

                // Think only for 1sec when either winning percentage is over 90%.
                if (win_rate < 0.1 || win_rate > 0.9) thinking_time = std::min(thinking_time, 1.0);
                can_extend &= (thinking_time > 1 && b.move_cnt > 3);
            }

            // c. Search in parallel with thread_cnt threads.
            ParallelSearch(thinking_time, b, is_ponder);
            SortChildren(pn, rc);

            // d. Extend thinking time when the trial number of first move
            //    and second move is close.
            if (!stop_think && can_extend) {

                rc0_game_cnt = use_rollout? (int)rc[0]->rollout_cnt : (int)rc[0]->value_cnt;
                rc1_game_cnt = use_rollout? (int)rc[1]->rollout_cnt : (int)rc[1]->value_cnt;

                double rc0_vr = (rc[0]->value_win / std::max(1, (int)rc[0]->value_cnt));
                double rc1_vr = (rc[1]->value_win / std::max(1, (int)rc[1]->value_cnt));

                if (rc0_game_cnt < rc1_game_cnt * 1.5 ||
                    (rc0_game_cnt < rc1_game_cnt * 2.5 && rc0_vr < rc1_vr))
                {
                    if (byoyomi > 0 && left_time <= byoyomi) {
                        --extension_cnt;
                    } else {
                        thinking_time *= 1.0;
                    }

                    ParallelSearch(thinking_time, b, is_ponder);
                    SortChildren(pn, rc);
                }
            }

            stop_think = false;

            // e. Update statistics of the board.
            if (pn->total_game_cnt - prev_game_cnt > 5000) stat -= prev_stat;

            // f. Output search information.
            auto t2 = std::chrono::system_clock::now();
            auto elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count()/1000;
            if (is_errout) {
                PrintLog(log_file, "move cnt=%d: left time=%.1f[sec]\n%d[nodes] %.1f[sec] %d[playouts] %.1f[pps/thread]\n",
                         b.move_cnt + 1,
                         std::max(0.0, (double)left_time - elapsed_time),
                         node_cnt,
                         elapsed_time,
                         (pn->total_game_cnt - prev_game_cnt),
                         (pn->total_game_cnt - prev_game_cnt) / elapsed_time / (thread_cnt - gpu_cnt)
                         );
            }

        }
    }

    // 8. Check whether pass should be returned. (Japanese rule)
    if (japanese_rule && b.prev_move[b.her] == PASS) {
        Board b_cpy;
        int win_cnt = 0;
        int playout_cnt = 1000;
        int pl = b.my;
        LGR lgr_th = lgr;

        for (int i = 0; i < playout_cnt; ++i) {
            b_cpy = b;
            b_cpy.PlayLegal(PASS);
            int result = PlayoutLGR(b_cpy, lgr_th, komi);
            if (pl == 0) {
                if (result == 0) ++win_cnt;
            } else {
                if (result != 0) ++win_cnt;
            }
        }

        // Return pass if the winning rate > 65%.
        if ((double)win_cnt / playout_cnt > 0.65) {
            win_rate = (double)win_cnt / playout_cnt;
            return PASS;
        }
    }
    // 9. When the best move is pass and the result is not much different,
    //    return the second move. (Chinese rule)
    else if (!japanese_rule && rc[0]->move == PASS) {
        double  win_sign = rc[0]->rollout_win * rc[1]->rollout_win;
        if (lambda == 1.0) win_sign = rc[0]->value_win * rc[1]->value_win;

        if (win_sign > 0) std::swap(rc[0], rc[1]);
    }

    // 10. Update winning_rate.
    win_rate = BranchRate(rc[0]);

    // 11. Output information of upper child nodes.
    if (is_errout) {
        PrintLog(log_file, "total games=%d, evaluated policy=%d(%d), value=%d(%d)\n",
                 (int)pn->total_game_cnt, eval_policy_cnt, (int)policy_que_cnt, eval_value_cnt, (int)value_que_cnt);

        std::stringstream ss;
        PrintChildInfo(root_node_idx, ss);
        PrintLog(log_file, "%s", ss.str().c_str());
    }
    return rc[0]->move;
}

/**
 *  Repeat searching with a single thread.
 */
void Tree::ThreadSearchBranch(Board& b, double time_limit, int cpu_idx, bool is_ponder) {

    Node* pn = &node[root_node_idx];
    if (pn->child_cnt <= 1) {
        stop_think = true;
        return;
    }

    bool use_rollout = (lambda != 1.0);
    int loop_cnt = 0;
    int initial_game_cnt = pn->total_game_cnt;
    const auto t1 = std::chrono::system_clock::now();

    mtx_lgr.lock();

    LGR lgr_th = lgr;
    Statistics stat_th = stat;
    Statistics initial_stat = stat;

    mtx_lgr.unlock();

#ifdef CPU_ONLY
    const int max_value_cnt = 24;
    const int max_policy_cnt = 12;
#else
    const int max_value_cnt = 192;
    const int max_policy_cnt = 96;
#endif //CPU_ONLY


    for (;;) {
        if (value_que_cnt > max_value_cnt || policy_que_cnt > max_policy_cnt) {
            // Wait for 1msec if the queue is full.
            std::this_thread::sleep_for (std::chrono::microseconds(1000)); //1 msec
        } else {
            Board b_ = b;
            int node_idx = root_node_idx;
            double value_result = 0.0;
            std::vector<std::pair<int,int>> serch_route;
            SearchBranch(b_, node_idx, value_result, serch_route, lgr_th, stat_th);
        }
        ++loop_cnt;

        // Check whether to terminate the search every 64 times.
        if (loop_cnt % 64 == 0) {
            auto t2 = std::chrono::system_clock::now();
            auto elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;

            // Terminate the search when the time limit has elapsed or stop_think flag is set.
            if (elapsed_time > time_limit || stop_think) {
                stat_th -= initial_stat;
                {
                    std::lock_guard<std::mutex> lock(mtx_lgr);
                    lgr = lgr_th;
                    stat += stat_th;
                }
                break;
            }
            else if (!is_ponder && (byoyomi == 0 || (double)left_time > byoyomi))
            {
                std::vector<Child*> rc;
                SortChildren(pn, rc);

                int th_rollout_cnt = (int)pn->total_game_cnt - initial_game_cnt;
                int rc0_cnt = use_rollout? (int)rc[0]->rollout_cnt : (int)rc[0]->value_cnt;
                int rc1_cnt = use_rollout? (int)rc[1]->rollout_cnt : (int)rc[1]->value_cnt;
                double max_rc1_cnt = rc1_cnt + th_rollout_cnt * (time_limit - elapsed_time) / elapsed_time;

                bool stand_out =     rc0_cnt > 50000 &&
                                    rc0_cnt > 100 * rc1_cnt;
                bool cannot_catchup =    th_rollout_cnt > 25000 &&
                                        rc0_cnt > 1.5 * max_rc1_cnt;

                if (stand_out || cannot_catchup) {
                    stat_th -= initial_stat;
                    {
                        std::lock_guard<std::mutex> lock(mtx_lgr);
                        lgr = lgr_th;
                        stat += stat_th;
                    }

                    stop_think = true;
                    break;
                }
            }
        }
    }
}

/**
 *  Evaluate policy and value of boards in a single thread.
 */
void Tree::ThreadEvaluate(double time_limit, int gpu_idx, bool is_ponder) {

    const auto t1 = std::chrono::system_clock::now();
    std::deque<ValueEntry> vque_th;
    std::deque<PolicyEntry> pque_th;
    std::vector<FeedTensor> ft_list;
    std::vector<std::array<double,EBVCNT>> prob_list;

#ifdef CPU_ONLY
    const int max_eval_value = 1;
    const int max_eval_policy = 1;
#else
    const int max_eval_value = 48;
    const int max_eval_policy = 12;
#endif //CPU_ONLY

    for (;;) {

        // 1. Process value_que.
        if (value_que_cnt > 0) {
            int eval_cnt = 0;
            {
                std::lock_guard<std::mutex> lock(mtx_vque);
                if (value_que_cnt > 0) {
                    eval_cnt = std::min(max_eval_value, (int)value_que_cnt);

                    // a. Copy partially to vque_th.
                    vque_th.resize(eval_cnt);
                    copy(value_que.begin(), value_que.begin() + eval_cnt, vque_th.begin());

                    // b. Remove value_que from the beginning.
                    for (int i = 0; i < eval_cnt; ++i) value_que.pop_front();
                    value_que_cnt -= eval_cnt;
                }
            }

            if (eval_cnt > 0) {
                Child* pc0;

                if (eval_cnt > 0) {

                    // c. Evaluate value.
                    ft_list.clear();
                    for (int i = 0; i < eval_cnt; ++i) {
                        ft_list.push_back(vque_th[i].ft);
                    }
                    std::vector<float> eval_list;

                    Network::get_value_moves(ft_list, eval_list, cfg_sym_idx);
                
                    // d. Update all value information of the upstream nodes.
                    for (int i = 0; i < eval_cnt; ++i) {

                        int leaf_pl = ft_list[i].color;
                        pc0 = &node[vque_th[i].node_idx[0]].children[vque_th[i].child_idx[0]];
                        pc0->value = -(double)eval_list[i];
                        pc0->is_value_eval = true;

                        for (int j = 0, n = vque_th[i].depth; j < n; ++j) {
                            if (vque_th[i].node_idx[j] < 0             ||
                                vque_th[i].node_idx[j] >= node_limit   ||
                                vque_th[i].child_idx[j] >= node[vque_th[i].node_idx[j]].child_cnt) continue;

                            Node* pn = &node[vque_th[i].node_idx[j]];
                            Child* pc = &pn->children[vque_th[i].child_idx[j]];
                            int add_cnt = vque_th[i].request_cnt;
                            double add_win = double(eval_list[i]) * add_cnt;
                            if (int(pn->pl) != leaf_pl) add_win *= -1;

                            FetchAdd(&pc->value_win, add_win);
                            pc->value_cnt += add_cnt;
                            FetchAdd(&pn->value_win, add_win);
                            pn->value_cnt += add_cnt;

                            if (vque_th[i].node_idx[j] == root_node_idx) break;
                        }

                    }
                    eval_value_cnt += eval_cnt;
                }
            }
        }

        // 2. Process policy_que.
#ifdef CPU_ONLY
        if (policy_que_cnt > 0 && mt_double(mt_32) < 0.25) {
#else
        if (policy_que_cnt > 0) {
#endif //CPU_ONLY
            int eval_cnt = 0;
            {
                std::lock_guard<std::mutex> lock(mtx_pque);
                if (policy_que_cnt > 0) {
                    eval_cnt = std::min(max_eval_policy, (int)policy_que_cnt);
                    eval_policy_cnt += eval_cnt;

                    // a. Copy partially to pque_th.
                    pque_th.resize(eval_cnt);
                    copy(policy_que.begin(), policy_que.begin()+eval_cnt, pque_th.begin());

                    // b. Remove policy_que from the beginning.
                    for (int i = 0; i < eval_cnt; ++i) policy_que.pop_front();
                    policy_que_cnt -= eval_cnt;
                }
            }

            if (eval_cnt > 0) {
                ft_list.clear();
                for (int i = 0; i < eval_cnt; ++i) {
                    ft_list.push_back(pque_th[i].ft);
                }

                // c. Evaluate policy.

                Network::get_policy_moves(ft_list, prob_list, cfg_sym_idx);

                // d. Update probability of nodes.
                for (int i = 0; i < eval_cnt; ++i) {
                    UpdateNodeProb(pque_th[i].node_idx, prob_list[i]);
                }
            }
        }

        // 3. Terminate evaluation when the time limit has elapsed or stop_think flag is set.
        auto t2 = std::chrono::system_clock::now();
        auto elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count()/1000;
        if (elapsed_time > time_limit || stop_think) {
            stop_think = true;
            break;
        }

    }

}

/**
 *  Search in parallel with thread_cnt threads.
 */
void Tree::ParallelSearch(double time_limit, Board& b, bool is_ponder) {
    std::vector<std::thread> ths(thread_cnt);
    std::vector<Board> b_;
    for (int i = 0; i < thread_cnt-gpu_cnt; ++i) {
        Board b_tmp = b;
        b_.push_back(b_tmp);
    }

    for (int i = 0; i < thread_cnt; ++i) {
        if (i < gpu_cnt) {
            ths[i] = std::thread(&Tree::ThreadEvaluate, this, time_limit, i, is_ponder);
        } else {
            ths[i] = std::thread(&Tree::ThreadSearchBranch, this, std::ref(b_[i - gpu_cnt]), time_limit, i - gpu_cnt, is_ponder);
        }
    }

    for (std::thread& th : ths) th.join();
}


/**
 *  Roll out 1000 times and output the final result.
 */
void Tree::PrintResult(Board& b) {

    stat.Clear();

    int win_cnt = 0;
    int rollout_cnt = 1000;
    Board b_ = b;
    for (int i = 0; i < rollout_cnt; ++i) {
        b_ = b;
        int result = PlayoutLGR(b_, lgr, stat, komi);
        if (result != 0) ++win_cnt;
    }
    int win_pl = ((double)win_cnt/rollout_cnt >= 0.5) ? 1 : 0;

    PrintFinalScore(b, stat.game, stat.owner, win_pl, komi, log_file);

}


std::string Tree::BestSequence(int node_idx, int head_move, int max_move) {

    string seq = "";
    Node* pn = &node[node_idx];
    if (head_move == VNULL && pn->prev_move[0] == VNULL) return seq;


    int head_move_ = (head_move == VNULL) ? (int)pn->prev_move[0] : head_move;
    string head_str = CoordinateString(head_move_);
    if (head_str.length() == 2) head_str += " ";

    seq += head_str;

    std::vector<Child*> child_list;
    for (int i = 0; i < max_move; ++i) {
        if (pn->child_cnt <= 1) break;
        SortChildren(pn, child_list);

        string move_str = CoordinateString(child_list[0]->move);
        if (move_str.length() == 2) move_str += " ";

        seq += "->";
        seq += move_str;

        if (child_list[0]->is_next &&
            node[child_list[0]->next_idx].hash == child_list[0]->next_hash)
        {
            pn = &node[child_list[0]->next_idx];
        }
        else break;
    }
    // D4->D16->Q16->Q4->...
    return seq;
}

void Tree::PrintGFX(std::ostream& ost) {

    double occupancy[BVCNT] = {0};
    double game_cnt = std::max(1.0, (double)stat.game[2]);
    for (int i = 0; i < BVCNT; ++i)
        occupancy[i] = (stat.owner[1][rtoe[i]]/game_cnt - 0.5) * 2;

    ost << "gogui-gfx:\n";
    ost << "VAR ";

    Node* pn = &node[root_node_idx];
    std::vector<Child*> child_list;
    for (int i = 0; i < 9; ++i) {
        if (pn->child_cnt <= 1) break;
        SortChildren(pn, child_list);

        int move = child_list[0]->move;

        string pl_str = (pn->pl == 1) ? "b " : "w ";
        ost << pl_str << CoordinateString(move) << " ";

        if (move != PASS) occupancy[etor[move]] = 0;

        if (child_list[0]->is_next &&
            node[child_list[0]->next_idx].hash == child_list[0]->next_hash)
        {
            pn = &node[child_list[0]->next_idx];
        }
        else break;
    }
    ost << endl;

    if (stat.game[2] == 0) return;

    ost << "INFLUENCE ";
    for (int i = 0; i < BVCNT; ++i) {
        if (-0.2 < occupancy[i] && occupancy[i] < 0.2) continue;
        ost << CoordinateString(rtoe[i]) << " " << occupancy[i] << " ";
    }
    ost << endl;

}

void Tree::PrintChildInfo(int node_idx, std::ostream& ost) {

    Node* pn = &node[node_idx];
    std::vector<Child*> rc;
    SortChildren(pn, rc);

    ost << "|move|count  |value|roll |prob |depth| best sequence" << endl;

    for (int i = 0; i < std::min((int)pn->child_cnt, 10); ++i) {

        Child* pc = rc[i];
        int game_cnt = (lambda != 1.0) ? (int)pc->rollout_cnt : (int)pc->value_cnt;

        if (game_cnt == 0) break;

        double rollout_rate = (pc->rollout_win / std::max(1, (int)pc->rollout_cnt) + 1) / 2;
        double value_rate = (pc->value_win / std::max(1, (int)pc->value_cnt) + 1) / 2;
        if (pc->value_win == 0.0) value_rate = 0;
        if (pc->rollout_win == 0.0) rollout_rate = 0;

        int depth = 1;
        string seq;
        if (pc->is_next) {
            std::unordered_set<int> node_list;
            depth = CollectNodeIndex((int)pc->next_idx, depth, node_list);
            seq = BestSequence((int)pc->next_idx, (int)pc->move);
        }

        ost << "|" << std::left << std::setw(4) << CoordinateString((int)pc->move);
        ost << "|" << std::right << std::setw(7) << std::min(9999999, game_cnt);
        auto prc = ost.precision();
        ost.precision(1);
        if (pc->value_cnt == 0) ost << "|" << std::setw(5) << "N/A";
        else ost << "|" << std::setw(5) << std::fixed << value_rate * 100;
        if (pc->rollout_cnt == 0) ost << "|" << std::setw(5) << "N/A";
        else ost << "|" << std::setw(5) << std::fixed << rollout_rate * 100;
        ost << "|" << std::setw(5) << std::fixed << (double)pc->prob * 100;
        ost.precision(prc);
        ost << "|" << std::setw(5) << depth;
        ost << "| " << seq;
        ost << endl;
    }
}

void Tree::PrintChildInfo(int node_idx, int next_move, std::ostream& ost, bool is_opp) {

    Node* pn = &node[node_idx];
    std::vector<Child*> rc;
    SortChildren(pn, rc);

    int nc_idx = -1;
    for (int i = 0; i < (int)rc.size(); ++i) {
        if (rc[i]->move == next_move) nc_idx = i;
    }

    if (nc_idx == -1) {
        ost << "not in children.\n";
        return;
    }

    ost << "|move|count  |value|roll |prob |depth| best sequence" << endl;

    for (int i = -1; i < std::min((int)pn->child_cnt, 8); ++i) {

        Child* pc;
        if (i < 0) pc = rc[nc_idx];
        else pc = rc[i];

        int game_cnt = std::max((int)pc->rollout_cnt, (int)pc->value_cnt);
        if (game_cnt == 0 && i >= 0) break;

        double rollout_rate = (pc->rollout_win / std::max(1, (int)pc->rollout_cnt) + 1) / 2;
        double value_rate = (pc->value_win / std::max(1, (int)pc->value_cnt) + 1) / 2;
        if (is_opp) {
            rollout_rate = 1 - rollout_rate;
            value_rate = 1 - value_rate;
        }

        if (pc->value_win == 0.0) value_rate = 0;
        if (pc->rollout_win == 0.0) rollout_rate = 0;


        int depth = 1;
        string seq;
        if (pc->is_next) {
            std::unordered_set<int> node_list;
            depth = CollectNodeIndex((int)pc->next_idx, depth, node_list);
            seq = BestSequence((int)pc->next_idx, (int)pc->move);
        }

        ost << "|" << std::left << std::setw(4) << CoordinateString((int)pc->move);
        ost << "|" << std::right << std::setw(7) << std::min(9999999, game_cnt);
        auto prc = ost.precision();
        ost.precision(1);
        if (pc->value_cnt == 0) ost << "|" << std::setw(5) << "N/A";
        else ost << "|" << std::setw(5) << std::fixed << value_rate * 100;
        if (pc->rollout_cnt == 0) ost << "|" << std::setw(5) << "N/A";
        else ost << "|" << std::setw(5) << std::fixed << rollout_rate * 100;
        ost << "|" << std::setw(5) << std::fixed << (double)pc->prob * 100;
        ost.precision(prc);
        ost << "|" << std::setw(5) << depth;
        ost << "| " << seq;
        ost << endl;
        if (i < 0 && is_opp) ost << "--" << endl;
    }
}
