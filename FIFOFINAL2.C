/* restaurante_fifo.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#define FIFO_PEDIDOS "/tmp/fifo_pedidos"
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define FIFO_ID "/tmp/fifo_id"
#define MAX_PEDIDO_LEN 100
#define MAX_CLIENTES 100

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int recibido;
    int preparado;
} EstadoPedido;

EstadoPedido pedidos_monitor[MAX_CLIENTES];
int total_pedidos = 0;

/* === MAIN === */
void limpiar_fifos() {
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_MONITOR);
    unlink(FIFO_ID);
}

void inicializar_fifos() {
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_MONITOR);
    unlink(FIFO_ID);

    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_MONITOR, 0666);
    mkfifo(FIFO_ID, 0666);
}

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

/* === COCINA === */
void cocina() {
    Pedido pedido;
    int fd_pedidos, fd_monitor, fd_id;
    int next_id = 1;

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        fd_id = open(FIFO_ID, O_WRONLY | O_NONBLOCK);
        if (fd_id >= 0) {
            write(fd_id, &next_id, sizeof(int));
            close(fd_id);
            next_id++;
        }

        fd_pedidos = open(FIFO_PEDIDOS, O_RDONLY);
        if (fd_pedidos < 0) continue;
        int r = read(fd_pedidos, &pedido, sizeof(Pedido));
        close(fd_pedidos);
        if (r <= 0) continue;

        printf("[Cocina] Preparando pedido del cliente %d: %s\n", pedido.cliente_id, pedido.pedido);
        sleep(2); // simulando preparación
        printf("[Cocina] Pedido del cliente %d listo\n", pedido.cliente_id);

        // Actualizar estado para monitor
        fd_monitor = open(FIFO_MONITOR, O_WRONLY);
        write(fd_monitor, &pedido, sizeof(Pedido));
        close(fd_monitor);

        // Responder al cliente
        char fifo_resp[64];
        sprintf(fifo_resp, "/tmp/resp_%d", pedido.cliente_id);
        int resp_fd = open(fifo_resp, O_WRONLY);
        if (resp_fd >= 0) {
            char msg[] = "pedido preparado ¡Buen provecho!";
            write(resp_fd, msg, sizeof(msg));
            close(resp_fd);
        }
    }
}

/* === CLIENTE === */
void cliente() {
    char pedido_texto[MAX_PEDIDO_LEN];
    char fifo_resp[64];
    int cliente_id;

    // Obtener ID único
    int id_fifo = open(FIFO_ID, O_RDONLY);
    read(id_fifo, &cliente_id, sizeof(int));
    close(id_fifo);

    sprintf(fifo_resp, "/tmp/resp_%d", cliente_id);
    mkfifo(fifo_resp, 0666);

    while (1) {
        printf("[Cliente %d]: Ingrese su pedido (Enter para salir): ", cliente_id);
        fgets(pedido_texto, MAX_PEDIDO_LEN, stdin);
        if (pedido_texto[0] == '\n') break;
        pedido_texto[strcspn(pedido_texto, "\n")] = 0;

        Pedido pedido;
        pedido.cliente_id = cliente_id;
        strncpy(pedido.pedido, pedido_texto, MAX_PEDIDO_LEN);

        int fd = open(FIFO_PEDIDOS, O_WRONLY);
        write(fd, &pedido, sizeof(Pedido));
        close(fd);

        printf("[Cliente %d]: pedido recibido, Esperando preparación\n", cliente_id);

        int resp_fd = open(fifo_resp, O_RDONLY);
        char buffer[100];
        read(resp_fd, buffer, sizeof(buffer));
        close(resp_fd);

        printf("[Cliente %d]: %s\n", cliente_id, buffer);
    }

    unlink(fifo_resp);
}

/* === MONITOR === */
void monitor() {
    Pedido pedido;
    int fd;
    printf("[Monitor] Iniciado...\n");

    while (1) {
        fd = open(FIFO_MONITOR, O_RDONLY);
        read(fd, &pedido, sizeof(Pedido));
        close(fd);

        int i;
        int encontrado = 0;
        for (i = 0; i < total_pedidos; i++) {
            if (pedidos_monitor[i].cliente_id == pedido.cliente_id) {
                encontrado = 1;
                break;
            }
        }

        if (!encontrado && total_pedidos < MAX_CLIENTES) {
            pedidos_monitor[total_pedidos].cliente_id = pedido.cliente_id;
            strncpy(pedidos_monitor[total_pedidos].pedido, pedido.pedido, MAX_PEDIDO_LEN);
            pedidos_monitor[total_pedidos].recibido = 1;
            pedidos_monitor[total_pedidos].preparado = 1;
            total_pedidos++;
        } else if (encontrado) {
            pedidos_monitor[i].recibido = 1;
            pedidos_monitor[i].preparado = 1;
        }

        // Mostrar estado
        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int j = 0; j < total_pedidos; j++) {
            printf("[%d] Cliente ID:%d | pedido \"%s\" | Recibido: %s | Preparado: %s\n",
                   j + 1,
                   pedidos_monitor[j].cliente_id,
                   pedidos_monitor[j].pedido,
                   pedidos_monitor[j].recibido ? "sí" : "no",
                   pedidos_monitor[j].preparado ? "sí" : "no");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
    }
}
