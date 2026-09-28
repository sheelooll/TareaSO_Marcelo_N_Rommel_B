/*
 * mensajes.c — Paso de mensajes por pipes  (Parte 1: Marcelo)
 *
 * Cada mensaje ocupa exactamente MAX_MSG bytes (texto + relleno con '\0').
 * Decisiones:
 *   - Tamaño fijo = "mensaje de texto acotado" del enunciado. El lector sabe
 *     siempre cuántos bytes leer, sin separadores ni largo variable.
 *   - MAX_MSG (128) < PIPE_BUF (4096): POSIX garantiza que cada write() de un
 *     mensaje es atómico, así que aunque varios procesos escriban en el mismo
 *     pipe los mensajes nunca se mezclan (sin condiciones de carrera).
 *   - Las lecturas y escrituras se reintentan si una señal las interrumpe
 *     (EINTR) o si el kernel entrega el mensaje en pedazos.
 */
#define _POSIX_C_SOURCE 200809L

#include "planificador.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int msg_enviar(int fd, const char *texto)
{
    char buf[MAX_MSG];
    memset(buf, 0, sizeof buf);
    snprintf(buf, sizeof buf, "%s", texto);   /* trunca si es muy largo */

    size_t enviados = 0;
    while (enviados < sizeof buf) {
        ssize_t r = write(fd, buf + enviados, sizeof buf - enviados);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        enviados += (size_t)r;
    }
    return 0;
}

int msg_recibir(int fd, char out[MAX_MSG])
{
    size_t leidos = 0;
    while (leidos < MAX_MSG) {
        ssize_t r = read(fd, out + leidos, MAX_MSG - leidos);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0)                          /* el otro extremo se cerró */
            return (leidos == 0) ? 0 : -1;   /* mensaje cortado = error */
        leidos += (size_t)r;
    }
    out[MAX_MSG - 1] = '\0';
    return 1;
}

/* Mensaje que una actividad terminada entrega a sus dependientes. */
void msg_formatear_insumo(const Actividad *a, char out[MAX_MSG])
{
    snprintf(out, MAX_MSG, "insumo de %.40s (%.60s) listo en %d ms",
             a->id, a->nombre, a->tiempo_ms);
}
