# Swarm Intelligence & Combat Architecture Specification
## Target Implementation: 

This document serves as the **final, authoritative engineering specification** for an AI Coding Agent to implement the next-generation bot for the UNSW Battlecode Dragon Game. It consolidates the Action-Heap architecture from `main3.cpp`, the Google Docs Swarm Intelligence design, and the user's specific strategic directives.

---

## 1. Executive Architecture Overview

The bot operates on a **Priority Action-Heap + Role-Stack + Encrypted Sonar** architecture:
1. **Per-Dragon Local Memory**: Operates under no-shared-memory constraints. Dragons maintain long-term memory of edges, permanent pearl beds, and body trails.
2. **Role Stack (`std::vector<Role> role_stack`)**: Instead of a static role, each dragon maintains a LIFO stack. Routine roles (`BREEDER`, `EXPLORER`, `BISHOP`) form the base. Emergent duties (`GUARD`, `PINCER_SPOTTER`, `PINCER_HELPER`) are pushed when triggered and popped upon completion.
3. **8-Compass Direction System (`Compass8`)**: Units navigate on 8 discrete vectors (N, NE, E, SE, S, SW, W, NW). Collisions with boundary edges cause geometric reflection (vertical edges invert horizontal vectors: $NE \leftrightarrow NW$; horizontal edges invert vertical vectors: $NE \leftrightarrow SE$).
4. **Action Max-Heap**: Independent strategy evaluators push candidate actions (`MOVE` or `SPLIT`) with weights and execution effects (`SonarOrder`). The resolver selects the highest-weight valid action with sprint verification.
5. **Authenticated 64-Bit Sonar Packet**: Unified packet encoding authentication, sender role, directive action, recommended direction, coordinates, and contextual payload.

---

## 2. Sonar Protocol & Bit-Packing Schema (64-Bit)

Every message sent through `ct.send_sonar(...)` must conform to the following 64-bit layout:

```
 63                  44 43        36 35    32 31    28 27     25 24     21 20      13 12       5 4     0
+----------------------+------------+--------+--------+---------+---------+----------+----------+-------+
|  Auth Tag (20 bits)  | Round (8b) | Role(4)| Act(4) | Dir8(3) | Rsvd(1) |   X (8b) |   Y (8b) |Extra(5)
+----------------------+------------+--------+--------+---------+---------+----------+----------+-------+
```

### 2.1 Bitfield Definitions

| Field | Bits | Range | Purpose |
|---|---|---|---|
| **Auth Tag** | $63 \dots 44$ (20b) | $0 \dots 2^{20}-1$ | SplitMix64 tag: `splitmix64(SECRET ^ round ^ role ^ action) >> 44` |
| **Round Stamp**| $43 \dots 36$ (8b) | $0 \dots 255$ | `round % 256` for replay rejection |
| **Broadcaster Role** | $35 \dots 32$ (4b) | $0 \dots 15$ | `0=QUEEN`, `1=BISHOP`, `2=GUARD`, `3=BREEDER`, `4=EXPLORER`, `5=PINCER` |
| **Action Directive** | $31 \dots 28$ (4b) | $0 \dots 15$ | `0=NONE`, `1=BEACON`, `2=RENDEZVOUS`, `3=SPLIT_VECTOR`, `4=PINCER_CALL`, `5=CLEAR_PATH`, `6=CLUSTER_ALERT` |
| **Direction to Go** | $27 \dots 25$ (3b) | $0 \dots 7$ | `Compass8` heading: `0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW` |
| **Reserved** | 24 (1b) | 0 | Reserved for future expansion |
| **Target X Coordinate** | $23 \dots 16$ (8b) | $0 \dots 255$ | Map X position of target/sender |
| **Target Y Coordinate** | $15 \dots 8$ (8b) | $0 \dots 255$ | Map Y position of target/sender |
| **Extra Payload** | $7 \dots 0$ (8b) | $0 \dots 255$ | Contextual data: pearl count, threat length, or cluster density |

