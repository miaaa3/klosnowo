# ♟️ Klosnowo

**A two-player Tafl-style strategy board game written in pure C11 — play in the terminal against a friend, against a Minimax AI, or over the network.**

![C11](https://img.shields.io/badge/C-11-A8B9CC?logo=c&logoColor=white)
![AI](https://img.shields.io/badge/AI-Minimax%20%2B%20alpha--beta-B65FB0)
![Network](https://img.shields.io/badge/network-TCP%20sockets-2E8B57)
![No dependencies](https://img.shields.io/badge/dependencies-none-lightgrey)

<p align="center">
  <img src="docs/demo.gif" alt="Playing Klosnowo against the AI in the terminal" width="100%" />
</p>

Built for the **ENSISA Immersion 2026** project. Klosnowo is inspired by Tablut / Hnefatafl: on an 11 × 7 board, BLUE and RED each have a king, nine soldiers and a city.

## 📜 Rules in 30 seconds

- Before the first move, each player places **one barricade** — an impassable square on their half of the board.
- Players then alternate, sliding **one piece** in a straight line (no jumping).
- The piece that just moved **captures** an adjacent enemy either by sandwiching it against an ally (*Linca*) or when nothing supports it from behind — an empty square or the board edge (*Seultou*). Captures can chain, and barricades always protect the defender.
- You win by **conquest** (your king reaches the enemy city), **execution** (the enemy king gets captured), or **on points** after 64 half-moves (controlled squares + surviving soldiers).

## ✨ Features

- **Four game modes** — local 2-player, human vs AI, host a network game, join a network game.
- **AI opponent** — Minimax with alpha-beta pruning (depth 4), evaluating material, territory and king threat.
- **Network play** — plain TCP sockets: the host plays BLUE, the client plays RED, moves and barricades are exchanged as small messages.
- **Animated terminal UI** — ANSI colours, territory shading, last-move highlight, sliding-piece animation and capture flashes.
- **Pure engine** — `board.c` knows nothing about input or display: rules, legal-move generation, captures and scoring are a reusable module with fixed-size arrays (no dynamic allocation).

## 🧱 Architecture

```
include/
  board.h     game state, types, engine API (the "contract")
  ai.h        Minimax interface
  network.h   TCP host / join / send / receive
src/
  board.c     rules engine — moves, captures, territory, score, end of game
  ai.c        evaluation function + Minimax with alpha-beta pruning
  network.c   socket layer
  main.c      menu, game loop, terminal rendering and animations
docs/
  Documentation_Klosnowo_V1.pdf   function-by-function technical documentation (French)
```

The game loop is identical in every mode — only the source of each side's move changes (keyboard, AI, or socket).

## 🚀 Build & play

Any C11 compiler on macOS or Linux, nothing else to install.

```bash
make        # builds ./klosnowo
make run    # builds and starts a game
```

Moves are typed as `x1 y1 x2 y2` (from → to, 0-indexed, `y = 0` at the top). Type `aide` for help, `plateau` to redraw the board, `quitter` to resign.

For a network game, start one instance in mode 3 (host) and another in mode 4 (join) with the host's IP address.

## 📜 License

[MIT](LICENSE) © 2026 Meryem Belmouaddine
