# Planificador Dieciochero — Tarea 1 Sistemas Operativos

Integrantes: Marcelo N. y Rommel B.

Simulador que lee un plan de actividades de Fiestas Patrias (un DAG), crea un
proceso por actividad respetando un límite de concurrencia K, propaga mensajes
entre actividades usando pipes, aísla las fallas y responde a Ctrl+C.

## Compilación y uso

```bash
make                         # compila ./planificador
./planificador plan.txt K    # K = máximo de procesos simultáneos
```

Compilación equivalente a la de la rúbrica:

```bash
gcc -Wall -Wextra -std=c17 -o planificador planificador.c parser.c dag.c mensajes.c actividad.c -lpthread
```

`-lpthread` se enlaza solo porque lo pide la rúbrica: el programa no usa hilos.

Pruebas de la Parte 1 (no necesitan el planificador):

```bash
make test                                  # prueba con ejemplos/plan.txt
./test_parte1 ejemplos/plan_falla.txt      # prueba con otro plan
python3 ejemplos/generar_plan.py 10000 > ejemplos/plan_10000.txt   # plan de estrés
```

## Estructura

| Archivo | Autor | Contenido |
|---|---|---|
| `planificador.h` | ambos | Estructuras y funciones compartidas (el contrato entre las dos partes) |
| `parser.c` | Marcelo | Lectura y validación de `plan.txt` |
| `dag.c` | Marcelo | Construcción del DAG, detección de ciclos, orden topológico, aborto de ramas |
| `mensajes.c` | Marcelo | Envío y recepción de mensajes de tamaño fijo por pipes |
| `actividad.c` | Marcelo | Código que ejecuta cada proceso hijo |
| `planificador.c` | Rommel | `main`, creación de procesos, límite K, espera, señales |
| `tests/test_parte1.c` | Marcelo | Pruebas de la Parte 1 |
| `ejemplos/` | ambos | Planes de prueba y generador de planes grandes |

## Formato de `plan.txt`

```
ID : Nombre : tiempo_ms : Dep1, Dep2, ...
```

- Se ignoran las líneas vacías y las que empiezan con `#`.
- Si el tiempo viene vacío, se sortea entre 100 y 5000 ms.
- Las dependencias pueden ir con o sin corchetes: `[1, 2]` o `1, 2`.
- El último `:` es opcional cuando no hay dependencias.

## Parte 1 — Parseo, DAG, mensajes y actividades (Marcelo)

### Funciones implementadas

| Función | Qué hace |
|---|---|
| `plan_cargar(ruta, plan)` | Lee el archivo línea a línea, valida cada campo y guarda las actividades. |
| `plan_construir_dag(plan)` | Traduce los IDs de dependencias a índices, arma las listas de dependencias y sucesores, y rechaza el plan si tiene un ciclo. |
| `plan_buscar(plan, id)` | Busca una actividad por su ID en O(1) usando la tabla hash. |
| `plan_orden_topologico(plan, orden)` | Algoritmo de Kahn; devuelve cuántas actividades quedaron ordenadas. |
| `plan_abortar_rama(plan, idx)` | Marca como `ABORTADA` toda actividad que dependa, directa o indirectamente, de la actividad fallida. |
| `msg_enviar(fd, texto)` / `msg_recibir(fd, buf)` | Envían y reciben un mensaje de exactamente `MAX_MSG` bytes por un pipe. |
| `actividad_ejecutar(a, fd_entrada, fd_salida, verbose)` | Se ejecuta dentro del hijo: recibe los insumos, simula el trabajo y envía su mensaje. |

### Decisiones de diseño

**Validación estricta del archivo.** Ante una línea mal formada, un ID repetido,
una dependencia inexistente, una actividad que depende de sí misma o un ciclo,
el programa informa el error con su número de línea y no ejecuta nada. Ejecutar
un plan a medias daría resultados engañosos, y un ciclo haría que el
planificador esperara para siempre.

