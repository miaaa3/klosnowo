#ifndef KLOSNOWO_NETWORK_H
#define KLOSNOWO_NETWORK_H

#include <stddef.h>
#include "board.h"

/* Petite couche reseau texte au-dessus de TCP : chaque message est une
 * ligne ASCII terminee par '\n' ("MOVE x1 y1 x2 y2\n" ou "BARRICADE x y\n").
 * Bloquante : recv()/accept()/connect() attendent sans limite de temps,
 * comme decrit dans le sujet (synchronisation par tour : on attend le coup
 * adverse avant de continuer). */

#define NETWORK_RECV_BUF_SIZE 256

typedef struct {
    int socket_fd;
    char recv_buf[NETWORK_RECV_BUF_SIZE];
    size_t recv_len; /* octets deja recus mais pas encore consommes (ligne incomplete) */
} NetConnection;

/* Serveur : cree un socket d'ecoute sur `port`, bloque jusqu'a ce qu'un
 * client se connecte (accept()), puis referme le socket d'ecoute. `conn`
 * est pret a envoyer/recevoir avec le client une fois la fonction revenue.
 * Retourne false en cas d'erreur systeme (affiche avec perror). */
bool network_host(NetConnection *conn, int port);

/* Client : se connecte a `host`:`port`. Retourne false en cas d'erreur
 * (hote invalide, connexion refusee, etc., affichee avec perror). */
bool network_connect(NetConnection *conn, const char *host, int port);

/* Ferme la connexion. Sans effet si elle est deja fermee. */
void network_close(NetConnection *conn);

/* Envoie un coup ou un placement de barricade. Retournent false si l'envoi
 * echoue (pair deconnecte). */
bool network_send_move(NetConnection *conn, Move move);
bool network_send_barricade(NetConnection *conn, Position pos);

typedef enum {
    NET_MSG_MOVE,
    NET_MSG_BARRICADE,
    NET_MSG_DISCONNECTED, /* le pair a ferme la connexion */
    NET_MSG_ERROR         /* ligne recue mais non reconnue */
} NetMessageType;

typedef struct {
    NetMessageType type;
    Move move;       /* valide si type == NET_MSG_MOVE */
    Position pos;    /* valide si type == NET_MSG_BARRICADE */
} NetMessage;

/* Bloque jusqu'a recevoir une ligne complete du pair, et la parse. Gere la
 * fragmentation TCP (une ligne peut arriver en plusieurs paquets, ou
 * plusieurs lignes dans un seul paquet) via le tampon interne de `conn`. */
bool network_receive(NetConnection *conn, NetMessage *out);

#endif /* KLOSNOWO_NETWORK_H */
