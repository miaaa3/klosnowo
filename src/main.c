#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "board.h"
#include "ai.h"
#include "network.h"

/* Quatre façons de jouer : deux joueurs humains au même clavier, un humain
 * (BLEU) contre l'IA (ROUGE), ou en réseau où l'hôte joue BLEU et celui qui
 * rejoint joue ROUGE. Dans tous les cas, la boucle de partie ci-dessous est
 * la même : seule la façon d'obtenir le coup de chaque camp change. */
typedef enum {
    MODE_LOCAL,
    MODE_VS_AI,
    MODE_NET_HOST,
    MODE_NET_CLIENT
} GameMode;

#define ANIM_FRAME_DELAY_US 120000 /* 120 ms par case parcourue */
#define FLASH_DELAY_US      160000
#define FLASH_BLINKS        3

#define FG_DIM    "\033[38;5;245m"
#define ANSI_OFF  "\033[0m"

static void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

static const char *player_name(Player p) {
    return p == PLAYER_BLUE ? "BLEU" : (p == PLAYER_RED ? "ROUGE" : "?");
}

static const char *player_color(Player p) {
    return p == PLAYER_BLUE ? "\033[1;34m" : (p == PLAYER_RED ? "\033[1;31m" : "");
}

static void clear_screen(void) {
    printf("\033[H\033[2J");
    fflush(stdout);
}

static void print_legend(void) {
    printf(FG_DIM "Kb/Kr" ANSI_OFF " roi   " FG_DIM "sb/sr" ANSI_OFF " soldat   "
           FG_DIM "Cb/Cr" ANSI_OFF " cite   " FG_DIM "####" ANSI_OFF " barricade   "
           "\033[48;5;17m    \033[0m territoire bleu   "
           "\033[48;5;52m    \033[0m territoire rouge   "
           "\033[48;5;58m    \033[0m dernier coup\n");
}

static void print_help(void) {
    printf("\nCommandes :\n");
    printf("  x1 y1 x2 y2   deplace la piece de (x1,y1) vers (x2,y2)\n");
    printf("  plateau       reaffiche le plateau\n");
    printf("  aide          affiche cette aide\n");
    printf("  quitter       abandonne la partie\n");
    printf("Coordonnees 0-indexees : x = colonne (0..%d), y = ligne (0..%d), y=0 en haut.\n\n",
           BOARD_WIDTH - 1, BOARD_HEIGHT - 1);
}

static void print_scores(const GameState *state) {
    printf("Score BLEU : %d (cases + soldats)   Score ROUGE : %d (cases + soldats)\n",
           board_compute_score(state, PLAYER_BLUE),
           board_compute_score(state, PLAYER_RED));
}

/* Lit une ligne de commande. Retourne false en cas d'EOF (Ctrl-D). */
static bool read_line(char *buf, size_t size) {
    if (!fgets(buf, (int)size, stdin)) return false;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return true;
}

static void to_lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Tolère les virgules ou points-virgules comme séparateurs ("x1,y1,x2,y2"),
 * en plus des espaces. */
static void normalize_separators(char *s) {
    for (; *s; s++) if (*s == ',' || *s == ';') *s = ' ';
}

/* Choix du mode de jeu au lancement. Relit tant que la reponse n'est pas
 * un chiffre valide entre 1 et 4. */
static GameMode choose_mode(void) {
    char line[64];
    for (;;) {
        printf("=== Klosnowo (ENSISA Immersion 2026) ===\n\n");
        printf("Mode de jeu :\n");
        printf("  1) Local     - 2 joueurs sur ce clavier\n");
        printf("  2) Contre IA - vous jouez BLEU, l'ordinateur joue ROUGE\n");
        printf("  3) Reseau    - heberger une partie (vous jouez BLEU)\n");
        printf("  4) Reseau    - rejoindre une partie (vous jouez ROUGE)\n");
        printf("Choix (1-4) : ");
        fflush(stdout);
        if (!read_line(line, sizeof(line))) exit(0);
        if (strcmp(line, "1") == 0) return MODE_LOCAL;
        if (strcmp(line, "2") == 0) return MODE_VS_AI;
        if (strcmp(line, "3") == 0) return MODE_NET_HOST;
        if (strcmp(line, "4") == 0) return MODE_NET_CLIENT;
        printf("Reponse non reconnue.\n\n");
    }
}