**Aristas como índices y tabla hash.** Las actividades se guardan en un arreglo
y las aristas usan índices en vez de punteros, así el arreglo puede crecer con
`realloc` sin invalidar nada. Los IDs se resuelven con una tabla hash (FNV-1a),
por lo que armar el DAG cuesta O(actividades + dependencias). Con 10000
actividades, parsear y validar el plan toma menos de 0,1 segundos.

**Lista de sucesores en cada nodo.** Cuando una actividad termina, el
planificador sabe de inmediato a quién avisarle, sin recorrer el plan completo.

**Recorridos iterativos.** El orden topológico y el aborto de ramas usan una
cola y una pila explícitas, no recursión. Una cadena de 10000 dependencias
podría desbordar la pila con recursión.

**Mensajes de tamaño fijo.** Cada mensaje ocupa exactamente `MAX_MSG` = 128
bytes, que es el "mensaje de texto acotado" del enunciado. Como 128 es menor
que `PIPE_BUF` (4096), POSIX garantiza que cada `write` de un mensaje es
atómico: aunque varios procesos escriban en el mismo pipe, los mensajes nunca
se mezclan. Las lecturas y escrituras se reintentan si una señal las
interrumpe (`EINTR`).

**El hijo verifica sus insumos.** Una actividad espera un mensaje por cada
dependencia. Si el pipe se cierra antes de recibirlos todos, termina con
`ACT_ERROR_INSUMO` en vez de trabajar con datos incompletos.

**Espera sin busy-waiting.** El trabajo se simula con `nanosleep`, que duerme
el proceso sin consumir CPU. Si una señal lo interrumpe, sigue durmiendo solo
el tiempo que faltaba.

**Fallas simuladas.** Para probar el aislamiento de errores, una actividad
falla si su nombre contiene `falla` (ver `ejemplos/plan_falla.txt`). También se
puede activar una probabilidad de falla aleatoria:

```bash
PROB_FALLA=0.05 ./planificador ejemplos/plan_10000.txt 8
```

Códigos de salida del hijo: `0` OK, `1` falla interna, `2` insumos
incompletos, `3` error al escribir en el pipe.

## Parte 2 — Planificador, concurrencia y señales (Rommel)

### Funciones implementadas

- `main`: Es la función principal que valida los argumentos (`plan.txt` y `K`), carga el plan, arma el DAG y corre el bucle principal que controla los procesos.
- `atajar_ctrl_c`: Manejador de la señal `SIGINT` (`Ctrl+c`) que cambia la variable `hubo_ctrl_c` a 1.

### Decisiones de diseño

- **Límite de concurrencia K sin busy-waiting:** Llevo la cuenta de los procesos corriendo con la variable `activos`. Antes de hacer `fork()`, el ciclo verifica que `activos < K`. Para no consumir CPU innecesariamente, uso `waitpid(-1, &status, 0)`, que deja al proceso padre bloqueado en el sistema operativo hasta que algún hijo termine.

- **Paso de mensajes por pipes:** Cuando una tarea tiene dependencias, el padre escribe los mensajes de los antecesores en un `pipe` de entrada (`p_in`) antes del `fork()`. El hijo lee de ahí sus insumos y escribe su mensaje de salida en otro `pipe` (`p_out`) que lee el padre al terminar.

- **Aislamiento de fallas:** Si un hijo termina con un código distinto de `ACT_OK` (0), se marca como `EST_FALLIDA` y se llama a `plan_abortar_rama()`. Esto cancela en cascada solo las tareas que dependían de la que falló, permitiendo que las ramas independientes sigan su ejecución.

- **Manejo de Ctrl+c (Seremi):** Se registra la señal con `signal(SIGINT, atajar_ctrl_c)`. Si el usuario presiona `Ctrl+c`, se activa la variable `hubo_ctrl_c`, se le envía un `SIGTERM` con `kill()` a los procesos hijos activos para no dejar procesos colgados, se esperan con `waitpid` y se marcan las demás tareas como `EST_ABORTADA`.
