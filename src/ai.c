#include "ai.h"

#include <limits.h>
#include <stdlib.h>

static int iabs(int v) { return v < 0 ? -v : v; }

static int manhattan(Position a, Position b) {
    return iabs((int)a.x - (int)b.x) + iabs((int)a.y - (int)b.y);
}

/* Poids de l'heuristique. Valeurs choisies a la main (pas d'auto-tuning) :
 * le materiel (soldats) prime, le territoire et la menace du roi affinent. */
#define W_SOLDIER   10
#define W_TERRITORY  3
#define W_KING       5

int ai_evaluate(const GameState *state, Player player) {
    Player opp = board_opponent(player);

    switch (state->status) {
        case STATUS_BLUE_WINS_CONQUEST:
        case STATUS_BLUE_WINS_EXECUTION:
        case STATUS_BLUE_WINS_SCORE:
            return (player == PLAYER_BLUE) ? 1000000 : -1000000;
        case STATUS_RED_WINS_CONQUEST:
        case STATUS_RED_WINS_EXECUTION:
        case STATUS_RED_WINS_SCORE:
            return (player == PLAYER_RED) ? 1000000 : -1000000;
        case STATUS_DRAW:
            return 0;
        case STATUS_ONGOING:
        default:
            break;
    }

    int soldier_diff = state->soldiers_alive[player] - state->soldiers_alive[opp];

    int controlled_player = 0, controlled_opp = 0;
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            Player c = state->board[y][x].controlled_by;
            if (c == player) controlled_player++;
            else if (c == opp) controlled_opp++;
        }
    }
    int territory_diff = controlled_player - controlled_opp;

    /* Plus la distance du roi a la cite adverse est petite, plus la menace
     * de conquete est forte : on veut donc que ce terme grandisse quand la
     * distance diminue, d'ou l'inversion (opp_dist - player_dist). */
    int player_king_dist = manhattan(state->king_pos[player], state->city_pos[opp]);
    int opp_king_dist = manhattan(state->king_pos[opp], state->city_pos[player]);
    int king_threat_diff = opp_king_dist - player_king_dist;

    return W_SOLDIER * soldier_diff + W_TERRITORY * territory_diff + W_KING * king_threat_diff;
}

static int minimax(const GameState *state, int depth, int alpha, int beta, Player root_player) {
    if (state->status != STATUS_ONGOING || depth == 0) {
        return ai_evaluate(state, root_player);
    }

    Player mover = state->current_player;
    MoveList moves;
    board_generate_moves(state, mover, &moves);

    if (moves.count == 0) {
        /* Aucun coup legal : situation degenerative, non prevue par les
         * regles officielles. On la traite comme une perte pour `mover`. */
        return (mover == root_player) ? -1000000 : 1000000;
    }

    bool maximizing = (mover == root_player);
    int best = maximizing ? INT_MIN : INT_MAX;

    for (int i = 0; i < moves.count; i++) {
        GameState child = *state;
        board_apply_move(&child, moves.moves[i]);
        int val = minimax(&child, depth - 1, alpha, beta, root_player);

        if (maximizing) {
            if (val > best) best = val;
            if (best > alpha) alpha = best;
        } else {
            if (val < best) best = val;
            if (best < beta) beta = best;
        }
        if (alpha >= beta) break; /* elagage */
    }

    return best;
}

bool ai_choose_move(const GameState *state, int depth, Move *out_move) {
    MoveList moves;
    board_generate_moves(state, state->current_player, &moves);
    if (moves.count == 0) return false;

    Player root = state->current_player;
    int alpha = INT_MIN, beta = INT_MAX;
    int best_val = INT_MIN;
    Move best_move = moves.moves[0];

    for (int i = 0; i < moves.count; i++) {
        GameState child = *state;
        board_apply_move(&child, moves.moves[i]);
        int val = minimax(&child, depth - 1, alpha, beta, root);

        if (val > best_val) {
            best_val = val;
            best_move = moves.moves[i];
        }
        if (best_val > alpha) alpha = best_val;
    }

    *out_move = best_move;
    return true;
}
