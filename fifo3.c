#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

#define FIFO_PEDIDOS "fifo_pedidos"
#define FIFO_CONTROL "fifo_control"
#define FIFO_MONITOR "fifo_monitor"
#define MAX_PEDIDO_LEN 100

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int listo;
} RegistroPedido;

void cliente() {
    char pedido[MAX_PEDIDO_LEN];
    char fifo_cliente[50];
    Pedido p;
    int fd_control, fd_pedidos, fd_cliente;

    // Solicitar ID a cocina
    fd_control = open(FIFO_CONTROL, O_WRONLY);
    int dummy = 1;
    write(fd_control, &dummy, sizeof(int));
    close(fd_control);

    fd_control = open(FIFO_CONTROL, O_RDONLY);
    read(fd_control, &p.cliente_id, sizeof(int));
    close(fd_control);

    snprintf(fifo_cliente, sizeof(fifo_cliente), "fifo_cliente_%d", p.cliente_id);
    mkfifo(fifo_cliente, 0666);

    // Pedido al cliente
    printf("[Cliente %d] Ingrese su pedido: ", p.cliente_id);
    fgets(p.pedido, MAX_PEDIDO_LEN, stdin);
    p.pedido[strcspn(p.pedido, "\n")] = 0;

    // Enviar pedido
    fd_pedidos = open(FIFO_PEDIDOS, O_WRONLY);
    write(fd_pedidos, &p, sizeof(Pedido));
    close(fd_pedidos);

    // Esperar respuesta
    fd_cliente = open(fifo_cliente, O_RDONLY);
    char respuesta[100];
    read(fd_cliente, respuesta, sizeof(respuesta));
    close(fd_cliente);

    printf("[Cliente %d] Respuesta: %s\n", p.cliente_id, respuesta);
    unlink(fifo_cliente);
}

void cocina() {
    int fd_pedidos, fd_cliente, fd_control, fd_monitor;
    Pedido p;
    static int id_counter = 1;

    mkfifo(FIFO_PEDIDOS, 0666);
    mkfifo(FIFO_CONTROL, 0666);
    mkfifo(FIFO_MONITOR, 0666);

    fd_pedidos = open(FIFO_PEDIDOS, O_RDONLY);
    fd_control = open(FIFO_CONTROL, O_RDWR);
    fd_monitor = open(FIFO_MONITOR, O_WRONLY);

    printf("[Cocina] Esperando pedidos...\n");

    fd_set readfds;
    int maxfd = fd_pedidos > fd_control ? fd_pedidos : fd_control;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(fd_pedidos, &readfds);
        FD_SET(fd_control, &readfds);

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            exit(1);
        }

        if (FD_ISSET(fd_control, &readfds)) {
            int dummy;
            read(fd_control, &dummy, sizeof(int));
            write(fd_control, &id_counter, sizeof(int));
            id_counter++;
        }

        if (FD_ISSET(fd_pedidos, &readfds)) {
            read(fd_pedidos, &p, sizeof(Pedido));

            // Confirmación al monitor
            char buffer[200];
            snprintf(buffer, sizeof(buffer), "%d|%s|CONFIRMADO\n", p.cliente_id, p.pedido);
            write(fd_monitor, buffer, strlen(buffer));

            // Simular preparación
            sleep(2);

            // Enviar pedido listo al cliente
            char fifo_cliente[50];
            snprintf(fifo_cliente, sizeof(fifo_cliente), "fifo_cliente_%d", p.cliente_id);
            fd_cliente = open(fifo_cliente, O_WRONLY);
            char respuesta[] = "Su pedido está listo.";
            write(fd_cliente, respuesta, sizeof(respuesta));
            close(fd_cliente);

            // Avisar al monitor
            snprintf(buffer, sizeof(buffer), "%d|%s|LISTO\n", p.cliente_id, p.pedido);
            write(fd_monitor, buffer, strlen(buffer));
        }
    }
}

void monitor() {
    mkfifo(FIFO_MONITOR, 0666);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);

    RegistroPedido registros[100];
    int count = 0;

    printf("[Monitor] Observando pedidos...\n");

    while (1) {
        char linea[200];
        int bytes = read(fd_monitor, linea, sizeof(linea) - 1);
        if (bytes > 0) {
            linea[bytes] = '\0';
            int id;
            char pedido[MAX_PEDIDO_LEN], estado[20];
            sscanf(linea, "%d|%[^|]|%s", &id, pedido, estado);

            int encontrado = 0;
            for (int i = 0; i < count; i++) {
                if (registros[i].cliente_id == id) {
                    if (strcmp(estado, "CONFIRMADO") == 0)
                        registros[i].confirmado = 1;
                    if (strcmp(estado, "LISTO") == 0)
                        registros[i].listo = 1;
                    encontrado = 1;
                    break;
                }
            }

            if (!encontrado) {
                registros[count].cliente_id = id;
                strcpy(registros[count].pedido, pedido);
                registros[count].confirmado = strcmp(estado, "CONFIRMADO") == 0;
                registros[count].listo = strcmp(estado, "LISTO") == 0;
                count++;
            }

            system("clear");
            printf("\n--- ESTADO DE LOS PEDIDOS (Monitor) ---\n\n");
            for (int i = 0; i < count; i++) {
                printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                       i + 1,
                       registros[i].cliente_id,
                       registros[i].pedido,
                       registros[i].confirmado ? "Sí" : "No",
                       registros[i].listo ? "Sí" : "No");
            }
            printf("\nPresiona Ctrl+C para salir del monitor.\n");
        }
        usleep(500000);
    }
}
