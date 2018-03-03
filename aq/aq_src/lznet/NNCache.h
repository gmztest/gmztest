/*
    This file is part of Leela Zero.
    Copyright (C) 2017 Michael O

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

#ifndef NNCACHE_H_INCLUDED
#define NNCACHE_H_INCLUDED

#include "config.h"

#include <deque>
#include <mutex>
#include <unordered_map>

#include "lz_net.h"

class NNCache {
public:
    // return the global NNCache
    static NNCache& get_NNCache(void);

    // Set a reasonable size gives max number of playouts
    void set_size_from_playouts(int max_playouts);

    // Resize NNCache
    void resize(int size);

    // Try and find an existing entry.
    bool lookup_policy(const Network::NNPlanes& features, Network::Prob& move_prob);
    bool lookup_value(const Network::NNPlanes& features, float& value);

    // Insert a new entry.
    void insert_policy(const Network::NNPlanes& features,
                       const Network::Prob& move_prob);
    void insert_value(const Network::NNPlanes& features,
                      const float& value);

    // Return the hit rate ratio.
    std::pair<int, int> hit_rate() const {
        return {m_hits, m_lookups};
    }

    void dump_stats();

private:
    NNCache(int size = 50000);  // ~ 250MB

    std::mutex m_mutex;

    size_t m_size;

    // Statistics
    int m_hits{0};
    int m_lookups{0};
    int m_inserts{0};
    int m_collisions{0};

    struct Policy {
        Policy(const Network::NNPlanes& f, const Network::Prob& p)
               : features(f), move_prob(p) {}
        Network::NNPlanes features; // ~ 1KB
        Network::Prob move_prob;  // ~ 3KB
    };

    struct Value {
        Value(const Network::NNPlanes& f, const float& v)
              : features(f), value(v) {}
        Network::NNPlanes features;
        float value;
    };

    // Map from hash to {features, result}
    std::unordered_map<size_t, std::unique_ptr<const Policy>> m_policy_cache;
    std::unordered_map<size_t, std::unique_ptr<const Value>> m_value_cache;
    // Order entries were added to the map.
    std::deque<size_t> m_policy_order;
    std::deque<size_t> m_value_order;
};

#endif
