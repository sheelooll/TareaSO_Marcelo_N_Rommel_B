/*
 * actividad.c — Lo que hace cada proceso hijo  (Parte 1: Marcelo)
 *
 * Flujo del hijo:
 *   1. Restaura SIGINT/SIGTERM a su acción por defecto (así Ctrl+C lo
 *      termina aunque el padre haya instalado un manejador antes del fork).
 *   2. Lee un insumo por cada dependencia desde fd_entrada.
 *   3. Simula el trabajo durmiendo tiempo_ms (nanosleep, sin busy-waiting).
 *   4. Decide si falla (ver "Fallas simuladas" abajo).
 *   5. Si no falló, escribe su mensaje en fd_salida para sus dependientes.
 *
 * Fallas simuladas (para probar el aislamiento de errores):
 *   - La actividad falla si su nombre contiene "falla" (ej.: "falla_parrilla").
 *   - Opcional: variable de entorno PROB_FALLA con una probabilidad entre 0 y 1,
 *     por ejemplo:  PROB_FALLA=0.05 ./planificador plan.txt 4
 */
#define _POSIX_C_SOURCE 200809L

#include "planificador.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Duerme ms milisegundos. Si una señal interrumpe el sueño, sigue durmiendo
   solo lo que faltaba. */
static void dormir_ms(int ms)
{
    struct timespec t = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) < 0 && errno == EINTR)
        ;
}

static int contiene_falla(const char *nombre)
{
    char minus[MAX_NOMBRE];
    size_t i = 0;
    for (; nombre[i] && i < sizeof minus - 1; i++)
        minus[i] = (char)tolower((unsigned char)nombre[i]);
    minus[i] = '\0';
    return strstr(minus, "falla") != NULL;
}

static int debe_fallar(const Actividad *a)
{
    if (contiene_falla(a->nombre)) return 1;

    const char *p = getenv("PROB_FALLA");
    if (p) {
        double prob = strtod(p, NULL);
        if (prob > 0 && (double)rand() / RAND_MAX < prob) return 1;
    }
    return 0;
}

static int ejecutar(const Actividad *a, int fd_entrada, int fd_salida, int verbose)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    srand((unsigned)getpid() ^ (unsigned)time(NULL));  /* semilla distinta por hijo */

    /* 1) Insumos de las dependencias */
    char msg[MAX_MSG];
    for (int i = 0; i < a->n_deps; i++) {
        int r = (fd_entrada >= 0) ? msg_recibir(fd_entrada, msg) : 0;
        if (r != 1) {
            fprintf(stderr, "[%s] recibió %d de %d insumos; no puede continuar\n",
                    a->id, i, a->n_deps);
            return ACT_ERROR_INSUMO;
        }
        if (verbose)
            printf("  [pid %d] %s recibe: %s\n", (int)getpid(), a->id, msg);
    }
    if (fd_entrada >= 0) close(fd_entrada);

    /* 2) Trabajo simulado */
    dormir_ms(a->tiempo_ms);

    /* 3) ¿Falla interna? */
    if (debe_fallar(a)) {
        fprintf(stderr, "  [pid %d] %s (%s) falló internamente\n",
                (int)getpid(), a->id, a->nombre);
        return ACT_FALLA;
    }

    /* 4) Mensaje para los dependientes */
    msg_formatear_insumo(a, msg);
    if (fd_salida >= 0) {
        if (msg_enviar(fd_salida, msg) < 0) return ACT_ERROR_PIPE;
        close(fd_salida);
    }
    return ACT_OK;
}

int actividad_ejecutar(const Actividad *a, int fd_entrada, int fd_salida,
                       int verbose)
{
    int codigo = ejecutar(a, fd_entrada, fd_salida, verbose);
    fflush(stdout);   /* el hijo termina con _exit(), que no vacía buffers */
    return codigo;
}
