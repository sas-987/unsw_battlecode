#include "helper.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

class DragonBot {
private:
    unswbc::Direction explore_dir;

    // Calculates wrapped distance on the board
    int toroidal_dist(unswbc::Position a, unswbc::Position b) {
        int dx = std::abs(a.x - b.x);
        int dy = std::abs(a.y - b.y);
        return std::min(dx, unswbc::game->width - dx) + std::min(dy, unswbc::game->height - dy);
    }

    // Ensures we don't hit kelp, portals, or any dragons (including ourselves)
    bool is_move_safe(unswbc::Controller& ct, unswbc::Position here, unswbc::Direction dir) {
        unswbc::Tile const* tile = ct.get_tile(here);
        if (!tile) return false;
        
        auto const& edge = tile->get_edge(dir);
        if (!edge.is_passable() || edge.is_portal()) return false;
        
        unswbc::Position next = here.add_dir(dir);
        unswbc::Tile const* next_tile = ct.get_tile(next);
        
        if (!next_tile || next_tile->get_dragon() != nullptr) return false;
        return true;
    }

    std::vector<unswbc::Direction> get_safe_moves(unswbc::Controller& ct, unswbc::Position here) {
        std::vector<unswbc::Direction> safe;
        for (auto d : unswbc::Direction::get_direction_list()) {
            if (is_move_safe(ct, here, d)) safe.push_back(d);
        }
        return safe;
    }

    // Finds the safest move that takes us closest to a known pearl
    unswbc::Direction get_closest_pearl_dir(unswbc::Controller& ct, unswbc::Position here, const std::vector<unswbc::Direction>& safe) {
        unswbc::Direction best_dir = safe.front();
        int min_dist = 999999;
        
        for (auto& tile : ct.get_tiles()) {
            if (tile.has_pearl()) {
                for (auto d : safe) {
                    int dist = toroidal_dist(here.add_dir(d), tile.get_position());
                    if (dist < min_dist) { 
                        min_dist = dist; 
                        best_dir = d; 
                    }
                }
            }
        }
        return best_dir;
    }

public:
    DragonBot() : explore_dir(unswbc::Direction::NORTH) {}

    void turn(unswbc::Controller& ct, unswbc::Game& game) {
        unswbc::Position here = ct.get_position();

        // Assign a dedicated exploration direction based on ID to ensure the swarm disperses
        static bool dir_initialized = false;
        if (!dir_initialized) {
            auto dirs = unswbc::Direction::get_direction_list();
            explore_dir = dirs[ct.get_id() % 4];
            dir_initialized = true;
        }

        // 1. DISPERSE AS MUCH AS POSSIBLE
        // Automatically splits if we are long enough to afford it and under the unit limit
        if (ct.can_split(2)) {
            ct.do_split(2);
            return;
        }

        auto safe = get_safe_moves(ct, here);

        // Fallback: If trapped, try an emergency passable move, else just default north
        if (safe.empty()) {
            for (auto d : unswbc::Direction::get_direction_list()) {
                unswbc::Tile const* t = ct.get_tile(here);
                if (t && t->get_edge(d).is_passable()) {
                    ct.make_move(d);
                    return;
                }
            }
            ct.make_move(unswbc::Direction::NORTH);
            return;
        }

        // 2. CHECK FOR ADJACENT PEARLS
        for (auto d : safe) {
            unswbc::Tile const* t = ct.get_tile(here.add_dir(d));
            if (t && t->has_pearl()) {
                ct.make_move(d);
                return;
            }
        }

        // 3. HUNT VISIBLE PEARLS
        bool sees_pearl = false;
        for (auto& t : ct.get_tiles()) {
            if (t.has_pearl()) sees_pearl = true;
        }
        
        if (sees_pearl) {
            ct.make_move(get_closest_pearl_dir(ct, here, safe));
            return;
        }

        // 4. CONTINUE EXPLORING
        // Keep moving in our assigned dispersal direction if it's safe
        if (std::find(safe.begin(), safe.end(), explore_dir) != safe.end()) {
            ct.make_move(explore_dir);
            return;
        }

        // If our preferred direction is blocked, turn and pick a new safe direction
        explore_dir = safe.front();
        ct.make_move(explore_dir);
    }
};

int main() {
    auto [ct, game] = unswbc::init();
    DragonBot bot;
    while (unswbc::update(ct, game)) {
        bot.turn(ct, game);
        unswbc::end_turn();
    }
    return 0;
}