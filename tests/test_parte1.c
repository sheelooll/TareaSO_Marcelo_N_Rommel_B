/*
 * tests/test_parte1.c — Pruebas de la Parte 1 (Marcelo), sin el planificador.
 *
 * Compilar y ejecutar:   make test
 * o a mano:              ./test_parte1 ejemplos/plan.txt
 *
 * Prueba: parseo, DAG y orden topológico, paso de mensajes por pipes con un
 * proceso hijo real, falla simulada, insumos incompletos y aborto de rama.
 */
#define _POSIX_C_SOURCE 200809L

#include "../planificador.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fallos = 0;

static void chequear(int ok, const char *que)
{
    printf("  [%s] %s\n", ok ? " OK " : "FALLA", que);
    if (!ok) fallos++;
}

/*
 * Lanza 'a' en un proceso hijo, le manda 'n_insumos' mensajes por un pipe
 * y lee su respuesta por otro pipe. Así lo hará el planificador real.
 * Devuelve el código de salida del hijo; deja la respuesta en 'respuesta'.
 */
static int correr_hijo(const Plan *plan, const Actividad *a, int n_insumos,
                       char respuesta[MAX_MSG], int *hubo_respuesta)
{
    int entrada[2], salida[2];
    if (pipe(entrada) < 0 || pipe(salida) < 0) { perror("pipe"); exit(1); }

    fflush(stdout);                      /* evita salida duplicada tras fork */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {                      /* ---- HIJO ---- */
        close(entrada[1]);
        close(salida[0]);
        _exit(actividad_ejecutar(a, entrada[0], salida[1], 1));
    }
    /* ---- PADRE ---- */
    close(entrada[0]);
    close(salida[1]);

    char msg[MAX_MSG];
    for (int i = 0; i < n_insumos; i++) {
        const Actividad *dep = (i < a->n_deps) ? &plan->acts[a->deps[i]] : a;
        msg_formatear_insumo(dep, msg);
        msg_enviar(entrada[1], msg);
    }
    close(entrada[1]);                   /* EOF: no hay más insumos */

    *hubo_respuesta = (msg_recibir(salida[0], respuesta) == 1);
    close(salida[0]);

    int estado;
    waitpid(pid, &estado, 0);
    return WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: %s plan.txt\n", argv[0]);
        return 1;
    }

    /* ---------- 1. Parseo y DAG ---------- */
    printf("== 1. Parseo y DAG: %s\n", argv[1]);
    Plan plan;
    if (plan_cargar(argv[1], &plan) < 0) {
        printf("  El archivo fue rechazado (ver error arriba).\n");
        return 2;
    }
    printf("  %d actividades leídas\n", plan.n);
    if (plan_construir_dag(&plan) < 0) {
        printf("  El DAG fue rechazado (ver error arriba).\n");
        plan_liberar(&plan);
        return 2;
    }
    if (plan.n <= 40) plan_imprimir(&plan);

    int *orden = malloc((size_t)plan.n * sizeof *orden);
    int en_orden = plan_orden_topologico(&plan, orden);
    chequear(en_orden == plan.n, "orden topológico incluye todas las actividades");

    /* Cada dependencia debe aparecer antes que quien depende de ella */
    int *posicion = malloc((size_t)plan.n * sizeof *posicion);
    for (int i = 0; i < en_orden; i++) posicion[orden[i]] = i;
    int orden_ok = 1;
    long aristas = 0;
    for (int i = 0; i < plan.n; i++)
        for (int j = 0; j < plan.acts[i].n_deps; j++, aristas++)
            if (posicion[plan.acts[i].deps[j]] >= posicion[i]) orden_ok = 0;
    chequear(orden_ok, "toda dependencia va antes que su dependiente");
    printf("  %ld aristas en el DAG\n", aristas);

    if (plan.n <= 40) {
        printf("  Orden topológico:");
        for (int i = 0; i < en_orden; i++) printf(" %s", plan.acts[orden[i]].id);
        printf("\n");
    }
    free(posicion);
    free(orden);

    /* Elegimos la actividad con más dependencias para probar los pipes */
    int elegida = 0;
    for (int i = 1; i < plan.n; i++)
        if (plan.acts[i].n_deps > plan.acts[elegida].n_deps) elegida = i;
    Actividad prueba = plan.acts[elegida];
    if (prueba.tiempo_ms > 300) prueba.tiempo_ms = 300;   /* que la prueba sea rápida */
    if (strstr(prueba.nombre, "falla"))                    /* la prueba 2 no debe fallar */
        snprintf(prueba.nombre, sizeof prueba.nombre, "prueba_ok");

    char resp[MAX_MSG];
    int hubo;

    /* ---------- 2. Pipes con un hijo real ---------- */
    printf("\n== 2. Pipes: '%s' recibe %d insumos y responde\n", prueba.id, prueba.n_deps);
    int cod = correr_hijo(&plan, &prueba, prueba.n_deps, resp, &hubo);
    chequear(cod == ACT_OK, "el hijo termina con ACT_OK");
    chequear(hubo, "el padre recibe el mensaje del hijo");
    if (hubo) printf("  mensaje: \"%s\"\n", resp);

    /* ---------- 3. Falla simulada ---------- */
    printf("\n== 3. Falla simulada (nombre con \"falla\")\n");
    Actividad mala = prueba;
    snprintf(mala.nombre, sizeof mala.nombre, "falla_parrilla");
    cod = correr_hijo(&plan, &mala, mala.n_deps, resp, &hubo);
    chequear(cod == ACT_FALLA, "el hijo termina con ACT_FALLA");
    chequear(!hubo, "una actividad fallida no envía mensaje");

    /* ---------- 4. Insumos incompletos ---------- */
    if (prueba.n_deps > 0) {
        printf("\n== 4. Insumos incompletos (se envían %d de %d)\n",
               prueba.n_deps - 1, prueba.n_deps);
        cod = correr_hijo(&plan, &prueba, prueba.n_deps - 1, resp, &hubo);
        chequear(cod == ACT_ERROR_INSUMO, "el hijo detecta el EOF y termina con ACT_ERROR_INSUMO");
    }

    /* ---------- 5. Aislamiento de errores ---------- */
    int raiz = -1;
    for (int i = 0; i < plan.n && raiz < 0; i++)
        if (plan.acts[i].n_deps == 0 && plan.acts[i].n_suc > 0) raiz = i;
    if (raiz >= 0) {
        printf("\n== 5. Si '%s' falla, se aborta solo su rama\n", plan.acts[raiz].id);
        plan.acts[raiz].estado = EST_FALLIDA;
        int n_abort = plan_abortar_rama(&plan, raiz);
        printf("  abortadas (%d):", n_abort);
        int intactas = 0, mostradas = 0;
        for (int i = 0; i < plan.n; i++) {
            if (plan.acts[i].estado == EST_ABORTADA) {
                if (mostradas++ < 20) printf(" %s", plan.acts[i].id);
            } else if (i != raiz) {
                intactas++;
            }
        }
        if (mostradas > 20) printf(" ...");
        printf("\n  siguen en pie: %d actividades\n", intactas);
        chequear(n_abort >= plan.acts[raiz].n_suc, "sus sucesores directos quedan abortados");
        chequear(plan_abortar_rama(&plan, raiz) == 0, "abortar dos veces no repite trabajo");
    }

    plan_liberar(&plan);
    printf("\n%s (%d fallas)\n", fallos ? "HAY PRUEBAS FALLIDAS" : "TODAS LAS PRUEBAS PASARON", fallos);
    return fallos ? 1 : 0;
}
