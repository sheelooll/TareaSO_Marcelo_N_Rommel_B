/*
 * ejemplos/generar_plan.c — Genera un plan grande y válido (DAG) para la
 * prueba de estrés.
 *
 * Uso:   make generar
 *        ./generar_plan 10000 > ejemplos/plan_10000.txt
 *        ./generar_plan 10000 50 > plan.txt    (segundo argumento: tiempo máx en ms)
 *
 * Cada actividad depende solo de actividades anteriores (de las 200 previas),
 * por lo que el plan nunca tiene ciclos.
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int n     = (argc > 1) ? atoi(argv[1]) : 10000;
    int t_max = (argc > 2) ? atoi(argv[2]) : 50;
    if (n <= 0 || t_max <= 0) {
        fprintf(stderr, "Uso: %s [n_actividades] [tiempo_max_ms]\n", argv[0]);
        return 1;
    }
    srand(18);   /* semilla fija: siempre genera el mismo plan */

    for (int i = 1; i <= n; i++) {
        printf("%d : actividad_%d : %d :", i, i, 1 + rand() % t_max);

        int desde = (i > 200) ? i - 200 : 1;   /* candidatas: [desde, i-1] */
        int disponibles = i - desde;
        int k = disponibles > 0 ? rand() % ((disponibles < 3 ? disponibles : 3) + 1) : 0;

        int elegidas[3];
        int m = 0;
        while (m < k) {
            int d = desde + rand() % disponibles;
            int repetida = 0;
            for (int j = 0; j < m; j++)
                if (elegidas[j] == d) repetida = 1;
            if (!repetida) elegidas[m++] = d;
        }
        for (int j = 0; j < m; j++)
            printf("%s %d", j ? "," : "", elegidas[j]);
        printf("\n");
    }
    return 0;
}
