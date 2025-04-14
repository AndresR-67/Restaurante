#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>

#define SHM_NAME "/restaurante_shm"
#define SEM_MUTEX_NAME "/restaurante_mutex"
#define SEM_PEDIDOS_NAME "/restaurante_pedidos"
#define MAX_PEDIDOS 100
#define MAX_PEDIDO_LEN 64
#define MAX_CLIENTES 10

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int pedido_listo;   // 0: no, 1: sí
    int confirmado;     // 0: no recibido, 1: confirmado por cocina
    int activo;         // 1 si el cliente sigue conectado
} Pedido;

typedef struct {
    Pedido cola[MAX_PEDIDOS];
    int inicio;
    int fin;
    int cantidad;
    int clientes_activos;
} ColaPedidos;

ColaPedidos *cola;
sem_t *mutex, *sem_pedidos;
char sem_nombre_confirmacion[128];
int cliente_id_global = -1;

void cleanup_cliente(int signo) {
    if (cliente_id_global != -1) {
        sem_wait(mutex);
        cola->clientes_activos--;
        sem_post(mutex);
        snprintf(sem_nombre_confirmacion, sizeof(sem_nombre_confirmacion), "/restaurante_c_%d", cliente_id_global);
        sem_unlink(sem_nombre_confirmacion);
    }
    exit(0);
}

void cliente() {
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }

    cola = mmap(NULL, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (cola == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    mutex = sem_open(SEM_MUTEX_NAME, 0);
    sem_pedidos = sem_open(SEM_PEDIDOS_NAME, 0);

    // Obtener nuevo cliente_id
    sem_wait(mutex);
    int id = -1;
    for (int i = 0; i < MAX_PEDIDOS; i++) {
        if (!cola->cola[i].activo) {
            id = i;
            cola->cola[i].activo = 1;
            break;
        }
    }
    if (id == -1) {
        printf("Demasiados clientes conectados.\n");
        sem_post(mutex);
        exit(1);
    }
    cola->clientes_activos++;
    sem_post(mutex);
    cliente_id_global = id;

    signal(SIGINT, cleanup_cliente);

    snprintf(sem_nombre_confirmacion, sizeof(sem_nombre_confirmacion), "/restaurante_c_%d", id);
    sem_t *sem_conf = sem_open(sem_nombre_confirmacion, O_CREAT, 0666, 0);

    char input[MAX_PEDIDO_LEN];
    while (1) {
        printf("Cliente %d, ingrese su pedido: ", id);
        fgets(input, MAX_PEDIDO_LEN, stdin);
        input[strcspn(input, "\n")] = '\0';

        // Enviar pedido
        sem_wait(mutex);
        Pedido *nuevo = &cola->cola[cola->fin];
        nuevo->cliente_id = id;
        strcpy(nuevo->pedido, input);
        nuevo->pedido_listo = 0;
        nuevo->confirmado = 0;
        nuevo->activo = 1;

        cola->fin = (cola->fin + 1) % MAX_PEDIDOS;
        cola->cantidad++;
        sem_post(mutex);

        sem_post(sem_pedidos);  // Notificar cocina

        // Esperar confirmación de recibido
        sem_wait(sem_conf);
        printf("Cliente %d: Pedido recibido por la cocina.\n", id);

        // Esperar preparación
        sem_wait(sem_conf);
        printf("Cliente %d: Pedido preparado.\n", id);
    }
}

void cocina() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(ColaPedidos));
    cola = mmap(NULL, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
    sem_pedidos = sem_open(SEM_PEDIDOS_NAME, O_CREAT, 0666, 0);

    printf("Cocina lista. Esperando pedidos...\n");

    while (1) {
        sem_wait(sem_pedidos);

        sem_wait(mutex);
        Pedido *pedido = &cola->cola[cola->inicio];
        int id = pedido->cliente_id;
        char pedido_str[MAX_PEDIDO_LEN];
        strcpy(pedido_str, pedido->pedido);
        cola->inicio = (cola->inicio + 1) % MAX_PEDIDOS;
        cola->cantidad--;
        sem_post(mutex);

        snprintf(sem_nombre_confirmacion, sizeof(sem_nombre_confirmacion), "/restaurante_c_%d", id);
        sem_t *sem_conf = sem_open(sem_nombre_confirmacion, 0);

        printf("Cocina: Recibido pedido del cliente %d: %s\n", id, pedido_str);

        // Confirmar recepción
        sem_wait(mutex);
        pedido->confirmado = 1;
        sem_post(mutex);
        sem_post(sem_conf);

        // Simular preparación
        sleep(2);

        sem_wait(mutex);
        pedido->pedido_listo = 1;
        sem_post(mutex);
        sem_post(sem_conf);
        printf("Cocina: Pedido de cliente %d listo.\n", id);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s cliente|cocina\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else {
        printf("Modo inválido. Usa cliente o cocina.\n");
        return 1;
    }

    return 0;
}
