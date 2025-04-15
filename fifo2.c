/* restaurante_fifo.c - Simulador de restaurante usando FIFOs */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_PEDIDO_LEN 100
#define FIFO_PEDIDOS "fifo_pedidos"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int confirmado;
    int pedido_listo;
} EstadoPedido;

void cocina() {
    mkfifo(FIFO_PEDIDOS, 0666);
    int fd_pedidos = open(FIFO_PEDIDOS, O_RDWR); // O_RDWR requerido

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        Pedido p;
        int n = read(fd_pedidos, &p, sizeof(Pedido));
        if (n <= 0) continue;

        printf("[Cocina] Preparando pedido del cliente %d: %s\n", p.cliente_id, p.pedido);

        // Enviar confirmación
        char fifo_cliente[50];
        snprintf(fifo_cliente, sizeof(fifo_cliente), "fifo_cliente_%d", p.cliente_id);
        mkfifo(fifo_cliente, 0666);
        int fd_cliente = open(fifo_cliente, O_WRONLY);

        EstadoPedido estado = {1, 0};
        write(fd_cliente, &estado, sizeof(EstadoPedido));

        sleep(2); // Simula preparación

        estado.pedido_listo = 1;
        write(fd_cliente, &estado, sizeof(EstadoPedido));
        close(fd_cliente);

        printf("[Cocina] Pedido de cliente %d listo.\n", p.cliente_id);
    }

    close(fd_pedidos);
    unlink(FIFO_PEDIDOS);
}

void cliente() {
    static int id_counter = 1;
    int id = getpid() % 10000; // ID único más simple

    char pedido[MAX_PEDIDO_LEN];

    while (1) {
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", id);
        fgets(pedido, MAX_PEDIDO_LEN, stdin);
        if (pedido[0] == '\n') break;
        pedido[strcspn(pedido, "\n")] = 0;

        if (strlen(pedido) == 0) continue;

        Pedido p = { id, "" };
        strncpy(p.pedido, pedido, MAX_PEDIDO_LEN);

        int fd_pedidos = open(FIFO_PEDIDOS, O_WRONLY);
        write(fd_pedidos, &p, sizeof(Pedido));
        close(fd_pedidos);

        // Escuchar respuesta por su FIFO
        char fifo_cliente[50];
        snprintf(fifo_cliente, sizeof(fifo_cliente), "fifo_cliente_%d", id);
        mkfifo(fifo_cliente, 0666);
        int fd_resp = open(fifo_cliente, O_RDONLY);

        EstadoPedido estado;
        read(fd_resp, &estado, sizeof(EstadoPedido));
        if (estado.confirmado)
            printf("[Cliente %d] Pedido recibido. Esperando preparación...\n", id);

        while (!estado.pedido_listo) {
            read(fd_resp, &estado, sizeof(EstadoPedido));
            sleep(1);
        }

        printf("[Cliente %d] Pedido preparado. ¡Buen provecho!\n", id);
        close(fd_resp);
        unlink(fifo_cliente);
    }
}

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int listo;
} RegistroPedido;

void monitor() {
    printf("[Monitor] Escuchando logs de cocina (simulado)...\n");
    RegistroPedido registros[100];
    int count = 0;

    while (1) {
        system("clear");
        printf("\n--- ESTADO DE LOS PEDIDOS (Monitor) ---\n\n");
        for (int i = 0; i < count; i++) {
            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                   i,
                   registros[i].cliente_id,
                   registros[i].pedido,
                   registros[i].confirmado ? "Sí" : "No",
                   registros[i].listo ? "Sí" : "No");
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

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor(); // monitor es simulado sin memoria compartida real
    } else {
        printf("Argumento inválido. Use cliente, cocina o monitor.\n");
        return 1;
    }

    return 0;
}
