#include "board.h"

#include <stdio.h>
#include <string.h>

static const int DX[4] = { 1, -1, 0, 0 };
static const int DY[4] = { 0, 0, 1, -1 };

static Position pos_make(int x, int y) {
    Position p;
    p.x = (int8_t)x;
    p.y = (int8_t)y;
    return p;
}

static bool pos_equal(Position a, Position b) {
    return a.x == b.x && a.y == b.y;
}

static Cell *cell_at(GameState *state, Position p) {
    return &state->board[p.y][p.x];
}

static const Cell *cell_at_c(const GameState *state, Position p) {
    return &state->board[p.y][p.x];
}

bool board_in_bounds(Position pos) {
    return pos.x >= 0 && pos.x < BOARD_WIDTH && pos.y >= 0 && pos.y < BOARD_HEIGHT;
}

/* Bloc de départ (roi + 9 soldats) dans le coin, exprimé en coordonnées
 * relatives (ligne, colonne) à partir de la diapo 8. Le plateau y est
 * illustré en 9x9 pour la lisibilité pédagogique ; comme le document ne
 * donne pas de schéma dédié au vrai plateau 11x7, on réutilise ce même
 * bloc en coin (boîte 4x4, bande de Manhattan 2..4 par rapport au coin)
 * et on le reflète pour l'autre joueur. A confirmer avec le barème/l'énoncé
 * complet ENSISA si un placement précis différent est exigé sur 11x7. */
typedef struct {
    int row, col;
    bool is_king;
} CornerCell;

static const CornerCell CORNER_BLOCK[SOLDIERS_PER_PLAYER + 1] = {
    { 0, 2, false }, { 0, 3, false },
    { 1, 1, true  }, { 1, 2, false }, { 1, 3, false },
    { 2, 0, false }, { 2, 1, false }, { 2, 2, false },
    { 3, 0, false }, { 3, 1, false },
};

static void clear_cell(Cell *c) {
    c->piece = PIECE_NONE;
    c->piece_owner = PLAYER_NONE;
    c->controlled_by = PLAYER_NONE;
    c->is_city = false;
    c->is_barricade = false;
}

static void place_corner(GameState *state, Player player, bool mirror) {
    for (int i = 0; i < SOLDIERS_PER_PLAYER + 1; i++) {
        int row = CORNER_BLOCK[i].row;
        int col = CORNER_BLOCK[i].col;
        Position p = mirror
            ? pos_make(BOARD_WIDTH - 1 - col, BOARD_HEIGHT - 1 - row)
            : pos_make(col, row);

        Cell *c = cell_at(state, p);
        c->piece = CORNER_BLOCK[i].is_king ? PIECE_KING : PIECE_SOLDIER;
        c->piece_owner = player;
        c->controlled_by = player;

        if (CORNER_BLOCK[i].is_king) {
            state->king_pos[player] = p;
        }
    }
}

void board_init(GameState *state) {
    memset(state, 0, sizeof(*state));

    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            clear_cell(&state->board[y][x]);
        }
    }

    state->city_pos[PLAYER_BLUE] = pos_make(0, 0);
    state->city_pos[PLAYER_RED]  = pos_make(BOARD_WIDTH - 1, BOARD_HEIGHT - 1);
    cell_at(state, state->city_pos[PLAYER_BLUE])->is_city = true;
    cell_at(state, state->city_pos[PLAYER_RED])->is_city = true;

    place_corner(state, PLAYER_BLUE, false);
    place_corner(state, PLAYER_RED, true);

    state->soldiers_alive[PLAYER_BLUE] = SOLDIERS_PER_PLAYER;
    state->soldiers_alive[PLAYER_RED] = SOLDIERS_PER_PLAYER;

    state->barricade_placed[PLAYER_BLUE] = false;
    state->barricade_placed[PLAYER_RED] = false;

    state->current_player = PLAYER_BLUE; /* le bleu commence, diapo 6 */
    state->status = STATUS_ONGOING;
    state->half_move_count = 0;
}

