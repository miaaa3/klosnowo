#include "network.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

bool network_host(NetConnection *conn, int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return false; }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return false;
    }
    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        return false;
    }

    printf("En attente d'une connexion sur le port %d...\n", port);
    fflush(stdout);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    close(listen_fd); /* on ne joue qu'a deux : plus besoin d'ecouter apres accept() */
    if (client_fd < 0) {
        perror("accept");
        return false;
    }

    conn->socket_fd = client_fd;
    conn->recv_len = 0;
    printf("Client connecte depuis %s.\n", inet_ntoa(client_addr.sin_addr));
    return true;
}

bool network_connect(NetConnection *conn, const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return false; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Adresse IP invalide : %s\n", host);
        close(fd);
        return false;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return false;
    }

    conn->socket_fd = fd;
    conn->recv_len = 0;
    return true;
}

void network_close(NetConnection *conn) {
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
}

static bool send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

bool network_send_move(NetConnection *conn, Move move) {
    char line[64];
    int n = snprintf(line, sizeof(line), "MOVE %d %d %d %d\n",
                      move.from.x, move.from.y, move.to.x, move.to.y);
    return n > 0 && send_all(conn->socket_fd, line, (size_t)n);
}

bool network_send_barricade(NetConnection *conn, Position pos) {
    char line[64];
    int n = snprintf(line, sizeof(line), "BARRICADE %d %d\n", pos.x, pos.y);
    return n > 0 && send_all(conn->socket_fd, line, (size_t)n);
}

/* Extrait une ligne (sans le '\n') du tampon de reception et decale le
 * reste. false si aucune ligne complete n'est encore disponible. */
static bool pop_line(NetConnection *conn, char *out, size_t out_size) {
    for (size_t i = 0; i < conn->recv_len; i++) {
        if (conn->recv_buf[i] != '\n') continue;

        size_t line_len = i < out_size - 1 ? i : out_size - 1;
        memcpy(out, conn->recv_buf, line_len);
        out[line_len] = '\0';

        size_t remaining = conn->recv_len - (i + 1);
        memmove(conn->recv_buf, conn->recv_buf + i + 1, remaining);
        conn->recv_len = remaining;
        return true;
    }
    return false;
}

static NetMessageType parse_line(const char *line, Move *move, Position *pos) {
    int a, b, c, d;
    if (sscanf(line, "MOVE %d %d %d %d", &a, &b, &c, &d) == 4) {
        move->from = (Position){ (int8_t)a, (int8_t)b };
        move->to   = (Position){ (int8_t)c, (int8_t)d };
        return NET_MSG_MOVE;
    }
    if (sscanf(line, "BARRICADE %d %d", &a, &b) == 2) {
        pos->x = (int8_t)a;
        pos->y = (int8_t)b;
        return NET_MSG_BARRICADE;
    }
    return NET_MSG_ERROR;
}

bool network_receive(NetConnection *conn, NetMessage *out) {
    char line[NETWORK_RECV_BUF_SIZE];

    while (!pop_line(conn, line, sizeof(line))) {
        if (conn->recv_len >= sizeof(conn->recv_buf)) {
            out->type = NET_MSG_ERROR; /* ligne anormalement longue : on signale plutot que de planter */
            return true;
        }
        ssize_t n = recv(conn->socket_fd, conn->recv_buf + conn->recv_len,
                          sizeof(conn->recv_buf) - conn->recv_len, 0);
        if (n <= 0) {
            out->type = NET_MSG_DISCONNECTED;
            return false;
        }
        conn->recv_len += (size_t)n;
    }

    out->type = parse_line(line, &out->move, &out->pos);
    return true;
}