static int ask_port(void) {
    char line[64];
    for (;;) {
        printf("Port (ex: 5000) : ");
        fflush(stdout);
        if (!read_line(line, sizeof(line))) exit(0);
        int port;
        if (sscanf(line, "%d", &port) == 1 && port > 0 && port < 65536) return port;
        printf("Port invalide.\n");
    }
}

static void ask_host(char *buf, size_t size) {
    printf("Adresse IP du serveur (ex: 127.0.0.1) : ");
    fflush(stdout);
    if (!read_line(buf, size)) exit(0);
}

/* L'IA de recherche (ai.c) ne couvre que le choix des coups, pas la phase
 * de barricade : on choisit ici la premiere case valide trouvee en
 * balayant le plateau, ce qui suffit a obtenir un placement legal sans
 * avoir a coder une heuristique dediee. */
static Position pick_ai_barricade(const GameState *state, Player player) {
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            Position pos = { (int8_t)x, (int8_t)y };
            if (board_barricade_error(state, player, pos) == NULL) return pos;
        }
    }
    return (Position){ 0, 0 }; /* ne devrait jamais arriver : il y a toujours une case valide */
}

/* Petite tutorial d'ouverture, avec des exemples concrets qui correspondent
 * vraiment au plateau de depart (pas des coordonnees abstraites). */