/* Diagonale = la vraie diagonale de la grille, cases où x+y est constant
 * (c'est littéralement ce que montrent les diapos : un escalier qui descend
 * d'une case à chaque pas). Le seuil naturel qui coupe le rectangle en deux
 * moitiés symétriques, coin bleu (0,0) / coin rouge (W-1,H-1), est le milieu
 * de la diagonale principale : (W-1 + H-1) / 2.
 * < DIAGONAL_REFERENCE -> côté bleu (coin haut-gauche)
 * > DIAGONAL_REFERENCE -> côté rouge (coin bas-droit)
 * == DIAGONAL_REFERENCE -> sur la diagonale (exclue du placement de barricade)
 *
 * HYPOTHESE (le PDF ne donne la diagonale que sur l'exemple 9x9, plateau
 * carré) : cette formule reproduit exactement la même diagonale géométrique,
 * généralisée au plateau rectangulaire 11x7. A valider s'il existe un schéma
 * officiel 11x7. Pour 11x7, DIAGONAL_REFERENCE = (10+6)/2 = 8 (division
 * entière exacte ici ; à surveiller si les dimensions changent et que
 * W-1+H-1 devient impair). */
static long diagonal_value(Position pos) {
    return (long)pos.x + (long)pos.y;
}
#define DIAGONAL_REFERENCE (((long)(BOARD_WIDTH - 1) + (BOARD_HEIGHT - 1)) / 2)

static bool is_adjacent_to_camp(const GameState *state, Player player, Position pos) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            Position n = pos_make(pos.x + dx, pos.y + dy);
            if (!board_in_bounds(n)) continue;
            if (pos_equal(n, state->city_pos[player])) return true;
            if (cell_at_c(state, n)->piece_owner == player) return true;
        }
    }
    return false;
}

/* Retourne NULL si `pos` est un choix de barricade valide pour `player`,
 * sinon un message expliquant précisément pourquoi (affichable tel quel). */
static const char *validate_barricade(const GameState *state, Player player, Position pos) {
    if (!board_in_bounds(pos)) return "hors du plateau";

    const Cell *c = cell_at_c(state, pos);
    if (c->is_city) return "une cité ne peut pas être une barricade";
    if (c->piece != PIECE_NONE) return "case déjà occupée par une pièce";
    if (c->is_barricade) return "il y a déjà une barricade ici";

    long v = diagonal_value(pos);
    if (v == DIAGONAL_REFERENCE) return "case sur la diagonale, exclue pour les deux joueurs";
    if (player == PLAYER_BLUE && v > DIAGONAL_REFERENCE) return "cette case est du côté rouge (x+y trop grand)";
    if (player == PLAYER_RED && v < DIAGONAL_REFERENCE) return "cette case est du côté bleu (x+y trop petit)";

    if (is_adjacent_to_camp(state, player, pos)) return "case adjacente à votre camp de départ";

    return NULL;
}

const char *board_barricade_error(const GameState *state, Player player, Position pos) {
    if (player != PLAYER_BLUE && player != PLAYER_RED) return "joueur invalide";
    if (state->barricade_placed[player]) return "votre barricade est déjà posée";
    if (player == PLAYER_RED && !state->barricade_placed[PLAYER_BLUE]) return "le bleu doit poser sa barricade en premier";
    return validate_barricade(state, player, pos);
}

bool board_place_barricade(GameState *state, Player player, Position pos) {
    if (board_barricade_error(state, player, pos) != NULL) return false;

    cell_at(state, pos)->is_barricade = true;
    state->barricade_pos[player] = pos;
    state->barricade_placed[player] = true;
    return true;
}

