/* restaurante_fifo.c - Simulador de restaurante con FIFOs */

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

#define FIFO_PEDIDOS "/tmp/fifo_pedidos"
#define FIFO_RESPUESTA "/tmp/fifo_respuesta"
#define MAX_PEDIDO_LEN 100
#define MAX_CLIENTES 100

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int cliente_id;
    char estado[32];
    char pedido[MAX_PEDIDO_LEN];
} EstadoPedido;

EstadoPedido pedidos_monitor[MAX_CLIENTES];
int total_pedidos = 0;

void limpiar_fifo() {
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_RESPUESTA);
}

void cliente() {
    Pedido pedido;
    char respuesta_fifo[64];
    int cliente_id = getpid();  // ID único por proceso
    pedido.cliente_id = cliente_id;

    // Crear FIFO de respuesta
    sprintf(respuesta_fifo, "/tmp/resp_%d", cliente_id);
    mkfifo(respuesta_fifo, 0666);

    while (1) {
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", cliente_id);
        fgets(pedido.pedido, MAX_PEDIDO_LEN, stdin);
        if (pedido.pedido[0] == '\n') break;
        pedido.pedido[strcspn(pedido.pedido, "\n")] = 0;
        if (strlen(pedido.pedido) == 0) continue;

        int fd = open(FIFO_PEDIDOS, O_WRONLY);
        write(fd, &pedido, sizeof(Pedido));
        close(fd);

        int resp = open(respuesta_fifo, O_RDONLY);
        char estado[64];
        read(resp, estado, sizeof(estado));
        close(resp);

        printf("[Cliente %d] %s\n", cliente_id, estado);

        if (strcmp(estado, "Pedido preparado. ¡Buen provecho!") == 0)
            break;
    }

    unlink(respuesta_fifo);
}

void cocina() {
    Pedido pedido;
    EstadoPedido estado;
    int fd;

    // Crear FIFO si no existe
    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_RESPUESTA, 0666);

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        fd = open(FIFO_PEDIDOS, O_RDONLY);
        if (fd < 0) continue;
        int r = read(fd, &pedido, sizeof(Pedido));
        close(fd);
        if (r <= 0) continue;

        printf("[Cocina] Preparando pedido de cliente %d: %s\n", pedido.cliente_id, pedido.pedido);

        // Registrar para el monitor
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
            strcpy(pedidos_monitor[total_pedidos].estado, "Recibido");
            total_pedidos++;
        } else if (encontrado) {
            strncpy(pedidos_monitor[i].pedido, pedido.pedido, MAX_PEDIDO_LEN);
            strcpy(pedidos_monitor[i].estado, "Recibido");
        }

        // Simula confirmación y preparación
        sleep(1);
        if (encontrado)
            strcpy(pedidos_monitor[i].estado, "Preparando");

        sleep(2);

        // Pedido listo
        char respuesta_fifo[64];
        sprintf(respuesta_fifo, "/tmp/resp_%d", pedido.cliente_id);
        int resp = open(respuesta_fifo, O_WRONLY);
        char estado_final[] = "Pedido preparado. ¡Buen provecho!";
        write(resp, estado_final, sizeof(estado_final));
        close(resp);

        if (encontrado)
            strcpy(pedidos_monitor[i].estado, "Listo");
    }
}

void monitor() {
    while (1) {
        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < total_pedidos; i++) {
            printf("Cliente ID: %d | Pedido: %s | Estado: %s\n",
                   pedidos_monitor[i].cliente_id,
                   pedidos_monitor[i].pedido,
                   pedidos_monitor[i].estado);
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, limpiar_fifo);

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

