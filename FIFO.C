#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define FIFO_PEDIDOS "fifo_pedidos"
#define FIFO_RESPUESTAS "fifo_respuestas"
#define FIFO_MONITOR "fifo_monitor"
#define MAX_PEDIDO 256

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO];
} Pedido;

typedef struct {
    int cliente_id;
    char mensaje[MAX_PEDIDO];
    int preparado;
} Respuesta;

int fd_pedidos, fd_respuestas, fd_monitor; // globales para limpieza

// Limpieza automática al cerrar cocina
void limpiar_fifos(int signo) {
    printf("\n[Cocina] Cerrando y limpiando recursos...\n");
    close(fd_pedidos);
    close(fd_respuestas);
    close(fd_monitor);
    unlink(FIFO_PEDIDOS);
    unlink(FIFO_RESPUESTAS);
    unlink(FIFO_MONITOR);
    exit(0);
}

// Función de la cocina
void cocina() {
    Pedido p;
    Respuesta r;

    signal(SIGINT, limpiar_fifos);

    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_RESPUESTAS, 0666);
    mkfifo(FIFO_MONITOR, 0666);

    fd_pedidos = open(FIFO_PEDIDOS, O_RDONLY);
    fd_respuestas = open(FIFO_RESPUESTAS, O_WRONLY);
    fd_monitor = open(FIFO_MONITOR, O_WRONLY);

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        if (read(fd_pedidos, &p, sizeof(Pedido)) > 0) {
            printf("[Cocina] Pedido recibido de %d: %s\n", p.cliente_id, p.pedido);

            snprintf(r.mensaje, sizeof(r.mensaje), "Pedido recibido: %s", p.pedido);
            r.cliente_id = p.cliente_id;
            r.preparado = 0;
            write(fd_respuestas, &r, sizeof(Respuesta));

            write(fd_monitor, &p, sizeof(Pedido));

            sleep(2); // Simular preparación

            snprintf(r.mensaje, sizeof(r.mensaje), "Pedido preparado: %s", p.pedido);
            r.preparado = 1;
            write(fd_respuestas, &r, sizeof(Respuesta));

            printf("[Cocina] Pedido de %d listo.\n", p.cliente_id);
        }
    }
}

// Función del cliente
void cliente() {
    Pedido p;
    Respuesta r;
    int fd_pedidos = open(FIFO_PEDIDOS, O_WRONLY);
    int fd_respuestas = open(FIFO_RESPUESTAS, O_RDONLY);
    int cliente_id = getpid();

    while (1) {
        printf("Cliente %d, ingrese su pedido (o 'salir'): ", cliente_id);
        fgets(p.pedido, MAX_PEDIDO, stdin);
        p.pedido[strcspn(p.pedido, "\n")] = 0;

        if (strcmp(p.pedido, "salir") == 0) break;

        p.cliente_id = cliente_id;
        write(fd_pedidos, &p, sizeof(Pedido));

        do {
            read(fd_respuestas, &r, sizeof(Respuesta));
        } while (r.cliente_id != cliente_id);

        printf("[Cliente %d] %s\n", cliente_id, r.mensaje);

        if (!r.preparado) {
            do {
                read(fd_respuestas, &r, sizeof(Respuesta));
            } while (r.cliente_id != cliente_id || r.preparado == 0);

            printf("[Cliente %d] %s\n", cliente_id, r.mensaje);
        }
    }

    close(fd_pedidos);
    close(fd_respuestas);
}

// Función del monitor
void monitor() {
    Pedido p;
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);

    printf("[Monitor] Observando pedidos en tiempo real:\n");

    while (1) {
        if (read(fd_monitor, &p, sizeof(Pedido)) > 0) {
            time_t now = time(NULL);
            char *timestamp = ctime(&now);
            timestamp[strcspn(timestamp, "\n")] = 0;
            printf("[Monitor %s] Cliente %d pidió: %s\n", timestamp, p.cliente_id, p.pedido);
        }
    }

    close(fd_monitor);
}

// main
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
        monitor();
    } else {
        printf("Modo desconocido: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