static const char *validate_move(const GameState *state, Player player, Move move) {
    if (player != PLAYER_BLUE && player != PLAYER_RED) return "joueur invalide";
    if (!board_in_bounds(move.from)) return "case de départ hors du plateau";
    if (!board_in_bounds(move.to)) return "case d'arrivée hors du plateau";
    if (pos_equal(move.from, move.to)) return "départ et arrivée identiques";

    bool same_row = move.from.y == move.to.y;
    bool same_col = move.from.x == move.to.x;
    if (same_row == same_col) return "déplacement non orthogonal (même ligne ou même colonne uniquement)";

    const Cell *src = cell_at_c(state, move.from);
    if (src->piece == PIECE_NONE) return "aucune pièce sur la case de départ";
    if (src->piece_owner != player) return "cette pièce ne vous appartient pas";

    int dx = (move.to.x > move.from.x) - (move.to.x < move.from.x);
    int dy = (move.to.y > move.from.y) - (move.to.y < move.from.y);

    Position cur = move.from;
    for (;;) {
        cur = pos_make(cur.x + dx, cur.y + dy);
        const Cell *c = cell_at_c(state, cur);

        bool is_destination = pos_equal(cur, move.to);
        if (!is_destination) {
            if (c->piece != PIECE_NONE) return "chemin bloqué par une pièce (pas de saut possible)";
            if (c->is_barricade) return "chemin bloqué par une barricade";
            continue;
        }

        if (c->piece != PIECE_NONE) return "case d'arrivée déjà occupée";
        if (c->is_barricade) return "case d'arrivée occupée par une barricade";
        if (c->is_city && src->piece != PIECE_KING) return "seul le roi peut entrer dans une cité";
        return NULL;
    }
}

const char *board_move_error(const GameState *state, Player player, Move move) {
    return validate_move(state, player, move);
}

bool board_is_legal_move(const GameState *state, Player player, Move move) {
    return validate_move(state, player, move) == NULL;
}

MoveResult board_apply_move(GameState *state, Move move) {
    MoveResult result;
    memset(&result, 0, sizeof(result));

    Player mover = state->current_player;
    if (!board_is_legal_move(state, mover, move)) {
        result.legal = false;
        result.status_after = state->status;
        return result;
    }
    result.legal = true;
    state->last_move = move;
    state->has_last_move = true;

    Cell *src = cell_at(state, move.from);
    PieceType moved_piece = src->piece;

    int dx = (move.to.x > move.from.x) - (move.to.x < move.from.x);
    int dy = (move.to.y > move.from.y) - (move.to.y < move.from.y);

    /* La case quittée reste contrôlée par le joueur (diapo 14) : on ne
     * touche pas à controlled_by, seule l'occupation change. Chaque case
     * traversée puis la case d'arrivée prennent la couleur du joueur
     * (diapo 14), sauf les cités qui ne sont jamais contrôlées (diapo 13). */
    Position cur = move.from;
    while (!pos_equal(cur, move.to)) {
        cur = pos_make(cur.x + dx, cur.y + dy);
        Cell *c = cell_at(state, cur);
        if (!c->is_city) c->controlled_by = mover;
    }

    Cell *dst = cell_at(state, move.to);
    dst->piece = moved_piece;
    dst->piece_owner = mover;

    src->piece = PIECE_NONE;
    src->piece_owner = PLAYER_NONE;

    if (moved_piece == PIECE_KING) {
        state->king_pos[mover] = move.to;
    }

    Player opponent = board_opponent(mover);

    /* Conquête : le roi se pose sur la cité adverse (diapo 11/12). */
    if (moved_piece == PIECE_KING && pos_equal(move.to, state->city_pos[opponent])) {
        state->status = (mover == PLAYER_BLUE) ? STATUS_BLUE_WINS_CONQUEST : STATUS_RED_WINS_CONQUEST;
    }

    /* Résolution des captures Linca / Seultou : seule la pièce qui vient de
     * bouger peut capturer, dans chacune des 4 directions depuis sa case
     * d'arrivée (diapo 23), avec chaînage possible (diapo 27, 33). Une
     * barricade agit comme un allié du joueur PASSIF (diapo 39) : elle ne
     * complète jamais un Linca pour l'attaquant, et elle fournit toujours
     * un "soutien" qui empêche un Seultou. */
    for (int d = 0; d < 4 && result.capture_count < MAX_CAPTURES_PER_MOVE; d++) {
        Position neighbor = pos_make(move.to.x + DX[d], move.to.y + DY[d]);
        if (!board_in_bounds(neighbor)) continue;

        Cell *nc = cell_at(state, neighbor);
        if (nc->piece == PIECE_NONE || nc->piece_owner != opponent) continue;

        Position beyond = pos_make(neighbor.x + DX[d], neighbor.y + DY[d]);
        bool captured = false;
        CaptureType type = CAPTURE_SEULTOU;

        if (!board_in_bounds(beyond)) {
            captured = true;               /* bord du plateau : aucun soutien */
            type = CAPTURE_SEULTOU;
        } else {
            const Cell *bc = cell_at_c(state, beyond);
            if (bc->piece != PIECE_NONE && bc->piece_owner == mover) {
                captured = true;           /* allié de l'attaquant : sandwich */
                type = CAPTURE_LINCA;
            } else if (bc->piece == PIECE_NONE && !bc->is_barricade) {
                captured = true;           /* case vide : pas de soutien */
                type = CAPTURE_SEULTOU;
            }
            /* sinon : allié du défenseur ou barricade -> protégé, pas de capture */
        }

        if (!captured) continue;

        bool was_king = (nc->piece == PIECE_KING);
        nc->piece = PIECE_NONE;
        nc->piece_owner = PLAYER_NONE;
        nc->controlled_by = PLAYER_NONE; /* la case redevient neutre, diapo 23 */

        result.captures[result.capture_count].position = neighbor;
        result.captures[result.capture_count].type = type;
        result.capture_count++;

        if (was_king) {
            state->status = (mover == PLAYER_BLUE) ? STATUS_BLUE_WINS_EXECUTION : STATUS_RED_WINS_EXECUTION;
        } else {
            state->soldiers_alive[opponent]--;
        }
    }

    state->half_move_count++;
    if (state->status == STATUS_ONGOING && state->half_move_count >= MAX_HALF_MOVES) {
        int blue_score = board_compute_score(state, PLAYER_BLUE);
        int red_score = board_compute_score(state, PLAYER_RED);
        if (blue_score > red_score) state->status = STATUS_BLUE_WINS_SCORE;
        else if (red_score > blue_score) state->status = STATUS_RED_WINS_SCORE;
        else state->status = STATUS_DRAW; /* égalité, exception rare cf. diapo 22 */
    }

    if (state->status == STATUS_ONGOING) {
        state->current_player = opponent;
    }

    result.status_after = state->status;
    return result;
}

