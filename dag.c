/*
 * dag.c — Modelado del plan como Grafo Acíclico Dirigido  (Parte 1: Marcelo)
 *
 * Decisiones:
 *   - Los nodos se guardan en un arreglo; las aristas usan índices enteros,
 *     no punteros, así el arreglo puede crecer con realloc sin romper nada.
 *   - Cada nodo guarda sus dependencias (deps) y sus sucesores (sucesores).
 *     Con los sucesores, cuando una actividad termina se sabe en O(grado)
 *     a quién avisarle, sin recorrer todo el plan. Importa con 10000 nodos.
 *   - Los IDs se resuelven con una tabla hash (FNV-1a, direccionamiento
 *     abierto), así construir el DAG cuesta O(nodos + aristas).
 *   - Los ciclos se detectan con el algoritmo de Kahn: si el orden
 *     topológico no incluye a todos los nodos, hay un ciclo y el plan se
 *     rechaza antes de crear procesos (si no, el planificador esperaría para
 *     siempre).
 *   - Todos los recorridos son iterativos (sin recursión), para no agotar
 *     la pila con cadenas largas de dependencias.
 */
#define _POSIX_C_SOURCE 200809L

#include "planificador.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Tabla hash ID -> índice ---------- */

static uint32_t hash_fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

int plan_buscar(const Plan *plan, const char *id)
{
    if (!plan->tabla) return -1;
    uint32_t mascara = (uint32_t)plan->tam_tabla - 1;
    for (uint32_t i = hash_fnv1a(id) & mascara; ; i = (i + 1) & mascara) {
        int idx = plan->tabla[i];
        if (idx < 0) return -1;
        if (strcmp(plan->acts[idx].id, id) == 0) return idx;
    }
}

/* Inserta todos los IDs. Retorna -1 si hay un ID repetido. */
static int construir_tabla(Plan *plan)
{
    int tam = 16;
    while (tam < plan->n * 2) tam *= 2;      /* carga <= 50% */
    plan->tabla = malloc((size_t)tam * sizeof *plan->tabla);
    if (!plan->tabla) { perror("malloc"); return -1; }
    plan->tam_tabla = tam;
    for (int i = 0; i < tam; i++) plan->tabla[i] = -1;

    uint32_t mascara = (uint32_t)tam - 1;
    for (int k = 0; k < plan->n; k++) {
        uint32_t i = hash_fnv1a(plan->acts[k].id) & mascara;
        while (plan->tabla[i] >= 0) {
            const Actividad *otra = &plan->acts[plan->tabla[i]];
            if (strcmp(otra->id, plan->acts[k].id) == 0) {
                fprintf(stderr, "Error: ID '%s' repetido (líneas %d y %d)\n",
                        otra->id, otra->linea, plan->acts[k].linea);
                return -1;
            }
            i = (i + 1) & mascara;
        }
        plan->tabla[i] = k;
    }
    return 0;
}

/* ---------- Aristas ---------- */

static int agregar_sucesor(Actividad *a, int idx)
{
    if (a->n_suc == a->cap_suc) {
        int cap = a->cap_suc ? a->cap_suc * 2 : 4;
        int *tmp = realloc(a->sucesores, (size_t)cap * sizeof *tmp);
        if (!tmp) { perror("realloc"); return -1; }
        a->sucesores = tmp;
        a->cap_suc = cap;
    }
    a->sucesores[a->n_suc++] = idx;
    return 0;
}

/* Traduce los IDs de dependencias de 'a' (índice k) a índices del arreglo. */
static int resolver_dependencias(Plan *plan, int k)
{
    Actividad *a = &plan->acts[k];
    if (a->n_dep_ids == 0) return 0;

    a->deps = malloc((size_t)a->n_dep_ids * sizeof *a->deps);
    if (!a->deps) { perror("malloc"); return -1; }

    for (int j = 0; j < a->n_dep_ids; j++) {
        int d = plan_buscar(plan, a->dep_ids[j]);
        if (d < 0) {
            fprintf(stderr, "Error (línea %d): '%s' depende de '%s', que no existe\n",
                    a->linea, a->id, a->dep_ids[j]);
            return -1;
        }
        if (d == k) {
            fprintf(stderr, "Error (línea %d): '%s' depende de sí misma\n",
                    a->linea, a->id);
            return -1;
        }
        int repetida = 0;                  /* "4 : x : 10 : 1, 1" -> una arista */
        for (int r = 0; r < a->n_deps; r++)
            if (a->deps[r] == d) { repetida = 1; break; }
        if (repetida) continue;

        a->deps[a->n_deps++] = d;
        /* Ojo: plan->acts[d] puede ser 'a' misma solo si d == k (ya descartado). */
        if (agregar_sucesor(&plan->acts[d], k) < 0) return -1;
    }
    return 0;
}

static void liberar_dep_ids(Actividad *a)
{
    for (int j = 0; j < a->n_dep_ids; j++) free(a->dep_ids[j]);
    free(a->dep_ids);
    a->dep_ids = NULL;
    a->n_dep_ids = 0;
}

/* ---------- Orden topológico (Kahn) ---------- */

