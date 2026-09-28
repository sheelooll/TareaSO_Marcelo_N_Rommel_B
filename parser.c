/*
 * parser.c — Lectura de plan.txt  (Parte 1: Marcelo)
 *
 * Formato de cada línea:
 *     ID : Nombre : tiempo_ms : Dep1, Dep2, ...
 *
 * Decisiones:
 *   - Se ignoran líneas vacías y líneas que empiezan con '#'.
 *   - Se toleran espacios extra, '\r' de archivos de Windows y corchetes
 *     opcionales en la lista de dependencias: "[1, 2]" o "1, 2".
 *   - El último ':' es opcional si no hay dependencias.
 *   - Si el tiempo viene vacío se sortea en [TIEMPO_MIN, TIEMPO_MAX].
 *   - Ante una línea mal formada se informa "archivo:línea: motivo" y se
 *     rechaza el archivo completo: es mejor no ejecutar un plan a medias.
 */
#define _POSIX_C_SOURCE 200809L

#include "planificador.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Quita espacios al inicio y al final, modificando la cadena. */
static char *recortar(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *fin = s + strlen(s);
    while (fin > s && isspace((unsigned char)fin[-1])) fin--;
    *fin = '\0';
    return s;
}

/* Igual que strsep(): corta *resto en el primer separador y avanza.
   (strsep no es C estándar ni POSIX, así que usamos nuestra versión.) */
static char *separar(char **resto, char sep)
{
    char *inicio = *resto;
    if (!inicio) return NULL;
    char *p = strchr(inicio, sep);
    if (p) { *p = '\0'; *resto = p + 1; }
    else   { *resto = NULL; }
    return inicio;
}

/* IDs alfanuméricos; se aceptan también '_' y '-'. */
static int id_valido(const char *id)
{
    if (*id == '\0') return 0;
    for (const char *p = id; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return 0;
    return 1;
}

/* Agrega un espacio vacío al final de plan->acts, creciendo si hace falta. */
static Actividad *nueva_actividad(Plan *plan)
{
    if (plan->n == plan->cap) {
        int nueva_cap = plan->cap ? plan->cap * 2 : 64;
        Actividad *tmp = realloc(plan->acts, (size_t)nueva_cap * sizeof *tmp);
        if (!tmp) return NULL;
        plan->acts = tmp;
        plan->cap  = nueva_cap;
    }
    Actividad *a = &plan->acts[plan->n++];
    memset(a, 0, sizeof *a);
    a->estado    = EST_PENDIENTE;
    a->pid       = -1;
    a->fd_salida = -1;
    return a;
}

/* Separa "1, 2, 3" (con o sin corchetes) y guarda cada ID en a->dep_ids. */
static int leer_dependencias(char *texto, Actividad *a,
                             const char *ruta, int nlinea)
{
    texto = recortar(texto);
    size_t len = strlen(texto);
    if (len > 0 && texto[0] == '[') {
        if (texto[len - 1] != ']') {
            fprintf(stderr, "%s:%d: falta ']' en las dependencias\n", ruta, nlinea);
            return -1;
        }
        texto[len - 1] = '\0';
        texto++;
    }

    int cap = 0;
    char *resto = texto;
    char *tok;
    while ((tok = separar(&resto, ',')) != NULL) {
        tok = recortar(tok);
        if (*tok == '\0') continue;          /* tolera "1,,2" o coma final */
        if (!id_valido(tok) || strlen(tok) >= MAX_ID) {
            fprintf(stderr, "%s:%d: dependencia inválida '%s'\n", ruta, nlinea, tok);
            return -1;
        }
        if (a->n_dep_ids == cap) {
            cap = cap ? cap * 2 : 4;
            char **tmp = realloc(a->dep_ids, (size_t)cap * sizeof *tmp);
            if (!tmp) { perror("realloc"); return -1; }
            a->dep_ids = tmp;
        }
        a->dep_ids[a->n_dep_ids] = strdup(tok);
        if (!a->dep_ids[a->n_dep_ids]) { perror("strdup"); return -1; }
        a->n_dep_ids++;
    }
    return 0;
}

/* Procesa una línea ya sin comentarios. Retorna 0 ok, -1 error. */
static int leer_linea(char *linea, Plan *plan, const char *ruta, int nlinea)
{
    /* Separamos en a lo más 4 campos usando los 3 primeros ':' */
    char *campo[4] = { NULL, NULL, NULL, NULL };
    int n = 0;
    char *resto = linea;
    while (n < 3 && resto) campo[n++] = separar(&resto, ':');
    if (resto) campo[n++] = resto;           /* cuarto campo: dependencias */

    if (n < 3) {
        fprintf(stderr, "%s:%d: se esperaban al menos 3 campos separados por ':'\n",
                ruta, nlinea);
        return -1;
    }

    char *id     = recortar(campo[0]);
    char *nombre = recortar(campo[1]);
    char *tiempo = recortar(campo[2]);

    if (!id_valido(id) || strlen(id) >= MAX_ID) {
        fprintf(stderr, "%s:%d: ID inválido '%s'\n", ruta, nlinea, id);
        return -1;
    }
    if (*nombre == '\0' || strlen(nombre) >= MAX_NOMBRE) {
        fprintf(stderr, "%s:%d: nombre vacío o demasiado largo\n", ruta, nlinea);
        return -1;
    }

    Actividad *a = nueva_actividad(plan);
    if (!a) { perror("realloc"); return -1; }
    snprintf(a->id, sizeof a->id, "%s", id);
    snprintf(a->nombre, sizeof a->nombre, "%s", nombre);
    a->linea = nlinea;

    if (*tiempo == '\0') {
        a->tiempo_ms = TIEMPO_MIN + rand() % (TIEMPO_MAX - TIEMPO_MIN + 1);
        a->tiempo_aleatorio = 1;
    } else {
        char *fin;
        errno = 0;
        long t = strtol(tiempo, &fin, 10);
        if (errno || *fin != '\0' || t < 0 || t > INT_MAX) {
            fprintf(stderr, "%s:%d: tiempo inválido '%s'\n", ruta, nlinea, tiempo);
            return -1;
        }
        a->tiempo_ms = (int)t;
    }

    if (campo[3] && leer_dependencias(campo[3], a, ruta, nlinea) < 0)
        return -1;
    return 0;
}

int plan_cargar(const char *ruta, Plan *plan)
{
    memset(plan, 0, sizeof *plan);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    FILE *f = fopen(ruta, "r");
    if (!f) { perror(ruta); return -1; }

    char  *linea = NULL;
    size_t cap   = 0;
    int    nlinea = 0, error = 0;

    while (getline(&linea, &cap, f) != -1) {
        nlinea++;
        char *l = recortar(linea);            /* también quita '\n' y '\r' */
        if (*l == '\0' || *l == '#') continue;
        if (leer_linea(l, plan, ruta, nlinea) < 0) { error = 1; break; }
    }
    free(linea);
    fclose(f);

    if (!error && plan->n == 0) {
        fprintf(stderr, "%s: el plan no tiene actividades\n", ruta);
        error = 1;
    }
    if (error) { plan_liberar(plan); return -1; }
    return 0;
}