### 2.2 Packet Helper Implementation
```cpp
namespace sonar {
enum RoleId : uint8_t { R_QUEEN = 0, R_BISHOP = 1, R_GUARD = 2, R_BREEDER = 3, R_EXPLORER = 4, R_PINCER = 5 };
enum ActionId : uint8_t { ACT_NONE = 0, ACT_BEACON = 1, ACT_RENDEZVOUS = 2, ACT_SPLIT_VECTOR = 3, ACT_PINCER_CALL = 4, ACT_CLEAR_PATH = 5, ACT_CLUSTER = 6 };

constexpr uint64_t SONAR_SECRET = 0x5EC12E7B0A7C0DE1ULL;

inline uint64_t compute_auth(int round, uint8_t role, uint8_t action) {
    uint64_t seed = SONAR_SECRET ^ (static_cast<uint64_t>(round) * 0x9E3779B97F4A7C15ULL)
                                ^ (static_cast<uint64_t>(role) << 12)
                                ^ (static_cast<uint64_t>(action) << 4);
    return splitmix64(seed) >> 44; // 20 bits
}

inline uint64_t pack(int round, RoleId role, ActionId act, uint8_t dir8, int x, int y, uint8_t extra = 0) {
    uint64_t auth = compute_auth(round, role, act);
    uint64_t pkt = 0;
    pkt |= (auth & 0xFFFFFULL) << 44;
    pkt |= (static_cast<uint64_t>(round & 0xFF)) << 36;
    pkt |= (static_cast<uint64_t>(role & 0xF)) << 32;
    pkt |= (static_cast<uint64_t>(act & 0xF)) << 28;
    pkt |= (static_cast<uint64_t>(dir8 & 0x7)) << 25;
    pkt |= (static_cast<uint64_t>(x & 0xFF)) << 16;
    pkt |= (static_cast<uint64_t>(y & 0xFF)) << 8;
    pkt |= static_cast<uint64_t>(extra & 0xFF);
    return pkt;
}

inline bool unpack(uint64_t pkt, int current_round, RoleId& role, ActionId& act, uint8_t& dir8, int& x, int& y, uint8_t& extra) {
    uint64_t auth = (pkt >> 44) & 0xFFFFFULL;
    int round_stamp = (pkt >> 36) & 0xFF;
    role = static_cast<RoleId>((pkt >> 32) & 0xF);
    act = static_cast<ActionId>((pkt >> 28) & 0xF);
    dir8 = (pkt >> 25) & 0x7;
    x = (pkt >> 16) & 0xFF;
    y = (pkt >> 8) & 0xFF;
    extra = pkt & 0xFF;

    // Check round window (within last 3 rounds accounting for 8-bit wrap)
    int round_diff = (current_round - round_stamp) & 0xFF;
    if (round_diff > 3) return false;

    // Verify auth tag
    int original_round = current_round - round_diff;
    if (auth != compute_auth(original_round, role, act)) return false;

    return true;
}
}
```

---

## 3. Role Hierarchy & Stack Architecture

Every bot drops the legacy static roles (`TRAPPER`, `SACRIFICE`, `ESCORT`) and implements a dynamic stack:
```cpp
std::vector<Role> role_stack;
```
The active role is always `role_stack.back()`.

```
        +-----------------------------------------------+
        |                   QUEEN (ID 0/1)              |
        | - Absolute priority. Never demoted.           |
        +-----------------------------------------------+
                               |
              +----------------+---------------+
              | Seen by any bot in 7x7?        |
              v                                v
    +-------------------+            +-------------------+
    |    GUARD (Queen)  |            |   GUARD (Bishop)  |
    | - Pincer / Ram    |            | - Orbit perimeter |
    | - Screen threats  |            | - Clear corridors |
    +-------------------+            +-------------------+
              ^                                ^
              |                                | Sees Bishop in 7x7
+-----------------------------+  Len > 12    +-------------------+
|      EXPLORER (ID 22..55)   | -----------> |      BISHOP       |
| - 8-dir search for pearls   |  Cluster > 4 | - Holds territory |
| - Bounce on kelp/edges      |              | - Beacon beacon   |
+-----------------------------+              +-------------------+
              ^
              | (Rounds 350-400: Received < 8 sonar signals)
              v
+-----------------------------+
|      BREEDER (ID < 22)      |
| - 8-dir movement            |
| - Splits whenever Len > 5   |
| - Hands off opposite quad   |
+-----------------------------+
```

