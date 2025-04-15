// restaurante_fifo.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define MAX_PEDIDO_LEN 100
#define MAX_PEDIDOS 100

#define FIFO_PEDIDOS "/tmp/fifo_pedidos"
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define FIFO_ID "/tmp/fifo_id"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int numero_pedido;
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int recibido;
    int preparado;
} EstadoPedido;

EstadoPedido lista_pedidos[MAX_PEDIDOS];
int total_pedidos = 0;

// ========== MAIN ==========

void cliente();
void cocina();
void monitor();
void inicializar_fifos();
void inicializar_fifo_ids();
void limpiar_fifos();

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    inicializar_fifos();
    signal(SIGINT, (void *)limpiar_fifos);

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else {
        printf("Argumento inválido. Use cliente, cocina o monitor.\n");
        return 1;
    }

    return 0;
}

// ========== COCINA ==========

void cocina() {
    Pedido pedido;

    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_MONITOR, 0666);

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        int fd = open(FIFO_PEDIDOS, O_RDONLY);
        if (fd < 0) continue;
        int r = read(fd, &pedido, sizeof(Pedido));
        close(fd);
        if (r <= 0) continue;

        printf("[Cocina] Preparando pedido del cliente %d: \"%s\"\n", pedido.cliente_id, pedido.pedido);
        sleep(1);

        // Enviar al monitor que fue recibido
        EstadoPedido nuevo;
        nuevo.numero_pedido = 0;
        nuevo.cliente_id = pedido.cliente_id;
        strncpy(nuevo.pedido, pedido.pedido, MAX_PEDIDO_LEN);
        nuevo.recibido = 1;
        nuevo.preparado = 0;

        int mfd = open(FIFO_MONITOR, O_WRONLY);
        write(mfd, &nuevo, sizeof(EstadoPedido));
        close(mfd);

        // Simular preparación
        sleep(2);
        nuevo.preparado = 1;
        mfd = open(FIFO_MONITOR, O_WRONLY);
        write(mfd, &nuevo, sizeof(EstadoPedido));
        close(mfd);

        printf("[Cocina] Pedido del cliente %d listo\n", pedido.cliente_id);

        // Responder al cliente
        char resp_fifo[64];
        sprintf(resp_fifo, "/tmp/resp_%d", pedido.cliente_id);
        int resp = open(resp_fifo, O_WRONLY);
        char msg[] = "pedido preparado ¡Buen provecho!";
        write(resp, msg, sizeof(msg));
        close(resp);
    }
}

// ========== CLIENTE ==========

void cliente() {
    mkfifo(FIFO_ID, 0666);

    char req[2] = "X";
    int fd = open(FIFO_ID, O_WRONLY);
    write(fd, req, sizeof(req));
    close(fd);

    int id;
    fd = open(FIFO_ID, O_RDONLY);
    read(fd, &id, sizeof(int));
    close(fd);

    char respuesta_fifo[64];
    sprintf(respuesta_fifo, "/tmp/resp_%d", id);
    mkfifo(respuesta_fifo, 0666);

    Pedido pedido;
    pedido.cliente_id = id;

    while (1) {
        printf("[Cliente %d]: Ingrese su pedido (Enter para salir): ", id);
        fflush(stdout);
        fgets(pedido.pedido, MAX_PEDIDO_LEN, stdin);

        if (pedido.pedido[0] == '\n') break;

        pedido.pedido[strcspn(pedido.pedido, "\n")] = 0;

        if (strlen(pedido.pedido) == 0) continue;

        int fd = open(FIFO_PEDIDOS, O_WRONLY);
        write(fd, &pedido, sizeof(Pedido));
        close(fd);

        printf("[Cliente %d]: pedido recibido, Esperando preparación\n", id);

        char buffer[128];
        fd = open(respuesta_fifo, O_RDONLY);
        read(fd, buffer, sizeof(buffer));
        close(fd);
        printf("[Cliente %d]: %s\n", id, buffer);
    }

    unlink(respuesta_fifo);
}

// ========== MONITOR ==========

void monitor() {
    mkfifo(FIFO_MONITOR, 0666);

    while (1) {
        int fd = open(FIFO_MONITOR, O_RDONLY);
        EstadoPedido info;
        while (read(fd, &info, sizeof(EstadoPedido)) > 0) {
            int encontrado = 0;
            for (int i = 0; i < total_pedidos; i++) {
                if (lista_pedidos[i].cliente_id == info.cliente_id &&
                    strcmp(lista_pedidos[i].pedido, info.pedido) == 0) {
                    lista_pedidos[i].recibido = info.recibido;
                    lista_pedidos[i].preparado = info.preparado;
                    encontrado = 1;
                    break;
                }
            }
            if (!encontrado && total_pedidos < MAX_PEDIDOS) {
                info.numero_pedido = total_pedidos + 1;
                lista_pedidos[total_pedidos++] = info;
            }
        }
        close(fd);

        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < total_pedidos; i++) {
            printf("[%d] Cliente ID:%d | pedido \"%s\" | Recibido: %s | Preparado: %s\n",
                   lista_pedidos[i].numero_pedido,
                   lista_pedidos[i].cliente_id,
                   lista_pedidos[i].pedido,
                   lista_pedidos[i].recibido ? "sí" : "no",
                   lista_pedidos[i].preparado ? "sí" : "no");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        sleep(1);
    }
}

// ========== FIFOS ==========

void inicializar_fifos() {
    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_MONITOR, 0666);
    mkfifo(FIFO_ID, 0666);
    inicializar_fifo_ids();
}

__attribute__((constructor))
void inicializar_fifo_ids() {
    mkfifo(FIFO_ID, 0666);
    if (fork() == 0) {
        int contador = 1;
        char buf[2];
        while (1) {
            int fd_w = open(FIFO_ID, O_RDONLY);
            read(fd_w, buf, sizeof(buf));
            close(fd_w);

            int fd_r = open(FIFO_ID, O_WRONLY);
            write(fd_r, &contador, sizeof(int));
            close(fd_r);

            contador++;
        }
        exit(0);
    }
}

void limpiar_fifos() {
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_MONITOR);
    unlink(FIFO_ID);
    for (int i = 0; i < 1000; i++) {
        char nombre[64];
        sprintf(nombre, "/tmp/resp_%d", i);
        unlink(nombre);
    }
    exit(0);
}
