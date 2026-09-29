# Makefile — Planificador Dieciochero
# Flags exigidos por la rúbrica. -lpthread solo se enlaza porque lo pide la
# rúbrica: el programa NO usa hilos.
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c17
LDLIBS  = -lpthread

PARTE1  = parser.c dag.c mensajes.c actividad.c

.PHONY: all test clean generar

# Programa completo (necesita planificador.c, que es la Parte 2)
all: planificador

planificador: planificador.c $(PARTE1) planificador.h
	$(CC) $(CFLAGS) -o $@ planificador.c $(PARTE1) $(LDLIBS)

# Pruebas de la Parte 1 (funcionan sin planificador.c)
test_parte1: tests/test_parte1.c $(PARTE1) planificador.h
	$(CC) $(CFLAGS) -o $@ tests/test_parte1.c $(PARTE1) $(LDLIBS)

test: test_parte1
	./test_parte1 ejemplos/plan.txt

# Generador de planes grandes para la prueba de estrés
generar: generar_plan

generar_plan: ejemplos/generar_plan.c
	$(CC) $(CFLAGS) -o $@ ejemplos/generar_plan.c

clean:
	rm -f planificador test_parte1 generar_plan *.o
