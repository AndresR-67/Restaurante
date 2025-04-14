#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>

#define MAX_PEDIDOS 10
#define MAX_PEDIDO_LEN 100
#define SHM_NAME "/restaurante_mem"
#define SEM_MUTEX_NAME "/restaurante_mutex"
#define SEM_PEDIDOS_NAME "/restaurante_pedidos"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int pedido_listo;
    int activo;
} Pedido;

typedef struct {
    Pedido cola[MAX_PEDIDOS];
    int inicio;
    int fin;
    int cantidad;
    int clientes_activos;
} ColaPedidos;

ColaPedidos *cola;
sem_t *mutex;
sem_t *sem_pedidos;
int cliente_id_global = -1;
char sem_nombre_confirmacion[64];

void cleanup_cliente(int signo) {
    if (cliente_id_global != -1) {
        sem_wait(mutex);
        cola->cola[cliente_id_global].activo = 0;
        cola->clientes_activos--;
        sem_post(mutex);
        snprintf(sem_nombre_confirmacion, sizeof(sem_nombre_confirmacion), "/restaurante_c_%d", cliente_id_global);
        sem_unlink(sem_nombre_confirmacion);
    }
    printf("\nCliente %d cerró sesión.\n", cliente_id_global);
    exit(0);
}

void cliente() {
    signal(SIGINT, cleanup_cliente);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(ColaPedidos));
    cola = mmap(NULL, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
    sem_pedidos = sem_open(SEM_PEDIDOS_NAME, O_CREAT, 0666, 0);

    sem_wait(mutex);
    int id = -1;
    for (int i = 0; i < MAX_PEDIDOS; i++) {
        if (!cola->cola[i].activo) {
            id = i;
            cola->cola[i].activo = 1;
            cola->clientes_activos++;
            break;
        }
    }
    sem_post(mutex);

    if (id == -1) {
        printf("Demasiados clientes conectados. Intente más tarde.\n");
        exit(1);
    }

    cliente_id_global = id;
    snprintf(sem_nombre_confirmacion, sizeof(sem_nombre_confirmacion), "/restaurante_c_%d", id);
    sem_t *sem_conf = sem_open(sem_nombre_confirmacion, O_CREAT, 0666, 0);

    printf("Cliente %d conectado. Puede realizar pedidos.\n", id);

    while (1) {
        char pedido[MAX_PEDIDO_LEN];
        printf("Ingrese su pedido: ");
        fgets(pedido, MAX_PEDIDO_LEN, stdin);
        pedido[strcspn(pedido, "\n")] = 0;

        sem_wait(mutex);
        if (cola->cantidad == MAX_PEDIDOS) {
            sem_post(mutex);
            printf("Cola llena. Intente más tarde.\n");
            continue;
        }

        cola->cola[id].cliente_id = id;
        strncpy(cola->cola[id].pedido, pedido, MAX_PEDIDO_LEN);
        cola->cola[id].confirmado = 0;
        cola->cola[id].pedido_listo = 0;

        cola->cola[cola->fin] = cola->cola[id];
        cola->fin = (cola->fin + 1) % MAX_PEDIDOS;
        cola->cantidad++;

        sem_post(mutex);
        sem_post(sem_pedidos);

        sem_wait(sem_conf);
        printf("Cocina recibió su pedido.\n");
        sem_wait(sem_conf);
        printf("Su pedido está listo.\n");
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
        if (cola->cantidad == 0 && cola->clientes_activos == 0) {
            sem_post(mutex);
            printf("Cocina: No hay más clientes. Cerrando y limpiando recursos...\n");
            sem_close(mutex);
            sem_close(sem_pedidos);
            sem_unlink(SEM_MUTEX_NAME);
            sem_unlink(SEM_PEDIDOS_NAME);
            shm_unlink(SHM_NAME);
            munmap(cola, sizeof(ColaPedidos));
            exit(0);
        }

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

        sem_wait(mutex);
        pedido->confirmado = 1;
        sem_post(mutex);
        sem_post(sem_conf);

        sleep(2);

        sem_wait(mutex);
        pedido->pedido_listo = 1;
        sem_post(mutex);
        sem_post(sem_conf);

        printf("Cocina: Pedido de cliente %d listo.\n", id);
    }
}

void monitor() {
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd < 0) {
        perror("No se puede abrir memoria compartida");
        exit(1);
    }
    cola = mmap(NULL, sizeof(ColaPedidos), PROT_READ, MAP_SHARED, shm_fd, 0);

    printf("Monitor de pedidos en tiempo real:\n");

    while (1) {
        printf("\033[2J\033[H");  // limpiar pantalla
        printf("Clientes activos: %d\n", cola->clientes_activos);
        printf("Pedidos en cola: %d\n", cola->cantidad);
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (cola->cola[i].activo) {
                printf("Cliente %d: %s | Confirmado: %d | Listo: %d\n",
                       cola->cola[i].cliente_id,
                       cola->cola[i].pedido,
                       cola->cola[i].confirmado,
                       cola->cola[i].pedido_listo);
            }
        }
        fflush(stdout);
        sleep(2);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s [cliente | cocina | monitor]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else {
        printf("Argumento inválido.\n");
        return 1;
    }

    return 0;
}
