#!/usr/bin/env python3
"""Genera un plan grande y válido (DAG) para pruebas de estrés.
Uso: python3 ejemplos/generar_plan.py 10000 > ejemplos/plan_10000.txt
Cada actividad depende solo de actividades anteriores, así nunca hay ciclos."""
import random, sys

n = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
t_max = int(sys.argv[2]) if len(sys.argv) > 2 else 50   # ms, cortos para probar rápido
random.seed(18)
for i in range(1, n + 1):
    deps = []
    if i > 1:
        k = random.randint(0, min(3, i - 1))
        deps = sorted(random.sample(range(max(1, i - 200), i), min(k, i - max(1, i - 200))))
    tiempo = random.randint(1, t_max)
    print(f"{i} : actividad_{i} : {tiempo} : {', '.join(map(str, deps))}")
