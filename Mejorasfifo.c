/* restaurante_fifos.c - Simulador de restaurante con FIFOs (pipes con nombre) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>

#define MAX_PEDIDO_LEN 100
#define MAX_PEDIDOS 100

#define FIFO_PEDIDOS "fifo_pedidos"
#define FIFO_MONITOR "fifo_monitor"
#define FIFO_IDS     "fifo_ids"

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
} Pedido;

typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int preparado;
} RegistroMonitor;

void crear_fifo(const char *nombre) {
    if (mkfifo(nombre, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        exit(1);
    }
}

void cocina() {
    crear_fifo(FIFO_PEDIDOS);
    crear_fifo(FIFO_MONITOR);

    int fd_pedidos = open(FIFO_PEDIDOS, O_RDONLY);
    if (fd_pedidos == -1) {
        perror("open fifo_pedidos");
        exit(1);
    }

    int fd_monitor = open(FIFO_MONITOR, O_WRONLY);
    if (fd_monitor == -1) {
        perror("open fifo_monitor");
        exit(1);
    }

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        Pedido pedido;
        if (read(fd_pedidos, &pedido, sizeof(Pedido)) <= 0) continue;

        printf("[Cocina] Preparando pedido del cliente %d: %s\n", pedido.cliente_id, pedido.pedido);

        // Enviar confirmación
        char fifo_conf[32];
        snprintf(fifo_conf, sizeof(fifo_conf), "fifo_conf_%d", pedido.cliente_id);
        crear_fifo(fifo_conf);
        int fd_conf = open(fifo_conf, O_WRONLY);
        int ok = 1;
        write(fd_conf, &ok, sizeof(int));
        close(fd_conf);

        // Simular preparación
        sleep(2);

        // Enviar preparado
        char fifo_prep[32];
        snprintf(fifo_prep, sizeof(fifo_prep), "fifo_preparado_%d", pedido.cliente_id);
        crear_fifo(fifo_prep);
        int fd_prep = open(fifo_prep, O_WRONLY);
        write(fd_prep, &ok, sizeof(int));
        close(fd_prep);

        printf("[Cocina] Pedido de cliente %d listo.\n", pedido.cliente_id);

        RegistroMonitor reg;
        reg.cliente_id = pedido.cliente_id;
        strncpy(reg.pedido, pedido.pedido, MAX_PEDIDO_LEN);
        reg.confirmado = 1;
        reg.preparado = 1;
        write(fd_monitor, &reg, sizeof(reg));
    }

    close(fd_pedidos);
    close(fd_monitor);
}

void cliente() {
    crear_fifo(FIFO_IDS);
    int fd_ids = open(FIFO_IDS, O_RDWR);
    if (fd_ids == -1) {
        perror("open fifo_ids");
        exit(1);
    }

    int id;
    read(fd_ids, &id, sizeof(int));
    lseek(fd_ids, 0, SEEK_SET);
    int next = id + 1;
    write(fd_ids, &next, sizeof(int));
    close(fd_ids);

    int fd_pedidos = open(FIFO_PEDIDOS, O_WRONLY);
    if (fd_pedidos == -1) {
        perror("open fifo_pedidos");
        exit(1);
    }

    while (1) {
        char pedido[MAX_PEDIDO_LEN];
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", id);
        fgets(pedido, MAX_PEDIDO_LEN, stdin);
        if (pedido[0] == '\n') break;
        pedido[strcspn(pedido, "\n")] = 0;
        if (strlen(pedido) == 0) continue;

        Pedido p;
        p.cliente_id = id;
        strncpy(p.pedido, pedido, MAX_PEDIDO_LEN);
        write(fd_pedidos, &p, sizeof(Pedido));

        // Esperar confirmación
        char fifo_conf[32];
        snprintf(fifo_conf, sizeof(fifo_conf), "fifo_conf_%d", id);
        crear_fifo(fifo_conf);
        int fd_conf = open(fifo_conf, O_RDONLY);
        int conf;
        read(fd_conf, &conf, sizeof(int));
        close(fd_conf);
        printf("[Cliente %d] Pedido recibido. Esperando preparación...\n", id);

        // Esperar preparación
        char fifo_prep[32];
        snprintf(fifo_prep, sizeof(fifo_prep), "fifo_preparado_%d", id);
        crear_fifo(fifo_prep);
        int fd_prep = open(fifo_prep, O_RDONLY);
        int prep;
        read(fd_prep, &prep, sizeof(int));
        close(fd_prep);
        printf("[Cliente %d] Pedido preparado. ¡Buen provecho!\n", id);
    }

    close(fd_pedidos);
}

void monitor() {
    crear_fifo(FIFO_MONITOR);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);
    if (fd_monitor == -1) {
        perror("open fifo_monitor");
        exit(1);
    }

    RegistroMonitor lista[MAX_PEDIDOS] = {0};
    int n = 0;

    while (1) {
        RegistroMonitor r;
        if (read(fd_monitor, &r, sizeof(r)) <= 0) continue;

        int encontrado = 0;
        for (int i = 0; i < n; i++) {
            if (lista[i].cliente_id == r.cliente_id && strcmp(lista[i].pedido, r.pedido) == 0) {
                lista[i].confirmado = r.confirmado;
                lista[i].preparado = r.preparado;
                encontrado = 1;
                break;
            }
        }
        if (!encontrado && n < MAX_PEDIDOS) {
            lista[n++] = r;
        }

        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < n; i++) {
            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                   i,
                   lista[i].cliente_id,
                   lista[i].pedido,
                   lista[i].confirmado ? "Sí" : "No",
                   lista[i].preparado ? "Sí" : "No");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
    }

    close(fd_monitor);
}

int proceso_igual_ejecutandose() {
    FILE *fp = popen("pgrep -f restaurante_fifos", "r");
    if (!fp) return 0;

    int count = 0, pid;
    while (fscanf(fp, "%d", &pid) != EOF) {
        if (pid != getpid()) count++;
    }

    pclose(fp);
    return count > 0;
}

void limpiar_fifos_si_ultimo() {
    if (!proceso_igual_ejecutandose()) {
        unlink(FIFO_PEDIDOS);
        unlink(FIFO_MONITOR);
        unlink(FIFO_IDS);
        for (int i = 1; i <= 100; i++) {
            char path[32];
            snprintf(path, sizeof(path), "fifo_conf_%d", i);
            unlink(path);
            snprintf(path, sizeof(path), "fifo_preparado_%d", i);
            unlink(path);
        }
        printf("🧹 Todos los FIFOs han sido eliminados.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    atexit(limpiar_fifos_si_ultimo);

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
