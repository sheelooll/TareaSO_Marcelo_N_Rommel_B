/*
 * planificador.h — Contrato común del Planificador Dieciochero.
 *
 * Este archivo lo comparten las dos partes del trabajo:
 *   Parte 1 (Marcelo): parser.c, dag.c, mensajes.c, actividad.c
 *   Parte 2 (Rommel):  planificador.c (main, fork, límite K, señales)
 *
 * Si necesitai cambiar algo, avisame
 */
#ifndef PLANIFICADOR_H
#define PLANIFICADOR_H

#include <sys/types.h>

/* ---------- Límites ---------- */
#define MAX_ID      64    /* largo máximo del ID de una actividad            */
#define MAX_NOMBRE  128   /* largo máximo del nombre                          */
#define MAX_MSG     128   /* tamaño fijo de cada mensaje por pipe (acotado).  */
                          /* Es menor que PIPE_BUF (4096), así que cada write */
                          /* de un mensaje es atómico.                        */
#define TIEMPO_MIN  100   /* rango para tiempos aleatorios (ms)               */
#define TIEMPO_MAX  5000

/* ---------- Códigos de salida de un proceso hijo ---------- */
#define ACT_OK             0  /* terminó bien y envió su mensaje             */
#define ACT_FALLA          1  /* falla interna simulada                       */
#define ACT_ERROR_INSUMO   2  /* no recibió todos los insumos esperados       */
#define ACT_ERROR_PIPE     3  /* no pudo escribir su mensaje de salida        */

/* ---------- Estados de una actividad ---------- */
typedef enum {
    EST_PENDIENTE,   /* le faltan dependencias por terminar                   */
    EST_LISTA,       /* dependencias cumplidas, esperando cupo (límite K)     */
    EST_EJECUTANDO,  /* tiene un proceso hijo vivo                            */
    EST_TERMINADA,   /* terminó bien                                          */
    EST_FALLIDA,     /* su proceso falló                                      */
    EST_ABORTADA     /* no se ejecutará: depende de una fallida, o SIGINT     */
} Estado;

/* ---------- Un nodo del DAG ---------- */
typedef struct {
    char   id[MAX_ID];
    char   nombre[MAX_NOMBRE];
    int    tiempo_ms;
    int    tiempo_aleatorio;   /* 1 si el archivo no traía tiempo             */
    int    linea;              /* línea de plan.txt (para mensajes de error)  */

    /* Dependencias como texto, tal como vienen en el archivo.
       plan_construir_dag() las traduce a índices y las libera. */
    char **dep_ids;
    int    n_dep_ids;

    /* Aristas del DAG, como índices dentro de plan->acts */
    int   *deps;       int n_deps;              /* de quién depende          */
    int   *sucesores;  int n_suc; int cap_suc;  /* quién depende de ella     */

    /* Campos de ejecución: los maneja el planificador (parte 2) */
    int    deps_pendientes;    /* dependencias que aún no terminan            */
    Estado estado;
    pid_t  pid;                /* pid del hijo mientras está EJECUTANDO       */
    int    fd_salida;          /* lectura del pipe hijo -> padre, o -1        */
    char   mensaje[MAX_MSG];   /* mensaje que produjo al terminar             */
} Actividad;

/* ---------- El plan completo ---------- */
typedef struct {
    Actividad *acts;
    int        n;
    int        cap;
    int       *tabla;          /* tabla hash: ID -> índice (búsqueda O(1))    */
    int        tam_tabla;
} Plan;

/* ================= PARTE 1 (Marcelo) ================= */

/* parser.c
 * Lee el archivo y llena plan->acts. Asigna tiempos aleatorios en
 * [TIEMPO_MIN, TIEMPO_MAX] cuando falta el tiempo.
 * Retorna 0 si todo bien, -1 si el archivo es inválido (ya imprimió el error). */
int  plan_cargar(const char *ruta, Plan *plan);

/* dag.c */
int  plan_construir_dag(Plan *plan);            /* 0 ok, -1 error o ciclo    */
int  plan_buscar(const Plan *plan, const char *id); /* índice o -1           */
int  plan_orden_topologico(const Plan *plan, int *orden); /* devuelve cuántos */
int  plan_abortar_rama(Plan *plan, int idx_fallida); /* marca descendientes   */
                                                     /* como ABORTADA, retorna*/
                                                     /* cuántas abortó        */
void plan_imprimir(const Plan *plan);
void plan_liberar(Plan *plan);

/* mensajes.c — mensajes de tamaño fijo MAX_MSG por pipe */
int  msg_enviar(int fd, const char *texto);     /* 0 ok, -1 error            */
int  msg_recibir(int fd, char out[MAX_MSG]);    /* 1 ok, 0 EOF, -1 error     */
void msg_formatear_insumo(const Actividad *a, char out[MAX_MSG]);

/* actividad.c — código que corre DENTRO del proceso hijo.
 * Lee a->n_deps insumos desde fd_entrada, simula el trabajo (a->tiempo_ms),
 * y escribe su propio mensaje en fd_salida. Retorna un código ACT_*.
 * El planificador debe llamarla así, justo después del fork():
 *
 *     _exit(actividad_ejecutar(a, fd_entrada, fd_salida, verbose));
 *
 * (usar _exit y no exit, para no vaciar dos veces los buffers del padre). */
int  actividad_ejecutar(const Actividad *a, int fd_entrada, int fd_salida,
                        int verbose);

/* ================= PARTE 2 (Rommel) ================= */
/* planificador.c: main(), creación de procesos con límite K, espera sin
 * busy-waiting, reenvío de mensajes entre actividades, manejo de fallas
 * (usando plan_abortar_rama) y de SIGINT. */

#endif