### 3.1 Role Descriptions

#### 1. QUEEN (`ID 0` or `ID 1`)
- **Initialization**: Fixed at spawn. Never pushed or popped.
- **Round 0 Opening Uncoil**: If coiled with 0 safe moves, splits `child = len - 7`, keeping 7 segments for immediate 2 free sprint steps.
- **Rules**:
  - **NEVER enters portals** (`m.portal = false; m.fatal = true;`).
  - **Enemy Head Safety**: Distance 1 is $+100,000$ threat penalty; Distance 2 is $+5,000$ penalty.
  - Broadcasts `ACT_BEACON` on omnidirectional sonar every round.
  - Scans ahead with 4 sonar pings and retreats if `echoes.enemy_head > 0`.

#### 2. BREEDER (`ID < 22`, excluding Queen)
- **Base Direction**: Assigned `id % 8` at birth.
- **Behavior**: Moves along 8-way vector, dodging kelp and harvesting path pearls.
- **Splitting Rule**: The moment `length > 5`, it splits immediately (`ct.do_split(2)`).
- **Quadrant Command**: Checks 3 forward quadrants in vision for pearls. If pearls are found in forward-left, commands the spawned child via `ACT_SPLIT_VECTOR` to take forward-right, and vice versa.

#### 3. EXPLORER (`ID 22` to `55`)
- **Base Direction**: Assigned `id % 8` at birth.
- **Objective**: Range out across the map toward unclaimed territory.
- **Bishop Promotion Check**:
  - Accumulates `pearl_history` bitfield: `__builtin_popcount(pearl_history) >= 5` over 8 turns OR local 7x7 window counts $\ge 4$ pearls.
  - If pearl cluster is detected AND `length > 12`: **Promote to BISHOP** (`role_stack.push_back(ROLE_BISHOP)`).

#### 4. BISHOP
- **Objective**: Anchor and dominate a high-yield pearl cluster.
- **Territory Control**: Establishes a breathing guard perimeter:
  $$R = \text{clamp}\left(2 + \left\lfloor \frac{\text{pearls} + \text{bots}}{3} \right\rfloor, 3, 9\right)$$
- **Sonar Beacon**: Emits `ACT_BEACON` every 3 rounds to guide idle explorers to the cluster.
- **Combat**: Attacks nearby enemy heads if `L_self >= L_enemy`.
- **Queen Preemption**: If a Bishop sees the Queen in its 7x7 vision, the Bishop yields its territory, pushes `ROLE_GUARD`, and screens the Queen!

#### 5. GUARD
- **Trigger**:
  - Any normal bot (`BREEDER`, `EXPLORER`) that sees the Queen or a Bishop in its 7x7 vision pushes `ROLE_GUARD`.
  - A Bishop that sees the Queen pushes `ROLE_GUARD`.
- **Defense Rules**:
  - If an enemy head is spotted within Chebyshev distance $\le 3$ of the Queen:
    - **Lunge/Kamikaze**: Sprints head-on directly into the enemy head to kill both dragons.
    - **Pincer Trap**: Spots forward vector and coordinates with teammate to seal flank.
    - **Wrap**: Sprints around enemy head to cut off exits.

---

## 4. 8-Compass Motion & Geometric Edge Reflection

### 4.1 8-Compass Direction System
```cpp
enum class Compass8 : uint8_t {
    N = 0, NE = 1, E = 2, SE = 3, S = 4, SW = 5, W = 6, NW = 7
};

struct Vector2D { int dx; int dy; };

constexpr Vector2D COMPASS_VECTORS[8] = {
    {0, -1},  // N  (y-up is negative in helper.hpp coords)
    {1, -1},  // NE
    {1, 0},   // E
    {1, 1},   // SE
    {0, 1},   // S
    {-1, 1},  // SW
    {-1, 0},  // W
    {-1, -1}  // NW
};
```