void board_generate_moves(const GameState *state, Player player, MoveList *out) {
    out->count = 0;

    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            const Cell *src = &state->board[y][x];
            if (src->piece == PIECE_NONE || src->piece_owner != player) continue;
            Position from = pos_make(x, y);

            for (int d = 0; d < 4; d++) {
                Position cur = from;
                for (;;) {
                    cur = pos_make(cur.x + DX[d], cur.y + DY[d]);
                    if (!board_in_bounds(cur)) break;

                    const Cell *c = cell_at_c(state, cur);
                    if (c->piece != PIECE_NONE || c->is_barricade) break; /* chemin bloque au-dela */

                    if (c->is_city && src->piece != PIECE_KING) continue; /* traversable mais pas atterrissable */

                    if (out->count < MAX_LEGAL_MOVES) {
                        out->moves[out->count].from = from;
                        out->moves[out->count].to = cur;
                        out->count++;
                    }
                }
            }
        }
    }
}

int board_compute_score(const GameState *state, Player player) {
    int controlled = 0;
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (state->board[y][x].controlled_by == player) controlled++;
        }
    }
    return controlled + state->soldiers_alive[player];
}

GameStatus board_check_end_condition(const GameState *state) {
    return state->status;
}

/* Palette 256 couleurs. Fonds sombres teintés pour le territoire (l'écho des
 * zones bleu clair / rose du document de règles), avant-plans vifs pour les
 * pièces afin qu'elles restent lisibles sur n'importe quel fond. */
