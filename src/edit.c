/* edit.c - reimplementation of the fx-580VN X edit-box support routines.
 *
 * Complements the print/keyboard clusters; every function here is checked
 * against the original ROM by tests/t_edit.c.
 */
#include "edit.h"

/* 0x0AC60 - prev_unit_off.  Machine translation of the ROM routine:
 *
 *   push er12
 *   mov er12, er2                  ; probe
 *   cmp er0, er2  / bc ge          ; lo >= probe (UNSIGNED `ge`) → answer 0
 *   add er12, -1                   ; probe - 1
 *   cmp er0, er12 / bc eq          ; probe == lo + 1 → answer probe - 1
 *   add er12, -1                   ; probe - 2
 *   l r0, [er12]                   ; BYTE load - the destination register
 *                                  ; size picks the access width
 *   cmp r0, 240 / bc ge            ; `ge` again: byte >= 240 unsigned, i.e.
 *                                  ; the marker/tag range 0xF0-0xFF
 *   add er12, 1                    ; plain char: settle on probe - 1
 *
 * So a marker byte (0xF0-0xFF) before the cursor hides a two-byte unit whose
 * start is one further back; everything else is a single-byte unit.  In this
 * annotation scheme plain `ge`/`eq` tests are the unsigned forms; the signed
 * variants are written `ges`/`lts` and do not appear here.
 */
uint16_t prev_unit_off(uint8_t *ds, uint16_t lo, uint16_t probe)
{
    uint16_t r12 = probe;

    if (lo >= probe) {
        r12 = 0;
    } else {
        r12 = (uint16_t)(probe - 1);
        if (lo != r12) {
            r12 = (uint16_t)(probe - 2);
            if (ds[r12] < 0xF0)                 /* not a marker byte */
                r12 = (uint16_t)(r12 + 1);
        }
    }
    return r12;
}
