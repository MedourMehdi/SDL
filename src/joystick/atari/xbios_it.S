/*
 * xbios_it.S - XBIOS joystick vector handler
 *
 * XbiosJoystickVector is installed into _KBDVECS.joyvec (offset 24).
 * The OS calls it with a0 pointing to the 3-byte IKBD joystick packet:
 *   a0@(0) = 0xFD  (packet header)
 *   a0@(1) = joy0  (port 0, mouse port - usually ignored)
 *   a0@(2) = joy1  (port 1, joystick port)
 *
 * Bit layout of joy1 byte (same for joy0):
 *   bit 0 = forward  (up)
 *   bit 1 = back     (down)
 *   bit 2 = left
 *   bit 3 = right
 *   bit 7 = fire
 *   bits 4-6 = always 0
 *
 * We save only d0 and a1. We must NOT restore a0 before chaining
 * because the old handler also expects a0 pointing to the packet.
 */

#if defined(__mcoldfire__)
# define SAVESTACK    lea sp@(-8),sp; moveml d0/a1,sp@
# define RESTORESTACK moveml sp@,d0/a1; lea sp@(8),sp
#else
# define SAVESTACK    moveml d0/a1,sp@-
# define RESTORESTACK moveml sp@+,d0/a1
#endif

    .text

    .globl _XbiosInstall
    .globl _XbiosUninstall
    .globl _XbiosJoystickVector
    .globl _Xbios_joystick
    .globl _old_joy_vec

/* -----------------------------------------------------------------------
 * XbiosInstall(_KBDVECS *kbdvecs, void *newvector)
 *
 * Stack on entry (after SAVESTACK pushes d0/a0-a1, 12 bytes):
 *   sp+12 = return address
 *   sp+16 = kbdvecs
 *   sp+20 = newvector
 * ----------------------------------------------------------------------- */
_XbiosInstall:
#if defined(__mcoldfire__)
    lea     sp@(-12),sp
    moveml  d0/a0-a1,sp@
#else
    moveml  d0/a0-a1,sp@-
#endif

    movel   sp@(16),a0          | a0 = _KBDVECS *
    movel   sp@(20),d0          | d0 = new handler address

    movew   #0x2700,sr          | disable interrupts

    movel   a0@(24),_old_joy_vec | save old joyvec (offset 24 in _KBDVECS)
    movel   d0,a0@(24)           | install ours

    movew   #0x2300,sr          | re-enable interrupts

#if defined(__mcoldfire__)
    moveml  sp@,d0/a0-a1
    lea     sp@(12),sp
#else
    moveml  sp@+,d0/a0-a1
#endif
    rts

/* -----------------------------------------------------------------------
 * XbiosUninstall(_KBDVECS *kbdvecs)
 *
 * Stack on entry (after save, 12 bytes):
 *   sp+16 = kbdvecs
 * ----------------------------------------------------------------------- */
_XbiosUninstall:
#if defined(__mcoldfire__)
    lea     sp@(-12),sp
    moveml  d0/a0-a1,sp@
#else
    moveml  d0/a0-a1,sp@-
#endif

    movel   sp@(16),a0

    movew   #0x2700,sr
    movel   _old_joy_vec,a0@(24) | restore original joyvec
    movew   #0x2300,sr

#if defined(__mcoldfire__)
    moveml  sp@,d0/a0-a1
    lea     sp@(12),sp
#else
    moveml  sp@+,d0/a0-a1
#endif
    rts

/* -----------------------------------------------------------------------
 * XbiosJoystickVector
 *
 * Called by the OS with a0 pointing to the joystick packet.
 * We save d0 and a1 only. a0 is left intact for the old handler.
 *
 * XBRA chain header must be immediately before the handler label.
 * ----------------------------------------------------------------------- */
    .text
    .even
    .ascii  "XBRA"
    .ascii  "LSDL"

_old_joy_vec:
    .dc.l   0                   | filled by XbiosInstall

_XbiosJoystickVector:
    SAVESTACK

    clrw    d0
    moveb   a0@(2),d0           | joystick 1 byte (port 1)
    andw    #0x8f,d0            | keep directions (bits 0-3) and fire (bit 7)
    movew   d0,_Xbios_joystick  | update shared variable

    RESTORESTACK

    | Chain to old handler with a0 still pointing at the packet
    movel   _old_joy_vec,a1
    jmp     (a1)                | jmp, not jsr — old handler ends with rts

/* -----------------------------------------------------------------------
 * Writeable data
 * ----------------------------------------------------------------------- */
    .data
    .even

_Xbios_joystick:
    .dc.w   0