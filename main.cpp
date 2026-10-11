#include "helper.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using unswbc::Controller;
using unswbc::Direction;
using unswbc::DragonPart;
using unswbc::Game;
using unswbc::Position;
using unswbc::Tile;

namespace cfg {
constexpr int SPREAD_ROUNDS = 60;
constexpr int RESCUE_START = 350;
constexpr int RESCUE_END = 400;
constexpr int ASSAULT_ROUND = 400;
constexpr int ASSAULT_MIN_UNITS = 4;
constexpr int SPRINT_MAX_STEPS = 3;
constexpr int AREA_MARGIN = 2;
constexpr int QUEEN_AREA_MARGIN = 6;
constexpr int AREA_CAP = 90;
constexpr int FLEE_TRIGGER = 3;

constexpr double W_QUEEN_UNCOIL = 50000;
constexpr double W_QUEEN_EMERGENCY = 30000;
constexpr double W_GUARD_KAMIKAZE = 25000;
constexpr double W_PINCER_SPOTTER = 15000;
constexpr double W_PEARL_EAT = 8000;
constexpr double W_PINCER_HELPER = 6000;
constexpr double W_BREEDER_SPLIT = 4500;
constexpr double W_BISHOP_STAKE = 4000;
constexpr double W_BISHOP_PATROL = 3000;
constexpr double W_COMPASS_MOVE = 1500;
constexpr double W_IDLE_SPRINT = 800;

constexpr double W_K1_ASSASSIN = 26000;
constexpr double W_QUEEN_SPACE = 2200;
constexpr double W_CLEAR_SUICIDE = 9000;
constexpr double W_FALLBACK_EXPLORE = 500;

constexpr uint64_t SONAR_SECRET = 0x5EC12E7B0A7C0DE1ULL;
}

constexpr int DX[4] = {0, 1, 0, -1};
constexpr int DY[4] = {-1, 0, 1, 0};

constexpr uint8_t EDGE_UNKNOWN = 0;
constexpr uint8_t EDGE_EMPTY = 1;
constexpr uint8_t EDGE_KELP = 2;
constexpr uint8_t EDGE_PORTAL = 3;

constexpr int BODY_NONE = -1;
constexpr int BODY_STRAY = -2;

const std::array<Direction, 4>& all_dirs() {
    static const std::array<Direction, 4> dirs = Direction::get_direction_list();
    return dirs;
}

inline int dir_index(Direction d) {
    switch (d.value) {
    case Direction::NORTH: return 0;
    case Direction::EAST: return 1;
    case Direction::SOUTH: return 2;
    default: return 3;
    }
}

inline int opposite(int d) { return (d + 2) & 3; }
inline char dir_char(int d) { return "NESW"[d & 3]; }

inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline uint8_t edge_code(const unswbc::Edge& e) {
    if (e.is_portal()) return EDGE_PORTAL;
    return e.is_passable() ? EDGE_EMPTY : EDGE_KELP;
}

enum class Compass8 : uint8_t {
    N = 0, NE = 1, E = 2, SE = 3, S = 4, SW = 5, W = 6, NW = 7
};

struct Vector2D { int dx; int dy; };

constexpr Vector2D COMPASS_VECTORS[8] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
};

inline Compass8 reflect_heading(Compass8 heading, bool hit_vert, bool hit_horiz) {
    if (hit_vert) {
        switch (heading) {
        case Compass8::NE: heading = Compass8::NW; break;
        case Compass8::NW: heading = Compass8::NE; break;
        case Compass8::SE: heading = Compass8::SW; break;
        case Compass8::SW: heading = Compass8::SE; break;
        case Compass8::E:  heading = Compass8::W; break;
        case Compass8::W:  heading = Compass8::E; break;
        default: break;
        }
    }
    if (hit_horiz) {
        switch (heading) {
        case Compass8::NE: heading = Compass8::SE; break;
        case Compass8::SE: heading = Compass8::NE; break;
        case Compass8::NW: heading = Compass8::SW; break;
        case Compass8::SW: heading = Compass8::NW; break;
        case Compass8::N:  heading = Compass8::S; break;
        case Compass8::S:  heading = Compass8::N; break;
        default: break;
        }
    }
    return heading;
}

namespace sonar {
enum class ActionId : uint8_t {
    ACT_NONE = 0,
    ACT_BEACON = 1,
    ACT_RENDEZVOUS = 2,
    ACT_SPLIT_VECTOR = 3,
    ACT_PINCER_CALL = 4,
    ACT_CLEAR_PATH = 5,
    ACT_CLUSTER = 6,
    ACT_PEARL_DROP = 7
};

inline uint64_t compute_auth(uint8_t action) {
    uint64_t seed = cfg::SONAR_SECRET ^ (static_cast<uint64_t>(action) << 4);
    return splitmix64(seed) >> 32;
}

inline uint64_t pack(ActionId act, uint8_t dir8, int x, int y, uint8_t extra = 0) {
    uint64_t auth = compute_auth(static_cast<uint8_t>(act));
    uint64_t pkt = 0;
    pkt |= (auth & 0xFFFFFFFFULL) << 32;
    pkt |= (static_cast<uint64_t>(static_cast<uint8_t>(act) & 0xF)) << 28;
    pkt |= (static_cast<uint64_t>(dir8 & 0x7)) << 25;
    pkt |= (static_cast<uint64_t>(x & 0xFF)) << 16;
    pkt |= (static_cast<uint64_t>(y & 0xFF)) << 8;
    pkt |= static_cast<uint64_t>(extra & 0xFF);
    return pkt;
}

inline bool unpack(uint64_t pkt, ActionId& act, uint8_t& dir8, int& x, int& y, uint8_t& extra) {
    uint64_t auth = (pkt >> 32) & 0xFFFFFFFFULL;
    uint8_t a = static_cast<uint8_t>((pkt >> 28) & 0xF);
    dir8 = static_cast<uint8_t>((pkt >> 25) & 0x7);
    x = static_cast<int>((pkt >> 16) & 0xFF);
    y = static_cast<int>((pkt >> 8) & 0xFF);
    extra = static_cast<uint8_t>(pkt & 0xFF);
    if (auth != compute_auth(a)) return false;
    act = static_cast<ActionId>(a);
    return true;
}
}

enum class Role : uint8_t {
    QUEEN,
    BISHOP,
    GUARD,
    BREEDER,
    EXPLORER,
    PINCER_SPOTTER,
    PINCER_HELPER
};

