#ifndef KLOSNOWO_AI_H
#define KLOSNOWO_AI_H

#include "board.h"

/* Profondeur de recherche par defaut (en demi-tours). Mesure sur ce
 * plateau (11x7, ~77 cases) : depth=4 reste quasi instantane (~30ms sur
 * la position de depart), depth=6 commence a couter plusieurs secondes
 * faute d'un tri de coups plus fin. 4 est donc un bon compromis force/vitesse. */
#define AI_DEFAULT_DEPTH 4

/* Evalue `state` du point de vue de `player` : un score positif est bon
 * pour `player`, negatif est bon pour son adversaire. Une partie terminee
 * vaut +-1 000 000 (victoire/defaite) ou 0 (egalite). Sinon, combinaison du
 * differentiel de soldats vivants, du differentiel de cases controlees, et
 * de la proximite du roi de `player` par rapport a la cite adverse. */
int ai_evaluate(const GameState *state, Player player);

/* Cherche le meilleur coup pour state->current_player avec un Minimax a
 * elagage Alpha-Beta de profondeur `depth`. Ecrit le coup choisi dans
 * `out_move` et renvoie true, ou renvoie false si `player` n'a aucun coup
 * legal (ne devrait pas arriver en cours de partie normale). */
bool ai_choose_move(const GameState *state, int depth, Move *out_move);

#endif /* KLOSNOWO_AI_H */