### 4.2 Step Decomposition
Because the game engine only allows 4 cardinal moves (`Direction::NORTH, EAST, SOUTH, WEST`), an 8-way vector is executed by alternating cardinal components based on round parity and local passability:
- For diagonal headings (e.g. `NE`): Candidate moves are `NORTH` and `EAST`.
- The bot evaluates both: prioritizes whichever collects pearls or has superior flood-fill exit area.

### 4.3 Kelp Wall & Boundary Reflection
When moving toward a boundary or kelp barrier, the bot reflects its vector:
```cpp
inline Compass8 reflect_heading(Compass8 heading, bool hit_vertical_edge, bool hit_horizontal_edge) {
    int h = static_cast<int>(heading);
    // Vertical edge (West/East barrier): Invert horizontal component (dx -> -dx)
    if (hit_vertical_edge) {
        switch (heading) {
            case Compass8::NE: return Compass8::NW;
            case Compass8::NW: return Compass8::NE;
            case Compass8::SE: return Compass8::SW;
            case Compass8::SW: return Compass8::SE;
            case Compass8::E:  return Compass8::W;
            case Compass8::W:  return Compass8::E;
            default: break;
        }
    }
    // Horizontal edge (North/South barrier): Invert vertical component (dy -> -dy)
    if (hit_horizontal_edge) {
        switch (heading) {
            case Compass8::NE: return Compass8::SE;
            case Compass8::SE: return Compass8::NE;
            case Compass8::NW: return Compass8::SW;
            case Compass8::SW: return Compass8::NW;
            case Compass8::N:  return Compass8::S;
            case Compass8::S:  return Compass8::N;
            default: break;
        }
    }
    return heading;
}
```

---

## 5. Quadrant Sensing & Asymmetric Child Splitting

When a `BREEDER` or `EXPLORER` splits:
1. Divide the 7x7 vision into 4 quadrants relative to the current heading vector:
   - **Forward-Left Quadrant**
   - **Forward-Center**
   - **Forward-Right Quadrant**
   - **Rear Quadrants**
2. Count pearls in Forward-Left vs Forward-Right:
   - If Forward-Left has more pearls, Parent steers Forward-Left, and Parent sends a Sonar packet with `ACT_SPLIT_VECTOR` commanding Child to take the Forward-Right heading.
   - If Forward-Right has more pearls, Parent steers Forward-Right, Child is assigned Forward-Left.
3. **Child Heading Reception**: Newly spawned child reads `ACT_SPLIT_VECTOR` in round 0 of its existence to initialize its heading vector.

---

## 6. Sacrificial Pincer Combat Protocol

When an enemy dragon enters local vision with length $L_{\text{enemy}} > L_{\text{self}} + 3$:

```
        Turn 1: Spotter pins forward tile <=== Enemy Dragon
                                                |
        Turn 2: Spotter dies; Enemy blocked     v
                Helper sprints to flank tile ===+
                                                |
        Turn 3: Enemy collides with Helper body -> Corpse pearls spawn!
```

1. **Spotter (`ROLE_PINCER_SPOTTER`)**:
   - Disables normal self-preservation safety weights.
   - Sprints directly onto the enemy head's predicted forward tile (`enemy_head + enemy_dir`).
   - Broadcasts `ACT_PINCER_CALL` with enemy coordinates and heading.
2. **Helper (`ROLE_PINCER_HELPER`)**:
   - The closest ally within 8 tiles pushes `ROLE_PINCER_HELPER`.
   - Paths at maximum sprint speed toward the orthogonal flank tile (`enemy_head + enemy_dir.rotate_90()`).
   - Walls the enemy into the spotter corpse.

---

## 7. Global Phase & Signal Thresholds

```
Round 0            Rounds 1 - 350         Rounds 350 - 400         Rounds 400 - 500
  |----------------------|-----------------------|-----------------------|
  Uncoil Queen           Normal Roles            Signal Check:           All Units
  (Keep 7, Child 18)     Breeders (Len>5)        If signals < 8:         Become EXPLORERS /
                         Explorers -> Bishop     Demote to BREEDER       ATTACKERS
                         Guards screen Queen     Else keep roles         Tiebreak Max Length
```

