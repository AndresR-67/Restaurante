// fiforestaurante.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#define MAX_PEDIDO_LEN 100
#define MAX_PEDIDOS 10

#define FIFO_CLIENTE "/tmp/fifo_cliente"
#define FIFO_COCINA "/tmp/fifo_cocina"
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define FIFO_IDS "/tmp/fifo_ids"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int pedido_listo;
} Pedido;

Pedido pedidos[MAX_PEDIDOS];
int cantidad = 0;
int id_counter = 1;

// Manejo de limpieza al salir
void limpiar_fifos() {
    unlink(FIFO_CLIENTE);
    unlink(FIFO_COCINA);
    unlink(FIFO_MONITOR);
    unlink(FIFO_IDS);
    printf("FIFOs eliminados.\n");
}

void sigint_handler(int signo) {
    limpiar_fifos();
    exit(0);
}

// Cocina
void cocina() {
    signal(SIGINT, sigint_handler);
    mkfifo(FIFO_CLIENTE, 0666);
    mkfifo(FIFO_COCINA, 0666);
    mkfifo(FIFO_MONITOR, 0666);

    // Evitar bloqueo
    int dummy_write = open(FIFO_CLIENTE, O_WRONLY | O_NONBLOCK);
    int fd_cliente = open(FIFO_CLIENTE, O_RDONLY);
    int fd_cocina = open(FIFO_COCINA, O_WRONLY);
    int fd_monitor = open(FIFO_MONITOR, O_WRONLY);

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        Pedido p;
        int r = read(fd_cliente, &p, sizeof(Pedido));
        if (r <= 0) continue;

        if (cantidad >= MAX_PEDIDOS) continue;

        printf("[Cocina] Preparando pedido del cliente %d: %s\n", p.cliente_id, p.pedido);
        p.confirmado = 1;
        write(fd_cocina, &p, sizeof(Pedido));

        sleep(2); // Simula preparación
        p.pedido_listo = 1;

        printf("[Cocina] Pedido de cliente %d listo.\n", p.cliente_id);
        write(fd_monitor, &p, sizeof(Pedido));
        write(fd_cocina, &p, sizeof(Pedido));

        // Actualiza lista
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (pedidos[i].cliente_id == p.cliente_id && strcmp(pedidos[i].pedido, p.pedido) == 0) {
                pedidos[i] = p;
                break;
            }
        }
    }
}

// Cliente
void cliente() {
    mkfifo(FIFO_CLIENTE, 0666);
    mkfifo(FIFO_COCINA, 0666);
    mkfifo(FIFO_MONITOR, 0666);
    mkfifo(FIFO_IDS, 0666);

    // Evitar bloqueo
    int dummy_write = open(FIFO_COCINA, O_WRONLY | O_NONBLOCK);
    int fd_cliente = open(FIFO_CLIENTE, O_WRONLY);
    int fd_cocina = open(FIFO_COCINA, O_RDONLY);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);
    int fd_ids = open(FIFO_IDS, O_RDWR);

    // Obtener ID
    int id;
    read(fd_ids, &id, sizeof(int));
    id_counter++;
    lseek(fd_ids, 0, SEEK_SET);
    write(fd_ids, &id_counter, sizeof(int));

    while (1) {
        char pedido[MAX_PEDIDO_LEN];
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", id);
        fgets(pedido, MAX_PEDIDO_LEN, stdin);
        if (pedido[0] == '\n') break;
        pedido[strcspn(pedido, "\n")] = 0;

        if (strlen(pedido) == 0) continue;

        Pedido p = {id, "", 0, 0};
        strncpy(p.pedido, pedido, MAX_PEDIDO_LEN);

        write(fd_cliente, &p, sizeof(Pedido));

        read(fd_cocina, &p, sizeof(Pedido));
        if(p.confirmado){
            printf("[Cliente %d] Pedido confirmado. Esperando preparacion... \n", id);
        }
        //espera que este listo
        while (1) {
            Pedido respuesta;
            int r = read(fd_cocina, &respuesta, sizeof(Pedido));
            if (r > 0 && respuesta.cliente_id == id && strcmp(respuesta.pedido, pedido) == 0 && respuesta.pedido_listo) {
                printf("[Cliente %d] Pedido preparado. ¡Buen provecho!\n", id);
                break;
            }
            sleep(1);
        }

        
    }

    close(fd_cliente);
    close(fd_cocina);
    close(fd_monitor);
    close(fd_ids);
}

// Monitor
void monitor() {
    mkfifo(FIFO_MONITOR, 0666);
    int dummy_write = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);

    while (1) {
        Pedido p;
        int r = read(fd_monitor, &p, sizeof(Pedido));
        if (r <= 0) continue;

        int actualizado = 0;
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (pedidos[i].cliente_id == p.cliente_id && strcmp(pedidos[i].pedido, p.pedido) == 0) {
                pedidos[i] = p;
                actualizado = 1;
                break;
            }
        }
        if (!actualizado && cantidad < MAX_PEDIDOS) {
            pedidos[cantidad++] = p;
        }

        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < cantidad; i++) {
            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                i,
                pedidos[i].cliente_id,
                pedidos[i].pedido,
                pedidos[i].confirmado ? "Sí" : "No",
                pedidos[i].pedido_listo ? "Sí" : "No");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        fflush(stdout);
        sleep(1);
    }

    close(fd_monitor);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor|inicializar]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else if (strcmp(argv[1], "inicializar") == 0) {
        mkfifo(FIFO_IDS, 0666);
        int fd = open(FIFO_IDS, O_WRONLY | O_CREAT);
        int inicial = 1;
        write(fd, &inicial, sizeof(int));
        close(fd);
        printf("FIFO_IDS inicializado con ID = 1\n");
    } else {
        printf("Argumento inválido. Usa cliente, cocina, monitor o inicializar.\n");
        return 1;
    }

    return 0;
}
