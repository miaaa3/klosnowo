#ifndef KLOSNOWO_BOARD_H
#define KLOSNOWO_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------
 * Dimensions du plateau (règles ENSISA Immersion 2026, Klosnowo)
 * ------------------------------------------------------------------------ */
#define BOARD_WIDTH          11   /* colonnes, axe x */
#define BOARD_HEIGHT          7   /* lignes,   axe y */
#define SOLDIERS_PER_PLAYER   9
#define MAX_HALF_MOVES       64   /* 32 tours par joueur avant score */

/* Bornes larges pour les listes de coups / captures : le plateau est petit
 * (11x7 = 77 cases) donc des tableaux à taille fixe suffisent, pas besoin
 * d'allocation dynamique. */
#define MAX_LEGAL_MOVES      512
#define MAX_CAPTURES_PER_MOVE  8  /* Linca/Seultou peuvent chaîner sur un
                                     seul coup (cf. "Double Linca", diapo 27) */

/* ------------------------------------------------------------------------
 * Types de base
 * ------------------------------------------------------------------------ */

typedef enum {
    PLAYER_NONE = 0, /* neutre : case non contrôlée, absence de pièce, etc. */
    PLAYER_BLUE,
    PLAYER_RED
} Player;

typedef enum {
    PIECE_NONE = 0,
    PIECE_SOLDIER,
    PIECE_KING
} PieceType;

typedef enum {
    STATUS_ONGOING = 0,
    STATUS_BLUE_WINS_CONQUEST,   /* roi bleu posé sur la cité rouge */
    STATUS_RED_WINS_CONQUEST,    /* roi rouge posé sur la cité bleue */
    STATUS_BLUE_WINS_EXECUTION,  /* roi rouge capturé */
    STATUS_RED_WINS_EXECUTION,   /* roi bleu capturé */
    STATUS_BLUE_WINS_SCORE,      /* limite de demi-tours atteinte, score bleu > rouge */
    STATUS_RED_WINS_SCORE,       /* limite de demi-tours atteinte, score rouge > bleu */
    STATUS_DRAW                  /* limite de demi-tours atteinte, égalité (cf. diapo 22) */
} GameStatus;

/* Nature du coup joué : utile pour le réseau (message à émettre) et pour
 * dérouler/déboguer une chaîne de captures. */
typedef enum {
    CAPTURE_LINCA = 0,  /* prise sandwich : pièce ennemie encadrée par la
                            pièce en mouvement et un allié (ou une barricade,
                            cf. diapo 39 : la barricade protège le joueur
                            passif comme le ferait un allié) */
    CAPTURE_SEULTOU     /* prise en poussant : attaque frontale d'un pion
                            sans soutien direct derrière lui */
} CaptureType;

/* ------------------------------------------------------------------------
 * Position et coup
 * ------------------------------------------------------------------------ */

typedef struct {
    int8_t x; /* colonne, 0..BOARD_WIDTH-1 */
    int8_t y; /* ligne,   0..BOARD_HEIGHT-1 */
} Position;

typedef struct {
    Position from;
    Position to;
} Move;

/* Une capture individuelle résultant d'un coup : la case qui redevient
 * neutre (occupation ET contrôle, cf. diapo 23) et le type de prise. */
typedef struct {
    Position position;
    CaptureType type;
} Capture;

/* Résultat complet de l'application d'un coup : nécessaire pour que main.c
 * notifie le réseau (send_move) et pour que ai.c évalue/annule un coup lors
 * de la recherche Alpha-Bêta. */
typedef struct {
    bool legal;                              /* false si le coup était invalide */
    int  capture_count;
    Capture captures[MAX_CAPTURES_PER_MOVE];
    GameStatus status_after;                 /* état de la partie après le coup */
} MoveResult;

typedef struct {
    Move moves[MAX_LEGAL_MOVES];
    int  count;
} MoveList;

/* ------------------------------------------------------------------------
 * Case du plateau
 * ------------------------------------------------------------------------ */

typedef struct {
    PieceType piece;        /* PIECE_NONE si la case est vide */
    Player    piece_owner;  /* PLAYER_NONE si piece == PIECE_NONE */
    Player    controlled_by;/* PLAYER_NONE si case neutre. Toujours
                                PLAYER_NONE sur une cité : les cités ne sont
                                jamais contrôlées (diapo 13). */
    bool      is_city;      /* case cité, fixe pour toute la partie */
    bool      is_barricade; /* posée une fois avant le premier coup, jamais
                                déplacée ni capturée ensuite (diapo 39) */
} Cell;

/* ------------------------------------------------------------------------
 * État complet de la partie
 * ------------------------------------------------------------------------ */