enum ActionFlags : unsigned {
    F_NONE = 0,
    F_NO_AREA = 1,
    F_NO_THREAT = 2,
    F_LUNGE = 4,
    F_SUICIDE = 8,
    F_RELAXED = 16,
    F_NO_SPRINT = 32,
    F_QUEEN_SPREAD = 64,
    F_FLEE_SPRINT = 128,
    F_NO_TAILCHECK = 256
};

struct SonarOrder {
    int dir;
    sonar::ActionId act;
    uint8_t dir8;
    int x;
    int y;
    uint8_t extra;
};

struct Action {
    enum Kind { MOVE, SPLIT } kind = MOVE;
    double weight = 0;
    std::vector<int> dests;
    int minLen = 0;
    int parentLen = 0;
    int childLen = 0;
    unsigned flags = F_NONE;
    std::vector<SonarOrder> exc;
    const char* tag = "";
    bool operator<(const Action& o) const { return weight < o.weight; }
};

class Bot {
public:
    Bot(Controller& ct, Game& game)
        : ct_(ct), game_(game), W_(game.width), H_(game.height), N_(game.width * game.height) {
        nbr_.resize(static_cast<size_t>(N_) * 4);
        for (int y = 0; y < H_; ++y) {
            for (int x = 0; x < W_; ++x) {
                for (int d = 0; d < 4; ++d) {
                    nbr_[(y * W_ + x) * 4 + d] = ((y + DY[d] + H_) % H_) * W_ + (x + DX[d] + W_) % W_;
                }
            }
        }
        lastSeen_.assign(N_, -1);
        lastVisit_.assign(N_, -1000);
        pearlTime_.assign(N_, -1);
        pearl_.assign(N_, 0);
        edge_.assign(static_cast<size_t>(N_) * 4, EDGE_UNKNOWN);
        bodyIdx_.assign(N_, BODY_NONE);
        blocked_.assign(N_, 0);
        enemyOcc_.assign(N_, 0);
        dist_.assign(N_, -1);
        par_.assign(N_, -1);
        parDir_.assign(N_, -1);
        vis_.assign(N_, 0);
        fd_.assign(N_, 0);
        extra_.assign(N_, 0);
        areaStamp_.fill(0);
        areaCache_.fill(0);
        sonarMsg_.fill(0);
        sonarPrio_.fill(0);
    }

    void play_turn() {
        bool acted = false;
        try {
            observe();
            read_sonar();
            acted = act();
        } catch (...) {
            acted = false;
        }
        if (!acted) simple_fallback();
    }

private:
    struct HeadInfo {
        int tile;
        int id;
        int dir;
    };

    struct Move {
        int d = 0;
        bool fatal = false;
        bool portal = false;
        bool kelp = false;
        bool headOn = false;
        bool enemyHead = false;
        bool enemyQueenHead = false;
        int dest = -1;
    };

    Controller& ct_;
    Game& game_;
    int W_, H_, N_;
    std::vector<int> nbr_;

    std::vector<int> lastSeen_;
    std::vector<int> lastVisit_;
    std::vector<int> pearlTime_;
    std::vector<char> pearl_;
    std::vector<uint8_t> edge_;
    std::deque<int> trail_;

    int firstRound_ = -1;
    int lastSplitRound_ = -1000;
    int queenSpreadSplits_ = 0;
    int startTile_ = -1;
    int peakUnits_ = 0;
    int scoutDir_ = -1;
    int forbidDir_ = -1;
    bool assault_ = false;

    int queenPos_ = -1;
    int queenRound_ = -100;
    int bishopPos_ = -1;
    int bishopRound_ = -100;
    int eqPos_ = -1;
    int eqRound_ = -100;
    bool eqDead_ = false;

    std::vector<std::pair<int, int>> enemyMem_;
    std::vector<std::pair<int, int>> eqPrev_;

    int turnStamp_ = 0;
    int round_ = 0;
    int len_ = 0;
    int id_ = 0;
    int headIdx_ = 0;
    int facing_ = 0;
    int units_ = 0;
    bool queen_ = false;
    bool orientation_ = false;
    bool alert_ = false;
    bool idle_ = false;
    bool strict_ = false;
    int need_ = 0;
    int ourQueenTile_ = -1;

    Role baseRole_ = Role::EXPLORER;
    std::vector<Role> roleStack_;
    Compass8 heading_ = Compass8::N;
    uint8_t pearlHistory_ = 0;
    int signalsReceivedInRescue_ = 0;
    int bishopClusterTile_ = -1;
    int bishopR_ = 4;
    int turnsAsBishop_ = 0;

    int pincerTarget_ = -1;
    int pincerFlank_ = -1;
    int pearlSignalTile_ = -1;

    std::vector<int> bodyIdx_;
    std::vector<char> blocked_;
    std::vector<char> enemyOcc_;
    std::vector<HeadInfo> enemyHeads_, allyHeads_;
    std::unordered_map<int, int> enemySegs_;
    std::vector<int> chain_;
    std::vector<const DragonPart*> ownBody_;
    std::array<Move, 4> mv_;
    std::priority_queue<Action> heap_;
    std::deque<std::pair<int, int>> relBodyQueue_;

    std::array<uint64_t, 4> sonarMsg_{};
    std::array<int, 4> sonarPrio_{};

    std::vector<int> dist_, par_, parDir_, queue_;
    std::vector<int> vis_, fd_;
    std::vector<char> extra_;
    int visStamp_ = 0;
    std::array<int, 4> areaCache_{}, areaStamp_{};

    int idx(Position p) const { return p.y * W_ + p.x; }
    Position pos_of(int i) const { return Position(i % W_, i / W_); }

    int wrap_dist(int a, int b) const {
        if (a < 0 || b < 0) return 1000000;
        int dx = std::abs(a % W_ - b % W_);
        int dy = std::abs(a / W_ - b / W_);
        return std::min(dx, W_ - dx) + std::min(dy, H_ - dy);
    }

    int cheb_dist(int a, int b) const {
        int dx = std::abs(a % W_ - b % W_);
        int dy = std::abs(a / W_ - b / W_);
        return std::max(std::min(dx, W_ - dx), std::min(dy, H_ - dy));
    }