1. **Rounds 350 to 400 (Population Rescue)**:
   - Every unit counts sonar signals received over the 50-round window.
   - If `signals_received < 8`, the swarm is dying out: unit pushes `ROLE_BREEDER` and splits at `len > 5` to aggressively repopulate.
2. **Rounds 400 to 500 (Endgame Assault)**:
   - All units (except Queen and Queen Guards) convert to `ROLE_EXPLORER` / Aggressive Attackers.
   - Camp high-value pearl beds, ram long enemy snakes, and optimize total length for Tiebreaks 2 and 3.

---

## 8. Action-Heap Weight Tuning Table

The resolver pops the highest-weight valid action from the max-heap. Evaluators push actions using these tuned weights:

| Priority | Action Description | Target Role | Weight Formula / Value | Flags |
|---|---|---|---|---|
| **P1** | Round 0 Queen Uncoil (keep 7) | QUEEN | $+50,000$ | `F_NONE` |
| **P2** | Emergency Queen Collision Avoidance (Dist $\le 2$) | QUEEN | $+30,000$ | `F_NO_THREAT` |
| **P3** | Guard Kamikaze Intercept of Enemy near Queen | GUARD | $+25,000$ | `F_LUNGE \| F_NO_THREAT` |
| **P4** | Pincer Spotter Intercept Tile | PINCER_SPOTTER | $+15,000$ | `F_LUNGE \| F_NO_AREA` |
| **P5** | Adjacent Pearl Consumption | ALL | $+8,000$ | `F_NONE` |
| **P6** | Pincer Helper Flank Seal | PINCER_HELPER | $+6,000$ | `F_NONE` |
| **P7** | Breeder Split (`len > 5`) | BREEDER | $+4,500$ | `F_NONE` |
| **P8** | Bishop Promotion & Territory Stake | EXPLORER | $+4,000$ | `F_NONE` |
| **P9** | Bishop Perimeter Patrol / Hunting | BISHOP | $+3,000$ | `F_NONE` |
| **P10**| 8-Compass Exploration Step / Sprint | BREEDER / EXPLORER | $+1,500$ | `F_NONE` |
| **P11**| Idle Free-Sprint Verification | ALL | $+800$ | `F_NONE` |

---

## 9. Code Transition Checklist for the Agent

When implementing `main4.cpp` from `main3.cpp`:

1. **Replace Sonar Encoding**: Replace `msg::Type` and `keystream` in lines 220–265 with `sonar::pack` and `sonar::unpack` using the 20-bit Auth, Role, Action, Dir8 schema.
2. **Replace Role Enum**: Replace `enum Role { QUEEN, TRAPPER, ... }` with `enum class Role { QUEEN, BISHOP, GUARD, BREEDER, EXPLORER, PINCER_SPOTTER, PINCER_HELPER };` and implement `std::vector<Role> role_stack;`.
3. **Implement Compass8 Class**: Add 8-way vector tracking, step decomposition, and `reflect_heading(...)`.
4. **Update `observe()` & Role Assignment**:
   - Set Queen for `id < 2`.
   - Set Breeder for `id < 22`.
   - Set Explorer for `id 22..55`.
   - Set `id % 8` initial heading.
5. **Update Splitting Strategy (`s_split`)**:
   - For Queen: round $\le 2$ coil uncoil keeps 7 (`child = len - 7`).
   - For Breeder: split if `len > 5` (`child = 2`).
   - Compute quadrant pearls and append `ACT_SPLIT_VECTOR` to child sonar order.
6. **Implement Guard & Bishop Logic**:
   - If visible dragon is Queen or Bishop, push `ROLE_GUARD`.
   - If Explorer in rich cluster has `len > 12`, push `ROLE_BISHOP`.
   - Bishop computes breathing perimeter $R$ and emits beacon.
7. **Integrate Pincer Combat Protocol**:
   - Add spotter and helper action generators targeting enemy heads.
8. **Add Signal Count Monitor (Rounds 350–400)**:
   - Track received message count; switch to Breeder if $< 8$.
   - Switch to Explorer/Assault at round $\ge 400$.
9. **Verify Compilation**: Compile with `g++ -std=c++20 -O2 main4.cpp helper.hpp` ensuring 0 warnings and 0 errors.
