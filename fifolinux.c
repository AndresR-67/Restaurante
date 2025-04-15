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
#define FIFO_ID "/tmp/fifo_id"
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define MAX_PEDIDO_LEN 100

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int cliente_id;
    char estado[32];
    char pedido[MAX_PEDIDO_LEN];
} EstadoPedido;

void cocina();
void cliente();
void monitor();
void crear_fifos();
void limpiar_fifos();
int solicitar_id();

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, (__sighandler_t)limpiar_fifos);
    crear_fifos();

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

// ======================= COCINA =========================

void cocina() {
    Pedido pedido;

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        int fd = open(FIFO_PEDIDOS, O_RDONLY);
        if (fd < 0) continue;

        int r = read(fd, &pedido, sizeof(Pedido));
        close(fd);
        if (r <= 0) continue;

        printf("[Cocina] Pedido de cliente %d: %s\n", pedido.cliente_id, pedido.pedido);

        EstadoPedido estado;
        estado.cliente_id = pedido.cliente_id;
        strncpy(estado.pedido, pedido.pedido, MAX_PEDIDO_LEN);
        strcpy(estado.estado, "Recibido");

        int mon = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);
        write(mon, &estado, sizeof(estado));
        close(mon);

        sleep(1);
        strcpy(estado.estado, "Preparando");
        mon = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);
        write(mon, &estado, sizeof(estado));
        close(mon);

        sleep(2);
        strcpy(estado.estado, "Listo");
        mon = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);
        write(mon, &estado, sizeof(estado));
        close(mon);

        char respuesta_fifo[64];
        sprintf(respuesta_fifo, "/tmp/resp_%d", pedido.cliente_id);
        int resp = open(respuesta_fifo, O_WRONLY);
        if (resp >= 0) {
            char msg[] = "Pedido preparado. ¡Buen provecho!";
            write(resp, msg, sizeof(msg));
            close(resp);
        }
    }
}

// ======================= CLIENTE =========================

void cliente() {
    Pedido pedido;
    int cliente_id = solicitar_id();
    char respuesta_fifo[64];
    sprintf(respuesta_fifo, "/tmp/resp_%d", cliente_id);
    mkfifo(respuesta_fifo, 0666);

    pedido.cliente_id = cliente_id;

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

// ======================= MONITOR =========================

void monitor() {
    EstadoPedido estado;
    EstadoPedido lista[100];
    int total = 0;

    while (1) {
        int fd = open(FIFO_MONITOR, O_RDONLY);
        int r = read(fd, &estado, sizeof(EstadoPedido));
        close(fd);
        if (r <= 0) continue;

        int encontrado = 0;
        for (int i = 0; i < total; i++) {
            if (lista[i].cliente_id == estado.cliente_id) {
                lista[i] = estado;
                encontrado = 1;
                break;
            }
        }
        if (!encontrado && total < 100) {
            lista[total++] = estado;
        }

        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < total; i++) {
            printf("Cliente ID: %d | Pedido: %s | Estado: %s\n",
                   lista[i].cliente_id,
                   lista[i].pedido,
                   lista[i].estado);
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
    }
}

// ======================= FIFOS Y UTILS =========================

void crear_fifos() {
    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_ID, 0666);
    mkfifo(FIFO_MONITOR, 0666);
}

void limpiar_fifos() {
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_ID);
    unlink(FIFO_MONITOR);

    for (int i = 0; i < 100; i++) {
        char tmp[64];
        sprintf(tmp, "/tmp/resp_%d", i);
        unlink(tmp);
    }

    printf("\n[FIFOs limpios]\n");
    exit(0);
}

int solicitar_id() {
    int id = getpid(); // fallback si no hay gestor
    int fd = open(FIFO_ID, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        write(fd, "1", 1);
        close(fd);
        fd = open(FIFO_ID, O_RDONLY);
        read(fd, &id, sizeof(int));
        close(fd);
    }
    return id;
}