    bool in_window(int tile) const {
        int dx = std::abs(tile % W_ - headIdx_ % W_);
        int dy = std::abs(tile / W_ - headIdx_ / W_);
        return std::min(dx, W_ - dx) <= 3 && std::min(dy, H_ - dy) <= 3;
    }

    void set_edge(int tile, int d, uint8_t code) {
        edge_[tile * 4 + d] = code;
        edge_[nbr_[tile * 4 + d] * 4 + opposite(d)] = code;
    }

    Role active_role() const {
        return roleStack_.empty() ? baseRole_ : roleStack_.back();
    }

    bool can_enter(int from, int d, int t, int eaten = 0) const {
        const uint8_t e = edge_[from * 4 + d];
        if (e == EDGE_KELP || e == EDGE_PORTAL) return false;
        const int to = nbr_[from * 4 + d];
        if (blocked_[to]) return false;
        const int bi = bodyIdx_[to];
        if (bi == BODY_STRAY) return false;
        if (bi >= 0 && t - eaten < len_ - bi + 1) return false;
        return true;
    }

    void observe() {
        ++turnStamp_;
        round_ = game_.get_round_num();
        len_ = ct_.get_length();
        id_ = ct_.get_id();
        queen_ = (id_ < 2);
        headIdx_ = idx(ct_.get_position());
        facing_ = dir_index(ct_.get_dir());
        units_ = ct_.get_unit_count();
        peakUnits_ = std::max(peakUnits_, units_);
        assault_ = (!queen_ && round_ >= cfg::ASSAULT_ROUND && units_ >= std::max(cfg::ASSAULT_MIN_UNITS, peakUnits_ / 2));

        if (firstRound_ < 0) {
            firstRound_ = round_;
            heading_ = static_cast<Compass8>(id_ % 8);
            if (queen_) {
                baseRole_ = Role::QUEEN;
            } else if ((id_ % 61) < 22) {
                baseRole_ = Role::BREEDER;
            } else {
                baseRole_ = Role::EXPLORER;
            }
            roleStack_.clear();
            roleStack_.push_back(baseRole_);
            if (round_ <= 1) startTile_ = headIdx_;
            else lastSplitRound_ = round_;
        }

        if (round_ >= cfg::RESCUE_START && round_ <= cfg::RESCUE_END && !queen_) {
            if (signalsReceivedInRescue_ < 8) {
                baseRole_ = Role::BREEDER;
            }
        }
        if (round_ >= cfg::ASSAULT_ROUND && !queen_) {
            if (active_role() != Role::GUARD) {
                baseRole_ = Role::EXPLORER;
            }
        }

        orientation_ = (firstRound_ > 1 && round_ <= firstRound_ + 1);
        lastVisit_[headIdx_] = round_;

        std::fill(bodyIdx_.begin(), bodyIdx_.end(), BODY_NONE);
        std::fill(blocked_.begin(), blocked_.end(), 0);
        std::fill(enemyOcc_.begin(), enemyOcc_.end(), 0);
        enemyHeads_.clear();
        allyHeads_.clear();
        enemySegs_.clear();
        ownBody_.clear();
        ourQueenTile_ = -1;

        std::vector<std::pair<int, int>> eqNow;
        int enemyParts = 0;
        int visiblePearls = 0;
        bool sawQueen = false;
        bool sawBishop = false;

        for (const Tile& t : ct_.get_tiles()) {
            const int i = idx(t.get_position());
            lastSeen_[i] = round_;
            pearl_[i] = t.has_pearl() ? 1 : 0;
            if (pearl_[i]) ++visiblePearls;
            pearlTime_[i] = t.get_pearl_time();
            for (int d = 0; d < 4; ++d) set_edge(i, d, edge_code(t.get_edge(all_dirs()[d])));

            if (const DragonPart* p = t.get_dragon()) {
                if (p->get_id() == id_) {
                    if (!p->is_head()) ownBody_.push_back(p);
                    continue;
                }
                blocked_[i] = 1;
                const bool ally = (p->get_team() == ct_.get_team());
                if (!ally) {
                    ++enemyParts;
                    enemyOcc_[i] = 1;
                    enemySegs_[p->get_id()]++;
                    if (p->get_id() < 2) eqNow.push_back({i, p->get_id()});
                }
                if (p->is_head()) {
                    const HeadInfo h{i, p->get_id(), dir_index(p->get_dir())};
                    if (ally) {
                        allyHeads_.push_back(h);
                        if (p->get_id() < 2) {
                            ourQueenTile_ = i;
                            queenPos_ = i;
                            queenRound_ = round_;
                            sawQueen = true;
                        }
                    } else {
                        enemyHeads_.push_back(h);
                        enemyMem_.push_back({i, round_});
                        if (p->get_id() < 2) {
                            eqPos_ = i;
                            eqRound_ = round_;
                        }
                    }
                }
            }
        }

        while (enemyMem_.size() > 40) enemyMem_.erase(enemyMem_.begin());
        idle_ = (enemyParts == 0);

        if (!eqPrev_.empty()) {
            for (int qid = 0; qid < 2; ++qid) {
                int count = 0;
                bool allVis = true, stillThere = false;
                for (const auto& [tile, pid] : eqPrev_) {
                    if (pid != qid) continue;
                    ++count;
                    if (!in_window(tile)) allVis = false;
                }
                for (const auto& [tile, pid] : eqNow) if (pid == qid) stillThere = true;
                if (count >= 3 && allVis && !stillThere && !eqDead_) {
                    eqDead_ = true;
                }
            }
        }
        if (!eqNow.empty()) eqDead_ = false;
        eqPrev_ = eqNow;

        build_body_chain();
        compute_need();

        pearlHistory_ = static_cast<uint8_t>((pearlHistory_ << 1) | (pearl_[headIdx_] ? 1 : 0));

        Role curRole = active_role();
        if (!queen_) {
            if (sawQueen) {
                if (curRole != Role::GUARD) roleStack_.push_back(Role::GUARD);
            } else if (sawBishop && curRole != Role::BISHOP) {
                if (curRole != Role::GUARD) roleStack_.push_back(Role::GUARD);
            } else if (curRole == Role::EXPLORER) {
                int clusterScore = visiblePearls;
                if (__builtin_popcount(pearlHistory_) >= 5 || clusterScore >= 4) {
                    if (len_ > 12) {
                        bishopClusterTile_ = headIdx_;
                        roleStack_.push_back(Role::BISHOP);
                        turnsAsBishop_ = 0;
                    }
                }
            }
        }
    }

