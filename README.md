# UNSW Battlecode Dragon Game: Decentralized Swarm Bot

An elite, high-performance decentralized AI engine engineered in **C++20** for the UNSW Battlecode competition. The system implements a **Priority Action-Heap + LIFO Role-Stack + Cryptographic Bit-Packed Sonar** architecture, optimizing multi-agent swarm intelligence, high-speed multi-tile sprinting, and geometric wrap-around combat on toroidal maps.

---

## Core Architectural Design

### 1. Priority Action-Resolution & LIFO Role Stack
* **Dynamic Task Allocation:** Instead of static states, units manage a runtime LIFO role stack (`std::vector<Role>`), seamlessly transitioning between operational roles (`BREEDER`, `EXPLORER`, `BISHOP`) and emergent combat triggers (`GUARD`, `PINCER_SPOTTER`, `PINCER_HELPER`).
* **Priority Action-Heap:** Independent strategy evaluators push candidate actions with weighted priorities and execution flags; the resolver selects the optimal valid action per turn cycle.

### 2. Cryptographic 64-Bit Bit-Packed Sonar Protocol
* **Bandwidth Optimization:** Compresses inter-agent messaging into a unified 64-bit bitfield layout encoding Action IDs, 8-compass headings, toroidal coordinates, and contextual payloads.
* **Stateless Authentication:** Implements custom `SplitMix64` cryptographic hashing tags to secure communication channels and prevent adversarial spoofing or replay injections.

### 3. Torus-Aware Pathfinding & Universal Sprinting
* **Toroidal Geometry:** Full mathematical modeling of wrap-around map boundaries using custom shortest-path modulo calculations (`wrap_dist` and `cheb_dist`).
* **Stamina Maximization:** Dynamic BFS-driven sprint path builders calculate multi-tile trajectories to instantly leverage maximum available stamina (`free_steps()`).
* **Geometric Edge Reflection:** Translates 8-compass vectors (`Compass8`) with real-time boundary and kelp barrier reflection logic.

### 4. High-Speed Sprint-Caging & Combat Protocols
* **Solo Sprint Caging:** Projects enemy movement vectors 1 to 2 steps forward and targets choke points to instantly lay down multi-tile body cages.
* **Coordinated Pincer Interception:** Executes calculated flanking maneuvers where spotter units sacrifice positioning to pin enemy heads against obstacles while helpers seal escape paths.

---

## Repository Structure

```text
├── main.cpp          # Core bot decision engine, action heap resolver, and behavior trees
├── helper.hpp        # UNSW Battlecode engine bindings, types, and spatial map wrappers
└── README.md         # Architecture documentation
```

---

## Performance Engineering

* **Language Standard:** C++20 (Compiled with `-O2` optimizations for strict execution tick limits).
* **Memory Management:** Zero-allocation hot paths during turn loops utilizing stack-allocated arrays and cache-efficient containers (`std::vector`, `std::deque`).
* **Spatial Processing:** Optimized O(N) BFS window flood-filling algorithms evaluating area openness and threat weight penalties.

---

## Build & Execution

To compile and execute the bot against the competition runner:

```bash
# Compile using a modern C++20 compiler
g++ -std=c++20 -O2 main.cpp helper.hpp -o bot

# Run via the official Battlecode client
battlecode-client run ./bot
```
