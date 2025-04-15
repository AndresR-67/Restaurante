// restaurante_fifo.c - Simulador de restaurante con FIFOs

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
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define FIFO_ID "/tmp/fifo_id"

#define MAX_PEDIDO_LEN 100
#define MAX_CLIENTES 100

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int numero;
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int recibido;
    int preparado;
} EstadoPedido;

EstadoPedido pedidos_monitor[MAX_CLIENTES];
int total_pedidos = 0;

// ---------- MAIN ----------
void cocina();
void cliente();
void monitor();
void limpiar_fifos();
void inicializar_fifo_ids();

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, limpiar_fifos);
    inicializar_fifo_ids();

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else {
        printf("Argument