static void print_intro(void) {
    clear_screen();
    printf("=== Klosnowo (ENSISA Immersion 2026) : mode local, 2 joueurs sur la meme machine ===\n\n");
    printf("Comment ca marche, en 3 etapes :\n");
    printf("  1) Chaque joueur pose UNE barricade (un obstacle infranchissable) avant de jouer.\n");
    printf("     -> On tape juste deux nombres separes par un espace : x y\n");
    printf("     -> Exemple pour BLEU : 6 0    Exemple pour ROUGE : 9 0\n");
    printf("  2) Ensuite BLEU puis ROUGE se relaient pour deplacer une piece a la fois.\n");
    printf("     -> On tape quatre nombres : x1 y1 x2 y2  (case de depart, puis case d'arrivee)\n");
    printf("     -> Exemple : 3 0 4 0   deplace la piece qui est en case (3,0) vers la case (4,0)\n");
    printf("     -> Les coordonnees s'affichent en haut (x, colonnes) et a gauche (y, lignes) du plateau.\n");
    printf("  3) On gagne en amenant son roi sur la cite adverse, en capturant le roi adverse,\n");
    printf("     ou en ayant le meilleur score (cases + soldats) apres %d coups.\n\n", MAX_HALF_MOVES);
    printf("Tapez 'aide' a tout moment pour revoir les commandes. Appuyez sur Entree pour commencer...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { /* attend la touche Entree */ }
}

/* Anime le glissement d'une piece de move.from a move.to sur une COPIE du
 * plateau (state n'est jamais modifie ici) : on ne fait qu'un rendu visuel
 * avant d'appliquer le vrai coup avec board_apply_move. */
static void animate_move(const GameState *state, Move move) {
    GameState anim = *state; /* copie complete : que des tableaux/entiers, pas de pointeurs */
    anim.has_last_move = false; /* pas de surlignage du coup precedent pendant l'animation */

    Cell moving = anim.board[move.from.y][move.from.x];
    anim.board[move.from.y][move.from.x].piece = PIECE_NONE;
    anim.board[move.from.y][move.from.x].piece_owner = PLAYER_NONE;

    int dx = (move.to.x > move.from.x) - (move.to.x < move.from.x);
    int dy = (move.to.y > move.from.y) - (move.to.y < move.from.y);

    Position cur = move.from;
    while (cur.x != move.to.x || cur.y != move.to.y) {
        Position prev = cur;
        cur.x = (int8_t)(cur.x + dx);
        cur.y = (int8_t)(cur.y + dy);

        anim.board[prev.y][prev.x].piece = PIECE_NONE;
        anim.board[prev.y][prev.x].piece_owner = PLAYER_NONE;
        anim.board[cur.y][cur.x].piece = moving.piece;
        anim.board[cur.y][cur.x].piece_owner = moving.piece_owner;

        clear_screen();
        board_print(&anim);
        print_legend();
        fflush(stdout);
        usleep(ANIM_FRAME_DELAY_US);
    }
}

/* Boucle de saisie d'une barricade valide pour `player`. N'efface pas
 * l'ecran entre deux essais invalides : le message d'erreur reste visible
 * juste au-dessus de la nouvelle invite. */
static void prompt_barricade(GameState *state, Player player) {
    char line[128];
    for (;;) {
        printf("%s%s\033[0m, choisissez la case de votre barricade (x y) : ",
               player_color(player), player_name(player));
        fflush(stdout);
        if (!read_line(line, sizeof(line))) {
            printf("\nFin de partie (EOF).\n");
            exit(0);
        }
        normalize_separators(line);

        int x, y;
        if (sscanf(line, "%d %d", &x, &y) != 2) {
            printf("Entree invalide, format attendu : x y  (ex: 8 0)\n");
            continue;
        }

        Position pos = { (int8_t)x, (int8_t)y };
        const char *err = board_barricade_error(state, player, pos);
        if (err != NULL) {
            printf("Position refusee : %s.\n", err);
            continue;
        }
        board_place_barricade(state, player, pos);

        clear_screen();
        board_print(state);
        print_legend();
        break;
    }
}

static const char *capture_type_label(CaptureType t) {
    return t == CAPTURE_LINCA ? "Linca" : "Seultou";
}

/* Fait clignoter chaque case capturee directement par-dessus le plateau deja
 * affiche a l'ecran (la derniere frame de animate_move montre encore la
 * pièce capturee juste avant sa disparition, donc pas besoin de redessiner
 * tout le plateau ici : on positionne juste le curseur sur chaque case). */
static void flash_captures(const MoveResult *result, Player captured_owner) {
    if (result->capture_count == 0) return;

    const char *color = (captured_owner == PLAYER_BLUE) ? "\033[1;34m" : "\033[1;31m";
    printf("\a"); /* bip terminal */

    for (int blink = 0; blink < FLASH_BLINKS; blink++) {
        for (int i = 0; i < result->capture_count; i++) {
            Position p = result->captures[i].position;
            move_cursor(board_screen_row(p.y), board_screen_col(p.x));
            printf("%s\033[7m XX \033[0m", color); /* video inversee */
        }
        fflush(stdout);
        usleep(FLASH_DELAY_US);

        for (int i = 0; i < result->capture_count; i++) {
            Position p = result->captures[i].position;
            move_cursor(board_screen_row(p.y), board_screen_col(p.x));
            printf("    ");
        }
        fflush(stdout);
        usleep(FLASH_DELAY_US / 2);
    }
}

/* Fait clignoter une ligne de texte quelques fois, pour donner un peu de
 * relief a l'annonce de victoire. */
static void flash_banner(const char *text, const char *color) {
    for (int i = 0; i < 4; i++) {
        printf("\r%s%s\033[0m", (i % 2 == 0) ? color : "\033[2m", text);
        fflush(stdout);
        usleep(300000);
    }
    printf("\n");
}

static void print_move_result(const MoveResult *result) {
    for (int i = 0; i < result->capture_count; i++) {
        printf("  -> %s : capture en (%d,%d)\n",
               capture_type_label(result->captures[i].type),
               result->captures[i].position.x,
               result->captures[i].position.y);
    }
}

static void print_final_status(const GameState *state) {
    printf("\n==================== FIN DE PARTIE ====================\n");
    board_print(state);
    print_scores(state);

    const char *yellow = "\033[1;33m";
    switch (state->status) {
        case STATUS_BLUE_WINS_CONQUEST:
            flash_banner("*** Victoire BLEU par CONQUETE (roi sur la cite adverse) ***", player_color(PLAYER_BLUE));
            break;
        case STATUS_RED_WINS_CONQUEST:
            flash_banner("*** Victoire ROUGE par CONQUETE (roi sur la cite adverse) ***", player_color(PLAYER_RED));
            break;
        case STATUS_BLUE_WINS_EXECUTION:
            flash_banner("*** Victoire BLEU par EXECUTION (roi rouge capture) ***", player_color(PLAYER_BLUE));
            break;
        case STATUS_RED_WINS_EXECUTION:
            flash_banner("*** Victoire ROUGE par EXECUTION (roi bleu capture) ***", player_color(PLAYER_RED));
            break;
        case STATUS_BLUE_WINS_SCORE: {
            char msg[128];
            snprintf(msg, sizeof(msg), "*** Limite de %d demi-tours : victoire BLEU au score ***", MAX_HALF_MOVES);
            flash_banner(msg, player_color(PLAYER_BLUE));
            break;
        }
        case STATUS_RED_WINS_SCORE: {
            char msg[128];
            snprintf(msg, sizeof(msg), "*** Limite de %d demi-tours : victoire ROUGE au score ***", MAX_HALF_MOVES);
            flash_banner(msg, player_color(PLAYER_RED));
            break;
        }
        case STATUS_DRAW: {
            char msg[128];
            snprintf(msg, sizeof(msg), "*** Limite de %d demi-tours : EGALITE ***", MAX_HALF_MOVES);
            flash_banner(msg, yellow);
            break;
        }
        default:
            break;
    }
}

/* Applique un coup deja considere legal : anime le glissement, l'applique
 * reellement, declenche le flash de capture, puis reaffiche le plateau.
 * Partagee par les trois sources de coups (joueur local, IA, reseau) pour
 * que l'animation soit identique quel que soit qui a joue. */
static MoveResult perform_move(GameState *state, Move move) {
    Player mover = state->current_player;
    animate_move(state, move);
    MoveResult result = board_apply_move(state, move);
    flash_captures(&result, board_opponent(mover));

    clear_screen();
    board_print(state);
    print_legend();
    print_move_result(&result);
    return result;
}

/* Boucle de saisie d'un coup pour le joueur local. Gere aussi les
 * commandes ('aide', 'plateau', 'quitter'). Renvoie le coup joue une fois
 * valide et applique. */
static void prompt_move(GameState *state, NetConnection *net, bool send_over_network) {
    char line[128];
    for (;;) {
        printf("Coup ? (x1 y1 x2 y2, 'plateau', 'aide', 'quitter') : ");
        fflush(stdout);

        if (!read_line(line, sizeof(line))) {
            printf("\nFin de partie (EOF).\n");
            exit(0);
        }

        char trimmed[128];
        strncpy(trimmed, line, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        to_lower_inplace(trimmed);

        if (strcmp(trimmed, "quitter") == 0 || strcmp(trimmed, "quit") == 0) {
            printf("Partie abandonnee.\n");
            exit(0);
        }
        if (strcmp(trimmed, "aide") == 0 || strcmp(trimmed, "help") == 0) {
            print_help();
            continue;
        }
        if (strcmp(trimmed, "plateau") == 0 || strcmp(trimmed, "board") == 0) {
            clear_screen();
            board_print(state);
            print_legend();
            continue;
        }

        normalize_separators(line);
        int x1, y1, x2, y2;
        if (sscanf(line, "%d %d %d %d", &x1, &y1, &x2, &y2) != 4) {
            printf("Entree invalide. Tapez 'aide' pour le format des commandes.\n");
            continue;
        }

        Move move;
        move.from = (Position){ (int8_t)x1, (int8_t)y1 };
        move.to   = (Position){ (int8_t)x2, (int8_t)y2 };

        const char *move_err = board_move_error(state, state->current_player, move);
        if (move_err != NULL) {
            printf("Coup illegal : %s.\n", move_err);
            continue;
        }

        if (send_over_network && !network_send_move(net, move)) {
            printf("Connexion perdue en envoyant le coup.\n");
            exit(1);
        }
        perform_move(state, move);
        return;
    }
}

int main(void) {
    GameMode mode = choose_mode();
    NetConnection net;
    net.socket_fd = -1;
    Player local_color = PLAYER_BLUE;

    switch (mode) {
        case MODE_LOCAL:
            break;
        case MODE_VS_AI:
            local_color = PLAYER_BLUE;
            printf("Vous jouez BLEU contre l'IA (profondeur de recherche %d).\n", AI_DEFAULT_DEPTH);
            break;
        case MODE_NET_HOST: {
            local_color = PLAYER_BLUE;
            int port = ask_port();
            if (!network_host(&net, port)) { fprintf(stderr, "Impossible d'heberger la partie.\n"); return 1; }
            break;
        }
        case MODE_NET_CLIENT: {
            local_color = PLAYER_RED;
            char host[128];
            ask_host(host, sizeof(host));
            int port = ask_port();
            if (!network_connect(&net, host, port)) { fprintf(stderr, "Connexion echouee.\n"); return 1; }
            break;
        }
    }

    GameState state;
    board_init(&state);

    print_intro();

    clear_screen();
    board_print(&state);
    print_legend();
    int diag_ref = (BOARD_WIDTH - 1 + BOARD_HEIGHT - 1) / 2;
    printf("\nPhase de barricade : le BLEU place en premier, puis le ROUGE.\n");
    printf("Regle : la case doit verifier x+y < %d pour BLEU (x+y > %d pour ROUGE),\n"
           "ne pas etre sur la diagonale (x+y = %d), ne pas etre occupee,\n"
           "et ne pas toucher (meme en diagonale) votre camp de depart.\n",
           diag_ref, diag_ref, diag_ref);

    Player barricade_order[2] = { PLAYER_BLUE, PLAYER_RED };
    for (int i = 0; i < 2; i++) {
        Player player = barricade_order[i];
        bool is_local = (mode == MODE_LOCAL) || (player == local_color);

        if (is_local) {
            prompt_barricade(&state, player);
            if ((mode == MODE_NET_HOST || mode == MODE_NET_CLIENT) &&
                !network_send_barricade(&net, state.barricade_pos[player])) {
                printf("Connexion perdue en envoyant la barricade.\n");
                return 1;
            }
        } else if (mode == MODE_VS_AI) {
            Position pos = pick_ai_barricade(&state, player);
            board_place_barricade(&state, player, pos);
            clear_screen();
            board_print(&state);
            print_legend();
            printf("L'IA (%s) pose sa barricade en (%d,%d).\n", player_name(player), pos.x, pos.y);
        } else { /* barricade distante, recue par le reseau */
            NetMessage msg;
            if (!network_receive(&net, &msg) || msg.type != NET_MSG_BARRICADE ||
                !board_place_barricade(&state, player, msg.pos)) {
                printf("Erreur reseau pendant la phase de barricade.\n");
                return 1;
            }
            clear_screen();
            board_print(&state);
            print_legend();
            printf("%s pose sa barricade en (%d,%d).\n", player_name(player), msg.pos.x, msg.pos.y);
        }
    }

    while (state.status == STATUS_ONGOING) {
        clear_screen();
        board_print(&state);
        print_legend();
        printf("\n%s%s\033[0m au trait  (demi-tour %d/%d)\n",
               player_color(state.current_player), player_name(state.current_player),
               state.half_move_count, MAX_HALF_MOVES);
        print_scores(&state);

        bool is_local_turn = (mode == MODE_LOCAL) || (state.current_player == local_color);
        bool is_network_mode = (mode == MODE_NET_HOST || mode == MODE_NET_CLIENT);

        if (is_local_turn) {
            prompt_move(&state, &net, is_network_mode);
        } else if (mode == MODE_VS_AI) {
            printf("L'IA reflechit...\n");
            fflush(stdout);
            Move move;
            if (!ai_choose_move(&state, AI_DEFAULT_DEPTH, &move)) {
                printf("L'IA n'a aucun coup legal.\n");
                break;
            }
            perform_move(&state, move);
        } else { /* coup distant, recu par le reseau */
            NetMessage msg;
            if (!network_receive(&net, &msg) || msg.type != NET_MSG_MOVE ||
                board_move_error(&state, state.current_player, msg.move) != NULL) {
                printf("Erreur reseau ou coup adverse invalide : partie interrompue.\n");
                break;
            }
            perform_move(&state, msg.move);
        }
    }

    if (state.status != STATUS_ONGOING) {
        print_final_status(&state);
    }

    network_close(&net);
    return 0;
}