    void build_body_chain() {
        chain_.clear();
        chain_.push_back(headIdx_);
        bodyIdx_[headIdx_] = 0;
        std::vector<char> used(ownBody_.size(), 0);
        int cur = headIdx_;
        while (true) {
            int found = -1;
            for (size_t k = 0; k < ownBody_.size(); ++k) {
                if (used[k]) continue;
                const int bi = idx(ownBody_[k]->get_position());
                if (nbr_[bi * 4 + dir_index(ownBody_[k]->get_dir())] == cur) {
                    found = static_cast<int>(k);
                    break;
                }
            }
            if (found < 0) break;
            used[found] = 1;
            cur = idx(ownBody_[found]->get_position());
            bodyIdx_[cur] = static_cast<int>(chain_.size());
            chain_.push_back(cur);
        }
        for (size_t k = 0; k < ownBody_.size(); ++k) {
            if (!used[k]) bodyIdx_[idx(ownBody_[k]->get_position())] = BODY_STRAY;
        }

        bool aligned = !trail_.empty() && trail_.front() == headIdx_;
        while (static_cast<int>(trail_.size()) > len_) trail_.pop_back();
        for (size_t i = 0; aligned && i < chain_.size(); ++i) {
            if (i >= trail_.size() || trail_[i] != chain_[i]) aligned = false;
        }
        if (!aligned) trail_.assign(chain_.begin(), chain_.end());
        while (static_cast<int>(trail_.size()) > len_) trail_.pop_back();
        for (size_t i = chain_.size(); i < trail_.size(); ++i) {
            const int t = trail_[i];
            if (lastSeen_[t] != round_ && bodyIdx_[t] == BODY_NONE) bodyIdx_[t] = static_cast<int>(i);
        }
    }

    void compute_need() {
        const int cap = std::max(N_ / 2, 4);
        strict_ = queen_ && (round_ >= cfg::ASSAULT_ROUND || eqDead_);
        need_ = std::min({len_ + (queen_ ? cfg::QUEEN_AREA_MARGIN : cfg::AREA_MARGIN), cfg::AREA_CAP, cap});
        if (strict_) need_ = std::min({2 * len_ + cfg::QUEEN_AREA_MARGIN, cfg::AREA_CAP * 2, cap});
        if (orientation_) need_ = std::min({2 * len_ + cfg::AREA_MARGIN, cfg::AREA_CAP, cap});
    }

    void queue_sonar(int d, uint64_t m, int prio) {
        if (d < 0 || d > 3 || prio < sonarPrio_[d]) return;
        sonarMsg_[d] = m;
        sonarPrio_[d] = prio;
    }

    void queue_all(uint64_t m, int prio) {
        for (int d = 0; d < 4; ++d) queue_sonar(d, m, prio);
    }

    void flush_sonar() {
        for (int d = 0; d < 4; ++d) {
            if (sonarPrio_[d] != 0) ct_.send_sonar(all_dirs()[d], sonarMsg_[d]);
        }
        sonarMsg_.fill(0);
        sonarPrio_.fill(0);
    }