#define BG_NEUTRAL    "\033[49m"
#define BG_BLUE_ZONE  "\033[48;5;17m"
#define BG_RED_ZONE   "\033[48;5;52m"
#define BG_LAST_MOVE  "\033[48;5;58m"
#define BG_BARRICADE  "\033[48;5;94m"
#define BG_CITY_BLUE  "\033[48;5;27m"
#define BG_CITY_RED   "\033[48;5;160m"
#define FG_BLUE       "\033[1;38;5;45m"
#define FG_RED        "\033[1;38;5;210m"
#define FG_WHITE      "\033[1;38;5;231m"
#define FG_GRID       "\033[38;5;240m"
#define FG_LABEL      "\033[38;5;245m"
#define ANSI_RESET    "\033[0m"

/* Largeur utile d'une case, en colonnes du terminal (" Kb "). */
#define CELL_WIDTH 4
/* Colonnes prises par l'étiquette de ligne à gauche du plateau (" 10 "). */
#define LABEL_WIDTH 4

int board_screen_row(int y) {
    return y + 3; /* ligne 1 : numéros de colonnes, ligne 2 : bordure haute */
}

int board_screen_col(int x) {
    return LABEL_WIDTH + 2 + CELL_WIDTH * x; /* +1 bordure gauche, +1 pour passer en 1-indexé */
}

static const char *zone_background(const GameState *state, Position p) {
    if (state->has_last_move &&
        (pos_equal(p, state->last_move.from) || pos_equal(p, state->last_move.to))) {
        return BG_LAST_MOVE;
    }
    const Cell *c = cell_at_c(state, p);
    if (c->controlled_by == PLAYER_BLUE) return BG_BLUE_ZONE;
    if (c->controlled_by == PLAYER_RED) return BG_RED_ZONE;
    return BG_NEUTRAL;
}

static void print_cell(const GameState *state, Position p) {
    const Cell *c = cell_at_c(state, p);

    if (c->is_barricade) {
        printf(BG_BARRICADE FG_WHITE "####" ANSI_RESET);
        return;
    }

    if (c->is_city) {
        bool blue_city = pos_equal(p, state->city_pos[PLAYER_BLUE]);
        printf("%s%s", blue_city ? BG_CITY_BLUE : BG_CITY_RED, FG_WHITE);
        if (c->piece != PIECE_NONE) {
            /* un roi a conquis la cité : on le montre dessus */
            printf(" %c! ", c->piece == PIECE_KING ? 'K' : 's');
        } else {
            printf(blue_city ? " Cb " : " Cr ");
        }
        printf(ANSI_RESET);
        return;
    }

    const char *bg = zone_background(state, p);

    if (c->piece != PIECE_NONE) {
        printf("%s%s %c%c " ANSI_RESET, bg,
               c->piece_owner == PLAYER_BLUE ? FG_BLUE : FG_RED,
               c->piece == PIECE_KING ? 'K' : 's',
               c->piece_owner == PLAYER_BLUE ? 'b' : 'r');
        return;
    }

    printf("%s    " ANSI_RESET, bg);
}

static void print_border(const char *left, const char *middle, const char *right) {
    printf("%*s" FG_GRID "%s", LABEL_WIDTH, "", left);
    for (int x = 0; x < BOARD_WIDTH; x++) {
        printf("────");
        if (x < BOARD_WIDTH - 1) printf("%s", middle);
    }
    printf("%s" ANSI_RESET "\n", right);
}

void board_print(const GameState *state) {
    printf("%*s" FG_LABEL, LABEL_WIDTH, "");
    for (int x = 0; x < BOARD_WIDTH; x++) printf(" %2d ", x);
    printf(ANSI_RESET "\n");

    print_border("┌", "┬", "┐");

    for (int y = 0; y < BOARD_HEIGHT; y++) {
        printf(FG_LABEL "%*d " ANSI_RESET FG_GRID "│" ANSI_RESET, LABEL_WIDTH - 1, y);
        for (int x = 0; x < BOARD_WIDTH; x++) {
            print_cell(state, pos_make(x, y));
            printf(FG_GRID "│" ANSI_RESET);
        }
        printf("\n");
    }

    print_border("└", "┴", "┘");
}
