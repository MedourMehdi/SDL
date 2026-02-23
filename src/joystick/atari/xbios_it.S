| ============================================================================
| xbios_it.S - XBIOS joystick vector handler
| ============================================================================
| Simple DirectMedia Layer
| Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
|
| This software is provided 'as-is', without any express or implied warranty.
| In no event will the authors be held liable for any damages arising from the
| use of this software.
|
| Permission is granted to anyone to use this software for any purpose, including
| commercial applications, and to alter it and redistribute it freely, subject
| to the following restrictions:
|
| 1. The origin of this software must not be misrepresented; you must not claim
|    that you wrote the original software. If you use this software in a product,
|    an acknowledgment in the product documentation would be appreciated but is
|    not required.
| 2. Altered source versions must be plainly marked as such, and must not be
|    misrepresented as being the original software.
| 3. This notice may not be removed or altered from any source distribution.

| ============================================================================
| Adapted from original sdl 1.2 code written by Patrice Mandin
| ============================================================================
| xbios_it.S – XBIOS joystick interrupt vector handler
| Medour Mehdi - 2026
| Architecture: Motorola 68000 / Atari ST-TT-Falcon
|
| XbiosInstall   — saves the current joyvec and installs XbiosJoystickVector.
|                  Must be called via Supexec().
| XbiosUninstall — restores the original joyvec.
|                  Must be called via Supexec().
| XbiosJoystickVector — called by the IKBD at interrupt level on every
|                  joystick packet. Masks the byte with 0x8F and stores
|                  it in Xbios_joystick for the main thread to read.
|
| ABI: interrupt handler — no C ABI; all registers preserved.
| ============================================================================

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

| ============================================================================
| XbiosInstall – Install XBIOS keyboard/joystick vector
| ============================================================================
| Stack on entry (after SAVESTACK pushes d0/a0-a1, 12 bytes):
|   SP+12 = return address
|   SP+16 = kbdvecs  (_KBDVECS pointer)
|   SP+20 = newvector (handler function pointer)
|
| Sauvegarde: d0/a0-a1 → pile (12 bytes)
| Paramètres: offsets absolus depuis SP après save
| ============================================================================

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