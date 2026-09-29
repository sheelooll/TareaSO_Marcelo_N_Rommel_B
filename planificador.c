#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "planificador.h"

// Variable global para detectar si el usuario presiono Ctrl+C
static volatile sig_atomic_t flag_sigint = 0;

// Manejador de la senal Ctrl+C (SIGINT)
void manejador_ctrl_c(int sig) {
    (void)sig;
    flag_sigint = 1;
}

// Revisa si todavia hay tareas en ejecucion o listas para salir
int hay_tareas_pendientes(Plan *plan) {
    for (int i = 0; i < plan->n; i++) {
        if (plan->acts[i].estado == EST_LISTA || plan->acts[i].estado == EST_EJECUTANDO) {
            return 1;
        }
    }
    return 0;
}

// Cancela todos los procesos hijos si llega la Seremi (Ctrl+C)
void cancelar_hijos_activos(Plan *plan) {
    printf("\n[SEREMI] Se presiono Ctrl+C. Abortando tareas...\n");
    
    // Mandar senal de termino a los hijos vivos
    for (int i = 0; i < plan->n; i++) {
        if (plan->acts[i].estado == EST_EJECUTANDO && plan->acts[i].pid > 0) {
            kill(plan->acts[i].pid, SIGTERM);
        }
    }
    
    // Esperar a que mueran todos los hijos
    int status;
    while (waitpid(-1, &status, 0) > 0 || errno == EINTR) {
        // bucle de espera
    }

    // Marcar tareas no terminadas como ABORTADAS
    for (int i = 0; i < plan->n; i++) {
        if (plan->acts[i].estado == EST_PENDIENTE || 
            plan->acts[i].estado == EST_LISTA || 
            plan->acts[i].estado == EST_EJECUTANDO) {
            plan->acts[i].estado = EST_ABORTADA;
        }
    }
}

int main(int argc, char *argv[]) {
    // 1. Validar parametros recibidos por consola
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <plan.txt> <K>\n", argv[0]);
        return 1;
    }

    char *ruta_plan = argv[1];
    int K = atoi(argv[2]);

    if (K <= 0) {
        fprintf(stderr, "Error: El limite K debe ser mayor a 0\n");
        return 1;
    }

    // Registrar el manejador para Ctrl+C
    signal(SIGINT, manejador_ctrl_c);

    // 2. Cargar el plan y construir el grafo (DAG)
    Plan plan;
    if (plan_cargar(ruta_plan, &plan) < 0) {
        return 1;
    }

    if (plan_construir_dag(&plan) < 0) {
        plan_liberar(&plan);
        return 1;
    }

    printf("=== Planificador Dieciochero ===\n");
    printf("Plan: %s | Tareas: %d | Concurrencia K: %d\n\n", ruta_plan, plan.n, K);

    int activos = 0;

    // 3. Bucle principal del planificador
    while (hay_tareas_pendientes(&plan) == 1 && flag_sigint == 0) {

        // A) Lanzar tareas LISTAS mientras no superemos el limite K
        for (int i = 0; i < plan.n && activos < K && flag_sigint == 0; i++) {
            if (plan.acts[i].estado == EST_LISTA) {
                Actividad *a = &plan.acts[i];

                int p_in[2] = {-1, -1};
                int p_out[2] = {-1, -1};

                // Crear pipe de entrada si la tarea depende de otras
                if (a->n_deps > 0) {
                    if (pipe(p_in) < 0) {
                        perror("pipe entrada");
                        break;
                    }
                    for (int d = 0; d < a->n_deps; d++) {
                        int idx_dep = a->deps[d];
                        msg_enviar(p_in[1], plan.acts[idx_dep].mensaje);
                    }
                    close(p_in[1]); // El padre cierra escritura
                }

                // Crear pipe de salida para recibir la respuesta del hijo
                if (pipe(p_out) < 0) {
                    if (p_in[0] >= 0) close(p_in[0]);
                    perror("pipe salida");
                    break;
                }

                // Crear el proceso hijo (fflush evita que el hijo herede y
                // repita texto que quedó en el buffer del padre)
                fflush(stdout);
                pid_t pid = fork();

                if (pid < 0) {
                    perror("fork error");
                    if (p_in[0] >= 0) close(p_in[0]);
                    close(p_out[0]);
                    close(p_out[1]);
                    break;
                }

                // CODIGO DEL HIJO
                if (pid == 0) {
                    if (p_in[1] >= 0) close(p_in[1]);
                    close(p_out[0]);

                    int codigo = actividad_ejecutar(a, p_in[0], p_out[1], 0);
                    _exit(codigo);
                }

                // CODIGO DEL PADRE
                if (p_in[0] >= 0) close(p_in[0]);
                close(p_out[1]);

                a->pid = pid;
                a->fd_salida = p_out[0];
                a->estado = EST_EJECUTANDO;
                activos++;
            }
        }

        // B) Esperar a que termine algun hijo (SIN busy-waiting)
        if (activos > 0) {
            int status = 0;
            pid_t pid_terminado = waitpid(-1, &status, 0);

            if (pid_terminado < 0) {
                if (errno == EINTR && flag_sigint == 1) {
                    break;
                }
                continue;
            }

            // Buscar cual actividad corresponde al PID que termino
            int idx = -1;
            for (int i = 0; i < plan.n; i++) {
                if (plan.acts[i].estado == EST_EJECUTANDO && plan.acts[i].pid == pid_terminado) {
                    idx = i;
                    break;
                }
            }

            if (idx != -1) {
                Actividad *a = &plan.acts[idx];
                activos--;

                int codigo_exit = -1;
                if (WIFEXITED(status)) {
                    codigo_exit = WEXITSTATUS(status);
                }

                if (codigo_exit == ACT_OK) {
                    // Si la tarea termino con exito
                    if (a->fd_salida >= 0) {
                        msg_recibir(a->fd_salida, a->mensaje);
                        close(a->fd_salida);
                        a->fd_salida = -1;
                    }
                    a->estado = EST_TERMINADA;

                    // Descontar dependencias pendientes a los sucesores
                    for (int s = 0; s < a->n_suc; s++) {
                        int idx_suc = a->sucesores[s];
                        Actividad *suc = &plan.acts[idx_suc];
                        suc->deps_pendientes--;
                        if (suc->deps_pendientes == 0 && suc->estado == EST_PENDIENTE) {
                            suc->estado = EST_LISTA;
                        }
                    }
                } else if (flag_sigint == 1) {
                    // Murio por el Ctrl+C: no es una falla, queda abortada
                    if (a->fd_salida >= 0) {
                        close(a->fd_salida);
                        a->fd_salida = -1;
                    }
                    a->estado = EST_ABORTADA;
                } else {
                    // Si la tarea fallo internamente
                    if (a->fd_salida >= 0) {
                        close(a->fd_salida);
                        a->fd_salida = -1;
                    }
                    a->estado = EST_FALLIDA;
                    plan_abortar_rama(&plan, idx);
                }
            }
        }
    }

    // 4. Si se presiono Ctrl+C, abortar los procesos que estaban corriendo
    if (flag_sigint == 1) {
        cancelar_hijos_activos(&plan);
    }

    // 5. Imprimir resultado final y liberar memoria
    printf("\n=== RESUMEN DE EJECUCION ===\n");
    plan_imprimir(&plan);
    plan_liberar(&plan);

    return flag_sigint ? 1 : 0;
}