typedef struct {
    Cell board[BOARD_HEIGHT][BOARD_WIDTH];

    Player current_player;   /* le bleu commence (diapo 6) */
    GameStatus status;

    Position king_pos[3];    /* indexé par Player : king_pos[PLAYER_BLUE/RED] */
    Position city_pos[3];    /* idem, position fixe des cités */
    Position barricade_pos[3];
    bool     barricade_placed[3];

    int soldiers_alive[3];   /* indexé par Player, hors roi (diapo 19) */
    int half_move_count;     /* incrémenté à chaque coup, max MAX_HALF_MOVES */

    /* Dernier coup joué, surligné à l'affichage pour qu'on voie d'un coup
     * d'œil ce que l'adversaire (ou l'IA) vient de faire. */
    Move last_move;
    bool has_last_move;
} GameState;

/* ------------------------------------------------------------------------
 * API du moteur de jeu
 * ------------------------------------------------------------------------ */

/* Initialise le plateau 11x7 : cités, positions de départ du roi et des 9
 * soldats de chaque camp, contrôle initial (10 cases par joueur, cf. diapo
 * 13), tour au joueur bleu. Aucune barricade n'est posée : voir
 * board_place_barricade(). */
void board_init(GameState *state);

/* Phase de barricade : le bleu choisit en premier, le rouge voit ensuite le
 * choix du bleu avant de placer le sien (diapo 36). Rejette toute case hors
 * du côté du joueur, sur la diagonale, ou adjacente à son propre camp de
 * départ (diapo 35). Retourne false et ne modifie rien si la position est
 * invalide ou si la barricade du joueur est déjà posée. */
bool board_place_barricade(GameState *state, Player player, Position pos);

/* NULL si `pos` est un choix de barricade valide pour `player`, sinon un
 * message expliquant précisément pourquoi (prêt à afficher au joueur). */
const char *board_barricade_error(const GameState *state, Player player, Position pos);

/* true si (x,y) est dans le plateau. */
bool board_in_bounds(Position pos);

/* true si un déplacement en ligne orthogonale de move.from à move.to est
 * géométriquement possible pour `player` : même ligne/colonne, chemin non
 * obstrué (aucun saut, aucune barricade traversée), case d'arrivée libre ou
 * capturable selon les règles de mouvement (pas de règle de capture ici,
 * seulement la légalité du déplacement). N'autorise l'arrivée sur une cité
 * que si la pièce déplacée est le roi ; l'arrivée sur sa propre cité est
 * toujours interdite. */
bool board_is_legal_move(const GameState *state, Player player, Move move);

/* NULL si le coup est légal, sinon un message expliquant précisément
 * pourquoi (prêt à afficher au joueur). */
const char *board_move_error(const GameState *state, Player player, Move move);

/* Applique un coup déjà validé par board_is_legal_move : déplace la pièce,
 * met à jour le contrôle de la case quittée (elle reste contrôlée, diapo
 * 14) et de la case d'arrivée, résout la chaîne de captures Linca/Seultou
 * déclenchée par la pièce arrivée (une pièce capturée est retirée et sa
 * case redevient neutre, diapo 23), vérifie les conditions de victoire,
 * incrémente half_move_count et passe la main à l'adversaire. */
MoveResult board_apply_move(GameState *state, Move move);

/* Remplit `out` avec tous les coups légaux de `player` dans `state`. Pense
 * pour l'IA (génération de coups avant recherche), mais ne dépend que de
 * l'API publique du plateau : un simple balayage des 4 directions depuis
 * chaque pièce de `player`, qui s'arrête dès qu'un obstacle bloque la
 * suite du chemin (même logique que board_is_legal_move). */
void board_generate_moves(const GameState *state, Player player, MoveList *out);

/* Score d'un joueur : cases contrôlées + soldats vivants (le roi ne compte
 * pas, diapo 19). Utilisé au bout de MAX_HALF_MOVES et par l'heuristique de
 * ai.c. */
int board_compute_score(const GameState *state, Player player);

/* Réévalue et retourne l'état de la partie (victoire/défaite/nulle/en
 * cours) sans modifier `state`. */
GameStatus board_check_end_condition(const GameState *state);

static inline Player board_opponent(Player p) {
    return p == PLAYER_BLUE ? PLAYER_RED : (p == PLAYER_RED ? PLAYER_BLUE : PLAYER_NONE);
}

/* Affiche le plateau dans le terminal : grille encadrée, fond coloré pour
 * le territoire contrôlé, surlignage du dernier coup (couleurs ANSI 256). */
void board_print(const GameState *state);

/* Position (ligne, colonne) d'une case du plateau dans le terminal, en
 * coordonnées ANSI 1-indexées, telle que board_print vient de la dessiner.
 * Permet d'aller réécrire une case précise (animation de capture) sans
 * redessiner tout le plateau, et garde ce calcul au même endroit que le
 * format d'affichage. */
int board_screen_row(int y);
int board_screen_col(int x);

#endif /* KLOSNOWO_BOARD_H */