int plan_orden_topologico(const Plan *plan, int *orden)
{
    int *grado = malloc((size_t)plan->n * sizeof *grado);
    if (!grado) { perror("malloc"); return -1; }

    /* 'orden' funciona también como cola: se lee desde 'ini', se escribe en 'fin' */
    int ini = 0, fin = 0;
    for (int i = 0; i < plan->n; i++) {
        grado[i] = plan->acts[i].n_deps;
        if (grado[i] == 0) orden[fin++] = i;
    }
    while (ini < fin) {
        const Actividad *a = &plan->acts[orden[ini++]];
        for (int s = 0; s < a->n_suc; s++)
            if (--grado[a->sucesores[s]] == 0) orden[fin++] = a->sucesores[s];
    }
    free(grado);
    return fin;   /* si fin < plan->n, hay un ciclo */
}

static void reportar_ciclo(const Plan *plan, const int *orden, int en_orden)
{
    char *visto = calloc((size_t)plan->n, 1);
    if (!visto) return;
    for (int i = 0; i < en_orden; i++) visto[orden[i]] = 1;

    fprintf(stderr, "Error: el plan tiene un ciclo; no es un DAG.\n"
                    "Actividades involucradas o bloqueadas por el ciclo:");
    int mostradas = 0;
    for (int i = 0; i < plan->n; i++) {
        if (visto[i]) continue;
        if (mostradas < 20) fprintf(stderr, " %s", plan->acts[i].id);
        mostradas++;
    }
    if (mostradas > 20) fprintf(stderr, " ... (%d en total)", mostradas);
    fprintf(stderr, "\n");
    free(visto);
}

/* ---------- API pública ---------- */

int plan_construir_dag(Plan *plan)
{
    if (construir_tabla(plan) < 0) return -1;

    for (int k = 0; k < plan->n; k++)
        if (resolver_dependencias(plan, k) < 0) return -1;

    for (int k = 0; k < plan->n; k++) {
        Actividad *a = &plan->acts[k];
        liberar_dep_ids(a);
        a->deps_pendientes = a->n_deps;
        a->estado = (a->n_deps == 0) ? EST_LISTA : EST_PENDIENTE;
    }

    int *orden = malloc((size_t)plan->n * sizeof *orden);
    if (!orden) { perror("malloc"); return -1; }
    int en_orden = plan_orden_topologico(plan, orden);
    if (en_orden >= 0 && en_orden < plan->n) reportar_ciclo(plan, orden, en_orden);
    free(orden);
    return (en_orden == plan->n) ? 0 : -1;
}

/*
 * Aislamiento de errores: marca como ABORTADA toda actividad que dependa,
 * directa o indirectamente, de 'idx_fallida'. Las demás ramas del plan no se
 * tocan y siguen ejecutándose. Solo se abortan nodos que aún no empezaron
 * (PENDIENTE o LISTA); un descendiente nunca puede estar corriendo, porque
 * necesitaba que la fallida terminara bien.
 */
int plan_abortar_rama(Plan *plan, int idx_fallida)
{
    int *pila = malloc((size_t)plan->n * sizeof *pila);
    if (!pila) { perror("malloc"); return -1; }

    int tope = 0, abortadas = 0;
    pila[tope++] = idx_fallida;
    while (tope > 0) {
        const Actividad *a = &plan->acts[pila[--tope]];
        for (int s = 0; s < a->n_suc; s++) {
            Actividad *h = &plan->acts[a->sucesores[s]];
            if (h->estado == EST_PENDIENTE || h->estado == EST_LISTA) {
                h->estado = EST_ABORTADA;    /* marcar antes de apilar: */
                pila[tope++] = a->sucesores[s]; /* cada nodo entra una vez */
                abortadas++;
            }
        }
    }
    free(pila);
    return abortadas;
}

static const char *nombre_estado(Estado e)
{
    switch (e) {
    case EST_PENDIENTE:  return "PENDIENTE";
    case EST_LISTA:      return "LISTA";
    case EST_EJECUTANDO: return "EJECUTANDO";
    case EST_TERMINADA:  return "TERMINADA";
    case EST_FALLIDA:    return "FALLIDA";
    case EST_ABORTADA:   return "ABORTADA";
    }
    return "?";
}

void plan_imprimir(const Plan *plan)
{
    printf("%-10s %-24s %8s  %-10s %s\n", "ID", "NOMBRE", "TIEMPO", "ESTADO", "DEPENDE DE");
    for (int i = 0; i < plan->n; i++) {
        const Actividad *a = &plan->acts[i];
        printf("%-10s %-24s %6dms%s %-10s", a->id, a->nombre, a->tiempo_ms,
               a->tiempo_aleatorio ? "*" : " ", nombre_estado(a->estado));
        for (int j = 0; j < a->n_deps; j++)
            printf("%s%s", j ? ", " : "", plan->acts[a->deps[j]].id);
        printf("\n");
    }
    printf("(* = tiempo asignado aleatoriamente)\n");
}

void plan_liberar(Plan *plan)
{
    for (int i = 0; i < plan->n; i++) {
        liberar_dep_ids(&plan->acts[i]);
        free(plan->acts[i].deps);
        free(plan->acts[i].sucesores);
    }
    free(plan->acts);
    free(plan->tabla);
    memset(plan, 0, sizeof *plan);
}
