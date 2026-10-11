# 🐉 Decentralized Swarm Intelligence: Systems Architecture & Strategy

<p align="center">
  <b>Advanced C++20 Multi-Agent Coordination Engine for UNSW Battlecode</b>
</p>

---

## 🏛️ 1. High-Level System Architecture

The bot rejects monolithic control in favor of a **fully decentralized, autonomous agent model** operating under strict no-shared-memory constraints[cite: 3, 4]. Each dragon unit acts as an independent processing node utilizing three foundational pillars:

              +-----------------------------------+
              |       Per-Dragon Local Memory     |
              |  (Edges, Trails, Pearl Beds, Map) |
              +-----------------------------------+
                                |
                                v
              +-----------------------------------+
              |      Dynamic LIFO Role Stack      |
              |  (Base Role + Transient Triggers) |
              +-----------------------------------+
                                |
                                v
              +-----------------------------------+
              |     Priority Action Max-Heap      |
              |  (Evaluators -> Weighted Actions) |
              +-----------------------------------+
                                |
                                v
              +-----------------------------------+
              |    Authenticated 64-Bit Sonar     |
              |    (SplitMix64 Bit-Packed Pkts)   |
              +-----------------------------------+

* **Priority Action-Heap:** Independent heuristic evaluators push candidate actions (`MOVE` or `SPLIT`) with weighted priority scores and execution flags[cite: 3, 4]. The turn resolver evaluates safety, stamina (`free_steps()`), and area flood-fills before committing the optimal valid action[cite: 3, 4].
* **LIFO Role-Stack (`std::vector<Role> role_stack`)**: Units transition fluidly between permanent baseline behaviors (`BREEDER`, `EXPLORER`, `BISHOP`) and emergency tactical overrides (`GUARD`, `PINCER_SPOTTER`, `PINCER_HELPER`) pushed and popped at runtime[cite: 3, 4].

---

## 🔐 2. Cryptographic 64-Bit Bit-Packed Sonar Protocol

To maximize acoustic bandwidth and prevent adversarial spoofing or message injection across shared channels, inter-agent communication uses an authenticated 64-bit bitfield layout[cite: 3, 4].

### Bitfield Layout Specification
63                  44 43        36 35    32 31    28 27     25 24     21 20      13 12       5 4     0
+----------------------+------------+--------+--------+---------+---------+----------+----------+-------+
|  Auth Tag (20 bits)  | Round (8b) | Role(4)| Act(4) | Dir8(3) | Rsvd(1) |   X (8b) |   Y (8b) |Extra(5)
+----------------------+------------+--------+--------+---------+---------+----------+----------+-------+


* **Auth Tag ($63 \dots 44$):** 20-bit cryptographic hash derived via `SplitMix64` seeded with round state, sender role, and action directive to ensure message integrity[cite: 3, 4].
* **Round Stamp ($43 \dots 36$):** 8-bit rolling round counter (`round % 256`) enforcing strict message window validity (rejecting packets older than 3 turns)[cite: 3, 4].
* **Payload Fields:** Encodes sender role classification, action commands (`BEACON`, `SPLIT_VECTOR`, `PINCER_CALL`), 8-compass direction headings, spatial coordinates, and contextual metadata[cite: 3, 4].

---

## 🌲 3. Dynamic Role Hierarchy & State Machine

                    +-----------------------+
                    |     QUEEN (ID 0/1)    |
                    +-----------------------+
                                |
        +-----------------------+-----------------------+
        | (Sighted by any unit)                         |
        v                                               v
+-----------------------+                       +-----------------------+
|     GUARD (Queen)     |                       |     GUARD (Bishop)    |
| - Kamikaze Intercept  |                       | - Perimeter Defense   |
| - Sprint Caging       |                       | - Corridor Clearance  |
+-----------------------+                       +-----------------------+
^
| (Cluster ≥ 4, Len > 12)
+-----------------------+       Rich Cluster            +-----------------------+
|       EXPLORER        | ----------------------------> |        BISHOP         |
| - 8-Dir Pathfinding   |                               | - Breathing Radius R  |
| - Edge Reflection     |                               | - Territory Anchoring |
+-----------------------+                               +-----------------------+
^
| (Signal Count < 8 in Rounds 350-400)
v
+-----------------------+
|        BREEDER        |
| - Asymmetric Splits   |
| - Quadrant Delegation |
+-----------------------+