    void read_sonar() {
        alert_ = false;
        forbidDir_ = -1;
        pincerTarget_ = -1;
        pincerFlank_ = -1;
        pearlSignalTile_ = -1;

        const unswbc::SonarEchoes echoes = ct_.get_sonar_echoes();
        if (queen_ && scoutDir_ >= 0) {
            if (echoes.enemy > 0 || echoes.enemy_head > 0) {
                forbidDir_ = scoutDir_;
                alert_ = true;
            }
            scoutDir_ = -1;
        }
        if (alert_) compute_need();

        for (uint64_t m : ct_.get_sonar_messages()) {
            sonar::ActionId act;
            uint8_t d8;
            int x, y;
            uint8_t extra;
            if (!sonar::unpack(m, act, d8, x, y, extra)) continue;

            if (round_ >= cfg::RESCUE_START && round_ <= cfg::RESCUE_END) {
                ++signalsReceivedInRescue_;
            }

            const int tile = (x < W_ && y < H_) ? y * W_ + x : -1;
            switch (act) {
            case sonar::ActionId::ACT_BEACON:
                if (tile >= 0) {
                    if (extra == 1) {
                        queenPos_ = tile;
                        queenRound_ = round_;
                    } else {
                        bishopPos_ = tile;
                        bishopRound_ = round_;
                    }
                }
                break;
            case sonar::ActionId::ACT_SPLIT_VECTOR:
                if (firstRound_ >= round_ - 1) {
                    heading_ = static_cast<Compass8>(d8 & 7);
                }
                break;
            case sonar::ActionId::ACT_PINCER_CALL:
                if (tile >= 0 && !queen_) {
                    pincerTarget_ = tile;
                    int fdir = (d8 + 2) & 7;
                    Vector2D fvec = COMPASS_VECTORS[fdir];
                    pincerFlank_ = ((y + fvec.dy + H_) % H_) * W_ + (x + fvec.dx + W_) % W_;
                    if (wrap_dist(headIdx_, tile) <= 8 && active_role() != Role::PINCER_SPOTTER) {
                        roleStack_.push_back(Role::PINCER_HELPER);
                    }
                }
                break;
            case sonar::ActionId::ACT_PEARL_DROP:
                if (tile >= 0) {
                    if (queen_) {
                        push_move(cfg::W_PEARL_EAT + 2000, {tile}, 0, F_NONE, "Queen Consume Dropped Pearls");
                    }
                    if (!queen_ && active_role() == Role::BREEDER) {
                        pearlSignalTile_ = tile;
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    double threat(int tile) const {
        double worst = 0;
        for (const HeadInfo& h : enemyHeads_) {
            const int d = wrap_dist(h.tile, tile);
            if (d == 1) worst = std::max(worst, queen_ ? 100000.0 : 40.0);
            else if (d == 2) worst = std::max(worst, queen_ ? 5000.0 : 8.0);
            else if (d == 3) worst = std::max(worst, queen_ ? 1000.0 : 2.0);
        }
        if (queen_) {
            for (const auto& [t, r] : enemyMem_) {
                if (round_ - r > 6) continue;
                const int d = wrap_dist(t, tile);
                if (d <= 2) worst = std::max(worst, 2500.0);
            }
        }
        return worst;
    }

    int flood_area(int start, int startDepth, int cap, bool strict, const std::vector<int>* extraBlocked = nullptr) {
        if (extraBlocked) {
            for (int t : *extraBlocked) extra_[t] = 1;
        }
        ++visStamp_;
        queue_.clear();
        queue_.push_back(start);
        vis_[start] = visStamp_;
        fd_[start] = startDepth;
        int count = 0;
        int result = -1;
        for (size_t h = 0; h < queue_.size(); ++h) {
            const int cur = queue_[h];
            ++count;
            if (count >= cap || (!strict && lastSeen_[cur] < 0)) {
                result = cap;
                break;
            }
            for (int d = 0; d < 4; ++d) {
                if (!can_enter(cur, d, fd_[cur] + 1)) continue;
                const int nx = nbr_[cur * 4 + d];
                if (vis_[nx] == visStamp_ || extra_[nx]) continue;
                vis_[nx] = visStamp_;
                fd_[nx] = fd_[cur] + 1;
                queue_.push_back(nx);
            }
        }
        if (result < 0) result = count;
        if (extraBlocked) {
            for (int t : *extraBlocked) extra_[t] = 0;
        }
        return result;
    }

    int step_area(int d) {
        if (areaStamp_[d] == turnStamp_) return areaCache_[d];
        const Move& m = mv_[d];
        int a = 0;
        if (!m.fatal && !m.portal) a = flood_area(m.dest, 1, need_, strict_);
        areaCache_[d] = a;
        areaStamp_[d] = turnStamp_;
        return a;
    }

    void bfs_window() {
        std::fill(dist_.begin(), dist_.end(), -1);
        queue_.clear();
        dist_[headIdx_] = 0;
        queue_.push_back(headIdx_);
        for (size_t h = 0; h < queue_.size(); ++h) {
            const int cur = queue_[h];
            for (int d = 0; d < 4; ++d) {
                if (!can_enter(cur, d, dist_[cur] + 1)) continue;
                const int nx = nbr_[cur * 4 + d];
                if (dist_[nx] != -1 || !in_window(nx)) continue;
                dist_[nx] = dist_[cur] + 1;
                par_[nx] = cur;
                parDir_[nx] = d;
                queue_.push_back(nx);
            }
        }
    }

    std::vector<int> path_to(int tile) const {
        std::vector<int> dirs;
        for (int cur = tile; cur != headIdx_ && cur >= 0; cur = par_[cur]) {
            dirs.push_back(parDir_[cur]);
        }
        std::reverse(dirs.begin(), dirs.end());
        return dirs;
    }

    int free_steps() const { return (len_ + 3) / 4; }
    int lunge_reach() const { return std::min(3, free_steps() + std::max(0, len_ - 2)); }

    bool lunge_steps(int target, std::vector<int>& steps) const {
        int bestN = -1, bestTotal = 1000, bestDir = -1;
        for (int e = 0; e < 4; ++e) {
            const int n = nbr_[target * 4 + e];
            const int toward = opposite(e);
            const uint8_t ed = edge_[n * 4 + toward];
            if (ed == EDGE_KELP || ed == EDGE_PORTAL) continue;
            if (n != headIdx_ && dist_[n] <= 0) continue;
            const int total = dist_[n] + 1;
            if (total < bestTotal) {
                bestTotal = total;
                bestN = n;
                bestDir = toward;
            }
        }
        if (bestN < 0 || bestTotal > lunge_reach()) return false;
        steps = path_to(bestN);
        steps.push_back(bestDir);
        return true;
    }

    bool sprint_ok(const std::vector<int>& steps, bool ignoreThreat = false) {
        int pos = headIdx_;
        int eaten = 0;
        std::vector<int> visited;
        for (size_t s = 0; s < steps.size(); ++s) {
            const int t = static_cast<int>(s) + 1;
            if (!can_enter(pos, steps[s], t, eaten)) return false;
            const int to = nbr_[pos * 4 + steps[s]];
            if (std::find(visited.begin(), visited.end(), to) != visited.end()) return false;
            visited.push_back(to);
            if (pearl_[to] && lastSeen_[to] == round_) ++eaten;
            pos = to;
        }
        if (!ignoreThreat && threat(pos) >= (queen_ ? 1000.0 : 40.0)) return false;
        return flood_area(pos, static_cast<int>(steps.size()), need_, strict_, &visited) >= need_;
    }

    int window_tile_toward(int goal) const {
        int best = -1;
        double bc = 1e18;
        for (int t = 0; t < N_; ++t) {
            if (dist_[t] <= 0) continue;
            const double c = wrap_dist(t, goal) + 0.1 * dist_[t];
            if (c < bc) {
                bc = c;
                best = t;
            }
        }
        return best;
    }

    void update_body_tracking() {
        relBodyQueue_.clear();
        int hx = headIdx_ % W_;
        int hy = headIdx_ / W_;
        for (int i = 0; i < len_ && i < static_cast<int>(chain_.size()); ++i) {
            int segTile = chain_[i];
            int sx = segTile % W_;
            int sy = segTile / W_;
            int dx = sx - hx;
            int dy = sy - hy;
            if (dx > W_ / 2) dx -= W_;
            else if (dx < -W_ / 2) dx += W_;
            if (dy > H_ / 2) dy -= H_;
            else if (dy < -H_ / 2) dy += H_;
            relBodyQueue_.push_back({dx, dy});
        }
    }

    std::vector<int> build_sprint_path(int targetTile) {
        int fs = free_steps();
        if (fs <= 1 || dist_[targetTile] <= 1) {
            return {targetTile};
        }
        std::vector<int> path;
        int curr = targetTile;
        while (dist_[curr] > 0) {
            path.push_back(curr);
            bool foundPrev = false;
            for (int d = 0; d < 4; ++d) {
                int nx = nbr_[curr * 4 + d];
                if (dist_[nx] == dist_[curr] - 1) {
                    curr = nx;
                    foundPrev = true;
                    break;
                }
            }
            if (!foundPrev) break;
        }
        std::reverse(path.begin(), path.end());
        if (static_cast<int>(path.size()) > fs) {
            path.resize(fs);
        }
        return path;
    }

    void push_move(double w, std::vector<int> dests, int minLen, unsigned flags, const char* tag) {
        Action a;
        a.kind = Action::MOVE;
        a.weight = w;
        a.dests = std::move(dests);
        a.minLen = minLen;
        a.flags = flags;
        a.tag = tag;
        heap_.push(std::move(a));
    }
    
    void push_sprint_move(double weight, int targetTile, unsigned flags = 0, const char* logMsg = "") {
        std::vector<int> sprintPath = build_sprint_path(targetTile);
        if (sprintPath.size() > 1) {
            flags |= F_LUNGE;
        }
        push_move(weight, {targetTile}, 0, flags, logMsg);
    }

    void push_split(double w, int parentLen, int childLen, unsigned flags, const char* tag, std::vector<SonarOrder> exc = {}) {
        Action a;
        a.kind = Action::SPLIT;
        a.weight = w;
        a.parentLen = parentLen;
        a.childLen = childLen;
        a.flags = flags;
        a.tag = tag;
        a.exc = std::move(exc);
        heap_.push(std::move(a));
    }

    void s_queen_opening() {
        if (!queen_ || round_ > 2) return;
        int safe = 0;
        for (int d = 0; d < 4; ++d) {
            if (!mv_[d].fatal && !mv_[d].portal) ++safe;
        }
        if (safe == 0 && ct_.can_split(len_ - 7)) {
            push_split(cfg::W_QUEEN_UNCOIL, 7, len_ - 7, F_NO_TAILCHECK, "P1 Queen Uncoil keep 7");
        }
    }

    void s_queen_emergency() {
        if (!queen_) return;
        for (const HeadInfo& e : enemyHeads_) {
            if (cheb_dist(headIdx_, e.tile) <= cfg::FLEE_TRIGGER) {
                for (int d = 0; d < 4; ++d) {
                    if (!mv_[d].fatal && !mv_[d].portal && threat(mv_[d].dest) < 1000.0) {
                        push_move(cfg::W_QUEEN_EMERGENCY + step_area(d), {mv_[d].dest}, 0, F_FLEE_SPRINT | F_NO_THREAT, "P2 Queen Emergency Evade");
                    }
                }
            }
        }
    }

    void s_guard_combat() {
        if (active_role() != Role::GUARD || queen_) return;
        int targetQ = ourQueenTile_ >= 0 ? ourQueenTile_ : queenPos_;
        if (targetQ < 0) return;

        for (const HeadInfo& e : enemyHeads_) {
            if (cheb_dist(targetQ, e.tile) <= 3) {
                std::vector<int> steps;
                if (lunge_steps(e.tile, steps)) {
                    push_move(cfg::W_GUARD_KAMIKAZE, {e.tile}, 0, F_LUNGE | F_NO_AREA | F_NO_THREAT, "P3 Guard Kamikaze");
                }
            }
        }
    }

    void s_wrap_trap() {
        if (queen_) return;
        int fs = free_steps();
        if (fs < 2) return;
        for (const HeadInfo& e : enemyHeads_) {
            if (cheb_dist(headIdx_, e.tile) > fs + 1) continue;
            
            int eLen = 0;
            auto it = enemySegs_.find(e.id);
            if (it != enemySegs_.end()) eLen = it->second;

            Vector2D fwd = COMPASS_VECTORS[e.dir * 2];
            int front1 = ((e.tile / W_ + fwd.dy + H_) % H_) * W_ + (e.tile % W_ + fwd.dx + W_) % W_;
            int front2 = ((front1 / W_ + fwd.dy + H_) % H_) * W_ + (front1 % W_ + fwd.dx + W_) % W_;
            int flankLeft = nbr_[front1 * 4 + (e.dir + 3) % 4];
            int flankRight = nbr_[front1 * 4 + (e.dir + 1) % 4];

            std::vector<int> chokePoints;
            if (!blocked_[front2] && edge_[front1 * 4 + e.dir] != EDGE_KELP) chokePoints.push_back(front2);
            if (!blocked_[flankLeft]) chokePoints.push_back(flankLeft);
            if (!blocked_[flankRight]) chokePoints.push_back(flankRight);
            if (!blocked_[front1]) chokePoints.push_back(front1);

            if (len_ > eLen + 2) {
                for (int targetTile : chokePoints) {
                    if (dist_[targetTile] > 0 && dist_[targetTile] <= fs) {
                        double w = cfg::W_K1_ASSASSIN + 3000.0 - (dist_[targetTile] * 50.0);
                        push_move(w, {targetTile}, 0, F_LUNGE | F_NO_AREA | F_NO_THREAT, "High-Speed Sprint Wrap");
                    }
                }
            }
        }
    }

    void s_pincer() {
        if (queen_) return;
        for (const HeadInfo& e : enemyHeads_) {
            int eLen = enemySegs_[e.id];
            if (eLen > len_ + 3) {
                Vector2D fvec = COMPASS_VECTORS[e.dir * 2];
                int eForward = ((e.tile / W_ + fvec.dy + H_) % H_) * W_ + (e.tile % W_ + fvec.dx + W_) % W_;
                if (in_window(eForward) && dist_[eForward] > 0) {
                    Action a;
                    a.kind = Action::MOVE;
                    a.weight = cfg::W_PINCER_SPOTTER;
                    a.dests = {eForward};
                    a.flags = F_LUNGE | F_NO_AREA;
                    a.tag = "P4 Pincer Spotter Pin";
                    SonarOrder so{opposite(facing_), sonar::ActionId::ACT_PINCER_CALL, static_cast<uint8_t>(e.dir * 2), e.tile % W_, e.tile / W_, static_cast<uint8_t>(eLen)};
                    a.exc.push_back(so);
                    heap_.push(std::move(a));
                }
            }
        }

        if (active_role() == Role::PINCER_HELPER && pincerFlank_ >= 0) {
            if (in_window(pincerFlank_) && dist_[pincerFlank_] > 0) {
                push_move(cfg::W_PINCER_HELPER, {pincerFlank_}, 0, F_NONE, "P6 Pincer Helper Flank");
            }
        }
    }

    void s_pearls() {
        for (int t = 0; t < N_; ++t) {
            if (dist_[t] <= 0) continue;
            if (pearl_[t]) {
                double w = cfg::W_PEARL_EAT - 5.0 * dist_[t];
                int ty = t / W_;
                if (ty <= 2 || ty >= H_ - 3) w += 100.0;
                if (!queen_ && ourQueenTile_ >= 0 && wrap_dist(t, ourQueenTile_) <= 2) continue;
                push_sprint_move(w, t, F_NONE, "P5 Pearl Eat (Sprint)");
            }
        }
    }

    void s_breeder_split() {
        if (queen_) {
            if (round_ < 200 && len_ > 13 && ct_.can_split(4)) {
                push_split(cfg::W_BREEDER_SPLIT + 2000, len_ - 4, 4, F_NONE, "Queen Early Aggressive Split");
            }
            return;
        }

        if (active_role() != Role::BREEDER) return;

        if (len_ > 1) {
            int childLen = std::max(1, len_ / 2); 
            if (ct_.can_split(childLen)) {
                push_split(cfg::W_BREEDER_SPLIT, len_ - childLen, childLen, F_NONE, "Breeder Aggressive Split");
            }
        }

        if (pearlSignalTile_ >= 0) {
            if (dist_[pearlSignalTile_] > 0) {
                push_sprint_move(cfg::W_PEARL_EAT + 1500, pearlSignalTile_, F_NONE, "Breeder Direct Chase Pearl Signal");
            } else {
                int targetTile = window_tile_toward(pearlSignalTile_);
                if (targetTile >= 0) {
                    push_sprint_move(cfg::W_PEARL_EAT + 800, targetTile, F_NONE, "Breeder Navigate to Pearl Signal");
                }
            }
        }
    }

    void s_bishop_control() {
        if (active_role() != Role::BISHOP || queen_) return;
        ++turnsAsBishop_;

        if (turnsAsBishop_ % 3 == 0) {
            Position hp = pos_of(headIdx_);
            queue_all(sonar::pack(sonar::ActionId::ACT_BEACON, static_cast<uint8_t>(heading_), hp.x, hp.y, 2), 3);
        }

        for (const HeadInfo& e : enemyHeads_) {
            if (enemySegs_[e.id] <= len_) {
                std::vector<int> steps;
                if (lunge_steps(e.tile, steps)) {
                    push_move(cfg::W_BISHOP_PATROL + 500, {e.tile}, 0, F_LUNGE | F_NO_AREA, "Bishop Attack Head");
                }
            }
        }

        if (bishopClusterTile_ >= 0) {
            int d = wrap_dist(headIdx_, bishopClusterTile_);
            if (d > bishopR_) {
                int t = window_tile_toward(bishopClusterTile_);
                if (t >= 0) push_move(cfg::W_BISHOP_STAKE, {t}, 0, F_NONE, "Bishop Retract to Cluster");
            } else if (d < bishopR_ - 1) {
                int bestT = -1, maxD = -1;
                for (int t = 0; t < N_; ++t) {
                    if (dist_[t] > 0) {
                        int wd = wrap_dist(t, bishopClusterTile_);
                        if (wd > maxD && wd <= bishopR_) {
                            maxD = wd;
                            bestT = t;
                        }
                    }
                }
                if (bestT >= 0) push_move(cfg::W_BISHOP_PATROL, {bestT}, 0, F_NONE, "Bishop Clear Core");
            }
        }
    }

    void s_compass_motion() {
        Vector2D vec = COMPASS_VECTORS[static_cast<int>(heading_)];
        std::vector<int> candidates;

        if (vec.dx > 0) candidates.push_back(1);
        else if (vec.dx < 0) candidates.push_back(3);

        if (vec.dy > 0) candidates.push_back(2);
        else if (vec.dy < 0) candidates.push_back(0);

        bool hitV = false, hitH = false;
        for (int c : candidates) {
            if (mv_[c].kelp) {
                if (c == 1 || c == 3) hitV = true;
                if (c == 0 || c == 2) hitH = true;
            }
        }
        if (hitV || hitH) {
            heading_ = reflect_heading(heading_, hitV, hitH);
            vec = COMPASS_VECTORS[static_cast<int>(heading_)];
            candidates.clear();
            if (vec.dx > 0) candidates.push_back(1);
            else if (vec.dx < 0) candidates.push_back(3);
            if (vec.dy > 0) candidates.push_back(2);
            else if (vec.dy < 0) candidates.push_back(0);
        }

        if (candidates.size() == 2 && ((round_ + id_) & 1)) {
            std::swap(candidates[0], candidates[1]);
        }

        std::vector<int> dests;
        for (int c : candidates) {
            if (!mv_[c].fatal && !mv_[c].portal) dests.push_back(mv_[c].dest);
        }
        
        for (int destTile : dests) {
            push_sprint_move(cfg::W_COMPASS_MOVE, destTile, F_NONE, "P10 Compass Motion (Sprint)");
        }
    }

    void s_queen_space() {
        if (!queen_) return;
        for (int d = 0; d < 4; ++d) {
            const Move& m = mv_[d];
            if (m.fatal || m.portal) continue;
            double w = cfg::W_QUEEN_SPACE + step_area(d) * 0.8 - threat(m.dest) * 0.3;
            if (d == facing_) w += 5.0;
            push_sprint_move(w, m.dest, F_NONE, "Queen Open Space (Sprint)");
        }
    }

    void s_assault() {
        if (assault_) {
            for (const HeadInfo& e : enemyHeads_) {
                std::vector<int> steps;
                if (lunge_steps(e.tile, steps)) {
                    push_move(cfg::W_K1_ASSASSIN, {e.tile}, 0, F_LUNGE | F_NO_AREA | F_NO_THREAT, "Assault Lunge");
                }
            }
        }
    }

    bool resolve_split(const Action& a) {
        if (a.parentLen + a.childLen != len_) return false;
        if (!ct_.can_split(a.childLen)) return false;
        emit_split(a);
        return true;
    }

    bool resolve_move(const Action& a) {
        if (len_ < a.minLen) return false;
        if (a.flags & F_SUICIDE) {
            emit_move({opposite(facing_)}, a.tag, -1);
            return true;
        }
        if (a.flags & F_LUNGE) {
            if (a.dests.empty()) return false;
            std::vector<int> steps;
            if (!lunge_steps(a.dests[0], steps)) return false;
            emit_move(steps, a.tag, a.dests[0]);
            return true;
        }

        int bestT = -1, bestD = 1000000;
        for (int t : a.dests) {
            if (t >= 0 && dist_[t] > 0 && dist_[t] < bestD) {
                bestD = dist_[t];
                bestT = t;
            }
        }
        if (bestT < 0) return false;
        const std::vector<int> path = path_to(bestT);
        if (path.empty()) return false;
        const int d0 = path[0];
        const Move& m = mv_[d0];
        if (m.fatal || m.portal) return false;
        if (queen_ && forbidDir_ >= 0 && d0 == forbidDir_) return false;
        if (!(a.flags & F_NO_AREA) && step_area(d0) < need_) return false;
        if (!(a.flags & F_NO_THREAT) && threat(m.dest) >= (queen_ ? 1000.0 : 40.0)) return false;

        std::vector<int> steps{d0};
        if (!(a.flags & F_NO_SPRINT) && free_steps() >= 2) {
            const int maxSteps = std::min({free_steps(), cfg::SPRINT_MAX_STEPS, static_cast<int>(path.size())});
            for (int k = maxSteps; k >= 2; --k) {
                const std::vector<int> cand(path.begin(), path.begin() + k);
                if (sprint_ok(cand, (a.flags & F_NO_THREAT) != 0)) {
                    steps = cand;
                    break;
                }
            }
        }

        const int nextDir = path.size() > steps.size() ? path[steps.size()] : steps.back();
        emit_move(steps, a.tag, bestT, nextDir);
        return true;
    }

    void emit_move(const std::vector<int>& steps, const char* why, int target, int nextDir = -1) {
        if (steps.size() == 1) {
            ct_.make_move(all_dirs()[steps[0]]);
        } else {
            std::vector<Direction> v;
            for (int d : steps) v.push_back(all_dirs()[d]);
            ct_.make_moves(v);
        }

        int pos = headIdx_;
        bool portal = false;
        for (int d : steps) {
            if (edge_[pos * 4 + d] == EDGE_PORTAL) portal = true;
            pos = nbr_[pos * 4 + d];
            if (!portal) trail_.push_front(pos);
        }
        if (portal) trail_.clear();

        if (queen_) {
            Position hp = pos_of(pos);
            queue_all(sonar::pack(sonar::ActionId::ACT_BEACON, static_cast<uint8_t>(heading_), hp.x, hp.y, 1), 4);
            scoutDir_ = nextDir >= 0 ? nextDir : steps.back();
        }
        flush_sonar();
    }

    void emit_split(const Action& a) {
        ct_.do_split(a.childLen);
        lastSplitRound_ = round_;
        if (a.flags & F_QUEEN_SPREAD) ++queenSpreadSplits_;
        scoutDir_ = -1;

        Position hp = pos_of(headIdx_);
        for (const SonarOrder& o : a.exc) {
            queue_sonar(o.dir, sonar::pack(o.act, o.dir8, o.x, o.y, o.extra), 5);
        }
        flush_sonar();
    }

    void classify_moves() {
        const Position hp = ct_.get_position();
        const Tile* here = ct_.get_tile(hp);
        for (int d = 0; d < 4; ++d) {
            Move& m = mv_[d];
            m = Move();
            m.d = d;
            m.dest = nbr_[headIdx_ * 4 + d];
            const uint8_t code = here ? edge_code(here->get_edge(all_dirs()[d])) : edge_[headIdx_ * 4 + d];
            if (code == EDGE_KELP) {
                m.fatal = true;
                m.kelp = true;
                continue;
            }
            if (code == EDGE_PORTAL) {
                m.portal = true;
                if (queen_) m.fatal = true;
                continue;
            }
            const Tile* ahead = ct_.get_tile(hp.add_dir(all_dirs()[d]));
            const DragonPart* part = ahead ? ahead->get_dragon() : nullptr;
            if (part) {
                m.fatal = true;
                if (part->is_head() && part->get_id() != id_) {
                    m.headOn = true;
                    if (!(part->get_team() == ct_.get_team())) {
                        m.enemyHead = true;
                        if (part->get_id() < 2) m.enemyQueenHead = true;
                    }
                }
            }
        }
    }

    bool act() {
        update_body_tracking(); 
        classify_moves();
        bfs_window();
        heap_ = std::priority_queue<Action>();

        s_queen_opening();
        s_queen_emergency();
        s_guard_combat();
        s_wrap_trap();      
        s_pincer();
        s_pearls();
        s_breeder_split();  
        s_bishop_control();
        s_compass_motion(); 
        s_queen_space();
        s_assault();

        while (!heap_.empty()) {
            const Action a = heap_.top();
            heap_.pop();
            if (a.kind == Action::SPLIT ? resolve_split(a) : resolve_move(a)) return true;
        }
        return fallback_chain();
    }

    bool fallback_chain() {
        int best = -1;
        double bs = -1e18;
        for (int d = 0; d < 4; ++d) {
            if (d == opposite(facing_)) continue;
            const Move& m = mv_[d];
            if (m.fatal) continue;
            const double s = m.portal ? -5000.0 : std::min(step_area(d), need_) * 1.0 - 2.0 * threat(m.dest) + (d == facing_ ? 1.5 : 0.0);
            if (s > bs) {
                bs = s;
                best = d;
            }
        }
        if (best >= 0) {
            emit_move({best}, "fallback", -1);
            return true;
        }
        const int child = queen_ ? 2 : len_ - 2;
        if (len_ >= 4 && ct_.can_split(child)) {
            Action a;
            a.kind = Action::SPLIT;
            a.childLen = child;
            a.parentLen = len_ - child;
            a.tag = "fallback split";
            emit_split(a);
            return true;
        }
        for (const Move& m : mv_) {
            if (m.enemyHead) {
                emit_move({m.d}, "fallback ram enemy head", -1);
                return true;
            }
        }
        for (const Move& m : mv_) {
            if (!m.headOn) {
                emit_move({m.d}, "fallback death", -1);
                return true;
            }
        }
        emit_move({0}, "fallback fatal", -1);
        return true;
    }

    void simple_fallback() {
        try {
            flush_sonar();
            const Position hp = ct_.get_position();
            const Tile* here = ct_.get_tile(hp);
            for (const Direction d : all_dirs()) {
                if (here && !here->get_edge(d).is_passable()) continue;
                const Tile* ahead = ct_.get_tile(hp.add_dir(d));
                if (ahead && ahead->get_dragon()) continue;
                ct_.make_move(d);
                return;
            }
        } catch (...) {
        }
        ct_.make_move(Direction::NORTH);
    }
};

}

int main() {
    auto [ct, game] = unswbc::init();
    Bot bot(ct, game);

    while (unswbc::update(ct, game)) {
        bot.play_turn();
        unswbc::end_turn();
    }
}