#ifndef AS4_UART_PROTOCOL_H
#define AS4_UART_PROTOCOL_H

/* Pure C/C++ helpers, identical on MCXC444 and ESP32.
 * Wire protocol: printable ASCII, comma-separated, newline-terminated.
 * This is framing/validation, NOT encryption or a checksum. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UART_LINK_LINE_SIZE 96u  /* includes room for the trailing '\0' */

/* Require exactly count decimal uint32 fields after prefix.
 * Reject empty, signed, whitespace-containing, overflowed or extra fields.
 * Input must be a complete NUL-terminated line with CR/LF already removed. */
static inline bool UartProtocol_Parse(const char *line, const char *prefix,
                                      uint32_t *values, size_t count)
{
    size_t prefixLength = strlen(prefix);
    if (strncmp(line, prefix, prefixLength) != 0) return false;
    const char *p = line + prefixLength;    //ignore the prefix
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = 0u;
        if (*p < '0' || *p > '9') return false; //no space between values, only comma
        do {
            uint32_t digit = (uint32_t)(*p - '0');
            if (v > (UINT32_MAX - digit) / 10u) return false; //value bound check
            v = v * 10u + digit;
            ++p;
        } while (*p >= '0' && *p <= '9'); //p should ponit to some non number after the loop
        values[i] = v;
        if (i + 1u < count) {   //not the last value to store
            if (*p != ',') return false;
            ++p;
        } else if (*p != '\0') {    //end of line
            return false;
        }
    }
    return true;
}

typedef struct {
    char line[UART_LINK_LINE_SIZE];
    size_t used;
    bool discard;
} UartProtocol_Receiver;

/* Feed one byte at a time. On true, r->line holds a complete valid-length
 * line (not yet semantically parsed). Copy/use it before feeding another byte.
 * Overlong or control-character-containing lines are discarded through LF.
 * An optional CR immediately before LF is accepted for Serial Monitor use. */
static inline bool UartProtocol_Feed(UartProtocol_Receiver *r, char c)
{
    if (c == '\n') {
        bool complete = !r->discard && r->used != 0u;
        if (r->used != 0u && r->line[r->used - 1u] == '\r') --r->used;
        r->line[r->used] = '\0';
        r->used = 0u;
        r->discard = false;
        return complete && r->line[0] != '\0';
    }
    if (r->discard) return false;
    if (r->used != 0u && r->line[r->used - 1u] == '\r') {
        r->discard = true;  /* CR is allowed only immediately before LF */
        return false;
    }
    if (((unsigned char)c < 32u && c != '\r') || (unsigned char)c > 126u ||
        r->used >= sizeof(r->line) - 1u) {
        r->discard = true;
        return false;
    }
    r->line[r->used++] = c;
    return false;
}

#endif