* **Queen (`ID 0–1`):** Absolute swarm anchor. Executes round 0 uncoiling, completely avoids portals, enforces strict threat avoidance weights against enemy heads, and broadcasts continuous tracking beacons[cite: 3, 4].
* **Breeder (`ID < 22`):** Rapid resource generation unit. Sweeps across assigned 8-way vectors, splits immediately upon reaching length thresholds, and delegates exploration quadrants to children via sonar packets[cite: 3, 4].
* **Explorer (`ID 22–55`):** Independent territory scout. Continuously evaluates local pearl cluster density and snake length to promote itself into an anchoring regional `BISHOP`[cite: 3, 4].
* **Bishop:** Dominates high-yield resource clusters, maintaining a dynamic breathing perimeter $R$, issuing periodic synchronization beacons, and automatically yielding territory to act as a defensive guard if the Queen enters visual range[cite: 3, 4].
* **Guard:** High-priority tactical role assumed instantly upon spotting leadership units under threat, deploying high-speed kamikaze interceptions and geometric sprint-cages[cite: 3, 4].

---

## 🧭 4. Toroidal Geometry & 8-Compass Navigation

* **Vector Discretization:** Units navigate along 8 discrete compass vectors (`N`, `NE`, `E`, `SE`, `S`, `SW`, `W`, `NW`)[cite: 3, 4].
* **Step Decomposition:** Diagonal movements alternate between cardinal components dynamically based on round parity and local tile passability[cite: 3, 4].
* **Geometric Edge Reflection:** When trajectory lines intersect map boundary walls or impassable kelp barriers, headings reflect symmetrically (vertical boundaries invert horizontal vector components; horizontal boundaries invert vertical components)[cite: 3, 4].

---

## ⚔️ 5. Advanced Tactical Combat Protocols

* **Asymmetric Child Splitting:** During population splits, breeders evaluate forward-left and forward-right visual quadrants for pearl concentrations, steering toward richer zones while commanding spawned children via sonar to cover complementary paths[cite: 3, 4].
* **Sacrificial Pincer Protocol:** When confronting enemy snakes with significant length advantages ($L_{\text{enemy}} > L_{\text{self}} + 3$), designated spotter units sacrifice positioning to pin the enemy's predicted forward path and broadcast coordinates[cite: 3, 4]. Nearby helper units execute high-speed flanking sprints to seal escape vectors, forcing fatal head-on collisions that generate harvestable corpse pearls[cite: 3, 4].

---

## 📊 6. Action Priority & Weight Hierarchy

The turn resolver evaluates and executes candidate behaviors based on the following weighted priority table:

| Priority | Action Description | Target Role | Weight / Value | Execution Flags |
| :---: | :--- | :---: | :---: | :--- |
| **P1** | Round 0 Queen Opening Uncoil | QUEEN | $+50,000$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P2** | Emergency Queen Collision Evasion | QUEEN | $+30,000$[cite: 3, 4] | `F_NO_THREAT`[cite: 3, 4] |
| **P3** | Guard Kamikaze Threat Interception | GUARD | $+25,000$[cite: 3, 4] | `F_LUNGE | F_NO_THREAT`[cite: 3, 4] |
| **P4** | Pincer Spotter Head Pinning | PINCER_SPOTTER | $+15,000$[cite: 3, 4] | `F_LUNGE | F_NO_AREA`[cite: 3, 4] |
| **P5** | Immediate Pearl Consumption | ALL | $+8,000$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P6** | Pincer Helper Flank Sealing | PINCER_HELPER | $+6,000$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P7** | Breeder Population Splitting | BREEDER | $+4,500$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P8** | Bishop Promotion & Territory Staking | EXPLORER | $+4,000$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P9** | Bishop Perimeter Patrol & Hunting | BISHOP | $+3,000$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P10**| 8-Compass Exploration & Sprinting | BREEDER / EXPLORER | $+1,500$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
| **P11**| Idle Free-Sprint Verification | ALL | $+800$[cite: 3, 4] | `F_NONE`[cite: 3, 4] |
