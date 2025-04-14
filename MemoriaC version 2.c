/* restaurante.c - Simulador de restaurante con IDs consecutivos */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_PEDIDOS 10
#define MAX_PEDIDO_LEN 100
#define SHM_NAME "/cola_pedidos"
#define SEM_MUTEX "/sem_mutex"
#define SEM_PEDIDOS "/sem_pedidos"
#define SEM_CONFIRMACION "/sem_conf"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int pedido_listo;
} Pedido;

typedef struct {
    Pedido cola[MAX_PEDIDOS];
    int inicio;
    int fin;
    int cantidad;
    int siguiente_id;
} ColaPedidos;

void inicializar_cola(ColaPedidos *cola) {
    cola->inicio = 0;
    cola->fin = 0;
    cola->cantidad = 0;
    cola->siguiente_id = 1;
    for (int i = 0; i < MAX_PEDIDOS; i++) {
        cola->cola[i].cliente_id = -1;
        memset(cola->cola[i].pedido, 0, MAX_PEDIDO_LEN);
        cola->cola[i].confirmado = 0;
        cola->cola[i].pedido_listo = 0;
    }
}

void monitor() {
    int shm_fd;
    ColaPedidos *cola;

    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("No se pudo abrir la memoria compartida");
        exit(1);
    }

    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    while (1) {
        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (cola->cola[i].cliente_id == -1 || strlen(cola->cola[i].pedido) == 0)
                continue;
            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                   i,
                   cola->cola[i].cliente_id,
                   cola->cola[i].pedido,
                   cola->cola[i].confirmado ? "S\u00ed" : "No",
                   cola->cola[i].pedido_listo ? "S\u00ed" : "No");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        sleep(1);
    }

    munmap(cola, sizeof(ColaPedidos));
    close(shm_fd);
}

void cocina() {
    int shm_fd;
    ColaPedidos *cola;
    sem_t *mutex, *sem_pedidos, *sem_conf;

    shm_unlink(SHM_NAME);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_PEDIDOS);
    sem_unlink(SEM_CONFIRMACION);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(ColaPedidos));
    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    inicializar_cola(cola);

    mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);
    sem_pedidos = sem_open(SEM_PEDIDOS, O_CREAT, 0666, 0);
    sem_conf = sem_open(SEM_CONFIRMACION, O_CREAT, 0666, 0);

    printf("[Cocina] Esperando pedidos...\n");
    while (1) {
        sem_wait(sem_pedidos);
        sem_wait(mutex);

        if (cola->cantidad > 0) {
            Pedido *p = &cola->cola[cola->inicio];
            printf("[Cocina] Preparando pedido del cliente %d: %s\n", p->cliente_id, p->pedido);
            p->confirmado = 1;
            sem_post(sem_conf);

            sleep(2);

            p->pedido_listo = 1;
            printf("[Cocina] Pedido de cliente %d listo.\n", p->cliente_id);

            cola->inicio = (cola->inicio + 1) % MAX_PEDIDOS;
            cola->cantidad--;
        }

        sem_post(mutex);
    }

    munmap(cola, sizeof(ColaPedidos));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    sem_close(mutex);
    sem_close(sem_pedidos);
    sem_close(sem_conf);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_PEDIDOS);
    sem_unlink(SEM_CONFIRMACION);

    printf("[Cocina] Recursos liberados.\n");
}

void cliente() {
    int shm_fd;
    ColaPedidos *cola;
    sem_t *mutex, *sem_pedidos, *sem_conf;

    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    mutex = sem_open(SEM_MUTEX, 0);
    sem_pedidos = sem_open(SEM_PEDIDOS, 0);
    sem_conf = sem_open(SEM_CONFIRMACION, 0);

    char pedido[MAX_PEDIDO_LEN];
    int id;

    sem_wait(mutex);
    id = cola->siguiente_id++;
    sem_post(mutex);

    while (1) {
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", id);
        fgets(pedido, MAX_PEDIDO_LEN, stdin);
        if (pedido[0] == '\n') break;
        pedido[strcspn(pedido, "\n")] = 0;

        if (strlen(pedido) == 0) continue;

        sem_wait(mutex);

        if (cola->cantidad == MAX_PEDIDOS) {
            printf("[Cliente %d] Cola llena. Intente más tarde.\n", id);
            sem_post(mutex);
            continue;
        }

        Pedido *p = &cola->cola[cola->fin];
        p->cliente_id = id;
        strncpy(p->pedido, pedido, MAX_PEDIDO_LEN);
        p->confirmado = 0;
        p->pedido_listo = 0;

        cola->fin = (cola->fin + 1) % MAX_PEDIDOS;
        cola->cantidad++;

        sem_post(mutex);
        sem_post(sem_pedidos);

        sem_wait(sem_conf);
        printf("[Cliente %d] Pedido recibido. Esperando preparación...\n", id);

        while (1) {
            sem_wait(mutex);
            int listo = 0;
            for (int i = 0; i < MAX_PEDIDOS; i++) {
                if (cola->cola[i].cliente_id == id && cola->cola[i].pedido_listo) {
                    listo = 1;
                    break;
                }
            }
            sem_post(mutex);
            if (listo) break;
            sleep(1);
        }
        printf("[Cliente %d] Pedido preparado. \u00a1Buen provecho!\n", id);
    }

    munmap(cola, sizeof(ColaPedidos));
    close(shm_fd);
    sem_close(mutex);
    sem_close(sem_pedidos);
    sem_close(sem_conf);
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
        monitor();
    } else {
        printf("Argumento inv\u00e1lido. Use cliente, cocina o monitor.\n");
        return 1;
    }

    return 0;
}

