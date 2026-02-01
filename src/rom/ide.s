; ============================================================
; IDE Driver for Amiga 600/A1200 (Gayle IDE Controller)
; ============================================================
; Implements sector read functionality with correct register spacing
;
; Hardware: Gayle IDE controller at $DA0000
; - 0x100 byte (256 byte) register spacing
; - 16-bit data transfers
; - LBA addressing mode
;
; Public routines:
;   IDETestRead - Test routine that reads sector 0 and verifies boot signature

; ============================================================
; Hardware Definitions
; ============================================================

; Gayle IDE Controller - A600/A1200
; CRITICAL: Register spacing is 0x100 bytes (256 bytes), NOT 4 bytes!
; Using ABSOLUTE addresses (as per FS-UAE source code)
IDE_DATA        equ $DA0000         ; Data port (16-bit R/W)
IDE_ERROR       equ $DA0100         ; Error register (R)
IDE_FEATURE     equ $DA0100         ; Features register (W) - same address
IDE_NSECTOR     equ $DA0200         ; Sector count (R/W)
IDE_SECTOR      equ $DA0300         ; Sector number / LBA 0-7 (R/W)
IDE_LCYL        equ $DA0400         ; Cylinder low / LBA 8-15 (R/W)
IDE_HCYL        equ $DA0500         ; Cylinder high / LBA 16-23 (R/W)
IDE_SELECT      equ $DA0600         ; Drive/head / LBA 24-27 (R/W)
IDE_STATUS      equ $DA0700         ; Status register (R)
IDE_COMMAND     equ $DA0700         ; Command register (W) - same address

; GAYLE Control Registers
GAYLE_CS        equ $DA8000         ; Status/control
GAYLE_IRQ       equ $DA9000         ; Interrupt status/clear
GAYLE_INT       equ $DAA000         ; Interrupt enable
GAYLE_CFG       equ $DAB000         ; Configuration

; Status Register Bits
STATUS_BSY      equ 7               ; Busy
STATUS_DRDY     equ 6               ; Drive Ready
STATUS_DRQ      equ 3               ; Data Request
STATUS_ERR      equ 0               ; Error

; Commands
CMD_READ_SECTORS equ $20
CMD_IDENTIFY     equ $EC

; Constants
IDE_TIMEOUT     equ $100000         ; Timeout counter
SECTOR_BUFFER   equ $300000         ; Read buffer location (in fast RAM)

; ============================================================
; IDEWaitReady - Wait for drive ready
; ============================================================
; Input:
;   None (uses absolute addresses)
; Output:
;   D0.b = status register value
;   CCR = set (BEQ if timeout occurred)
; Clobbers: D1, D2
; ============================================================
IDEWaitReady:
    movem.l d2,-(sp)
    move.l  #IDE_TIMEOUT,d2         ; Timeout counter
.loop:
    move.b  IDE_STATUS,d0           ; Read status
    btst    #STATUS_BSY,d0          ; Check BSY bit
    beq.s   .ready                  ; If not busy, we're ready
    subq.l  #1,d2                   ; Decrement timeout
    bne.s   .loop                   ; Continue if not timed out
    moveq   #0,d0                   ; Signal timeout (Z=1)
    movem.l (sp)+,d2
    rts
.ready:
    moveq   #-1,d0                  ; Signal success (Z=0)
    movem.l (sp)+,d2
    rts

; ============================================================
; IDEWaitDRQ - Wait for data request
; ============================================================
; Input:
;   None (uses absolute addresses)
; Output:
;   D0.b = status register value
;   CCR = set (BEQ if timeout occurred)
; Clobbers: D1, D2
; ============================================================
IDEWaitDRQ:
    movem.l d2,-(sp)
    move.l  #IDE_TIMEOUT,d2         ; Timeout counter
.loop:
    move.b  IDE_STATUS,d0           ; Read status
    btst    #STATUS_BSY,d0          ; Check BSY bit
    bne.s   .continue               ; If busy, keep waiting
    btst    #STATUS_ERR,d0          ; Check ERR bit
    bne.s   .error                  ; Error occurred
    btst    #STATUS_DRQ,d0          ; Check DRQ bit
    bne.s   .ready                  ; Data is ready
.continue:
    subq.l  #1,d2                   ; Decrement timeout
    bne.s   .loop                   ; Continue if not timed out
    moveq   #0,d0                   ; Signal timeout (Z=1)
    movem.l (sp)+,d2
    rts
.error:
    moveq   #0,d0                   ; Signal error (Z=1)
    movem.l (sp)+,d2
    rts
.ready:
    moveq   #-1,d0                  ; Signal success (Z=0)
    movem.l (sp)+,d2
    rts

; ============================================================
; IDEReadSector - Read a single sector
; ============================================================
; Input:
;   A1 = Buffer address (512 bytes)
;   D0.l = LBA sector number
; Output:
;   D0.l = 0 on success, -1 on error
; Clobbers: D1, D2
; ============================================================
IDEReadSector:
    movem.l d3-d4/a2-a3,-(sp)
    move.l  a1,a2                   ; Save buffer address
    move.l  d0,d3                   ; Save LBA

    ; Debug: entered read sector
    movem.l d0-d1/a0-a1,-(sp)
    pea     .dbg_enter(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Wait for drive ready
    bsr     IDEWaitReady
    beq     .timeout_waitready      ; Branch if timeout

    ; Debug: passed wait ready
    movem.l d0-d1/a0-a1,-(sp)
    pea     .dbg_ready(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Initialize GAYLE (set timing and voltage)
    movem.l d0-d1/a0-a1,-(sp)
    pea     .dbg_gayle_init(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1

    move.b  #$01,GAYLE_CFG          ; Set 5V mode

    ; Debug: check if this helps
    movem.l d0-d1/a0-a1,-(sp)
    moveq   #0,d1
    move.b  IDE_STATUS,d1
    move.w  d1,-(sp)
    pea     .dbg_after_gayle(pc)
    bsr     SerialPrintf
    addq.l  #6,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Try IDENTIFY first to see if drive responds
    movem.l d0-d1/a0-a1,-(sp)
    pea     .dbg_identify(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1

    move.b  #$A0,IDE_SELECT         ; Select master drive
    move.b  #CMD_IDENTIFY,IDE_COMMAND

    ; Check status after IDENTIFY
    movem.l d0-d1/a0-a1,-(sp)
    moveq   #0,d1
    move.b  IDE_STATUS,d1
    move.w  d1,-(sp)
    pea     .dbg_identify_status(pc)
    bsr     SerialPrintf
    addq.l  #6,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Set up LBA read
    move.b  #1,IDE_NSECTOR          ; Read 1 sector

    ; Program LBA address
    move.l  d3,d0
    move.b  d0,IDE_SECTOR           ; LBA bits 0-7
    lsr.l   #8,d0
    move.b  d0,IDE_LCYL             ; LBA bits 8-15
    lsr.l   #8,d0
    move.b  d0,IDE_HCYL             ; LBA bits 16-23
    lsr.l   #8,d0
    andi.b  #$0F,d0                 ; LBA bits 24-27
    ori.b   #$E0,d0                 ; Set LBA mode + master drive
    move.b  d0,IDE_SELECT

    ; Send READ SECTORS command
    move.b  #CMD_READ_SECTORS,IDE_COMMAND

    ; Debug: sent command, check status
    movem.l d0-d1/a0-a1,-(sp)
    moveq   #0,d1
    move.b  IDE_STATUS,d1
    move.w  d1,-(sp)
    pea     .dbg_command(pc)
    bsr     SerialPrintf
    addq.l  #6,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Wait for DRQ
    bsr     IDEWaitDRQ
    beq     .timeout_waitdrq        ; Branch if timeout

    ; Debug: passed wait DRQ
    movem.l d0-d1/a0-a1,-(sp)
    pea     .dbg_drq(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1

    ; Read 256 words (512 bytes)
    move.w  #255,d1
.read_loop:
    move.w  IDE_DATA,(a2)+
    dbf     d1,.read_loop

    ; Wait for completion
.wait_done:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_done

    ; Check for errors in final status
    btst    #STATUS_ERR,d0
    bne.s   .read_error

    ; Success
    moveq   #0,d0
    movem.l (sp)+,d3-d4/a2-a3
    rts

.timeout_waitready:
    moveq   #-2,d0                  ; Timeout in WaitReady
    movem.l (sp)+,d3-d4/a2-a3
    rts

.timeout_waitdrq:
    moveq   #-3,d0                  ; Timeout in WaitDRQ
    movem.l (sp)+,d3-d4/a2-a3
    rts

.read_error:
    moveq   #-1,d0                  ; Error code
    movem.l (sp)+,d3-d4/a2-a3
    rts

.dbg_enter:
    dc.b    "  DBG: Enter IDEReadSector",13,10,0
.dbg_ready:
    dc.b    "  DBG: Passed WaitReady",13,10,0
.dbg_gayle_init:
    dc.b    "  DBG: Initializing GAYLE...",13,10,0
.dbg_after_gayle:
    dc.b    "  DBG: Status after GAYLE init: $%x.b",13,10,0
.dbg_identify:
    dc.b    "  DBG: Sending IDENTIFY command...",13,10,0
.dbg_identify_status:
    dc.b    "  DBG: Status after IDENTIFY: $%x.b",13,10,0
.dbg_command:
    dc.b    "  DBG: Sent READ command, status: $%x.b",13,10,0
.dbg_drq:
    dc.b    "  DBG: Passed WaitDRQ",13,10,0
    even

; ============================================================
; IDETestRead - Test routine to read sector 0 and verify signature
; ============================================================
; Reads sector 0 to SECTOR_BUFFER and checks for boot signature.
; Sets background color: green ($0F0) = valid, red ($F00) = invalid
; Prints status via SerialPrintf
; ============================================================
IDETestRead:
    movem.l d0-d2/a0-a2,-(sp)

    ; Print initial message
    pea     .msg_waiting(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Check for drive presence (GAYLE returns 0x7F if no drive)
    moveq   #0,d1
    move.b  IDE_STATUS,d1

    ; Print initial status BEFORE checking
    move.w  d1,-(sp)
    pea     .msg_status(pc)
    bsr     SerialPrintf
    addq.l  #6,sp

    ; Now check for no drive
    cmpi.b  #$7F,d1
    beq     .no_drive

    ; Also check for all-ones (0xFF - another no-drive indicator)
    cmpi.b  #$FF,d1
    beq     .no_drive

    ; Clear buffer first to verify it gets updated
    lea     SECTOR_BUFFER,a0
    move.w  #127,d2                 ; 512 bytes / 4 = 128 longwords
.clear_loop:
    clr.l   (a0)+
    dbf     d2,.clear_loop

    ; Set up registers for read
    lea     SECTOR_BUFFER,a1        ; Buffer address
    moveq   #0,d0                   ; Sector 0 (LBA)

    ; Read sector 0
    bsr     IDEReadSector

    ; Debug: print result
    movem.l d0-d1/a0-a1,-(sp)
    move.l  d0,-(sp)
    pea     .msg_read_result(pc)
    bsr     SerialPrintf
    addq.l  #8,sp
    movem.l (sp)+,d0-d1/a0-a1

    tst.l   d0
    bne     .check_error

    ; Print read complete message
    pea     .msg_read_complete(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Print first 16 bytes of sector (4 longwords)
    lea     SECTOR_BUFFER,a0
    move.l  (a0),-(sp)
    move.l  4(a0),-(sp)
    move.l  8(a0),-(sp)
    move.l  12(a0),-(sp)
    pea     .msg_first_bytes(pc)
    bsr     SerialPrintf
    lea     20(sp),sp

    ; Check boot signature at offset 0x1FE
    lea     SECTOR_BUFFER,a0
    move.w  $1FE(a0),d1             ; Read signature word
    move.w  d1,-(sp)
    pea     .msg_signature(pc)
    bsr     SerialPrintf
    addq.l  #6,sp

    ; Check for standard boot signature (0x55AA)
    cmp.w   #$55AA,d1
    beq.s   .valid_boot
    ; Also check for byte-swapped version
    cmp.w   #$AA55,d1
    beq.s   .valid_boot
    ; Invalid signature
    pea     .msg_invalid_boot(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$F80,$DFF180           ; Orange background
    bra     .done

.valid_boot:
    pea     .msg_valid_boot(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$0F0,$DFF180           ; Green background
    bra     .done

.no_drive:
    pea     .msg_no_drive(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$F00,$DFF180           ; Red background
    bra     .done

.check_error:
    ; Print error code for debugging
    move.l  d0,-(sp)
    pea     .msg_error_code(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Check which error occurred
    cmp.l   #-2,d0
    beq.s   .timeout_ready
    cmp.l   #-3,d0
    beq.s   .timeout_drq
    ; Otherwise generic error
.read_error:
    pea     .msg_read_error(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$F00,$DFF180           ; Red background
    bra     .done

.timeout_ready:
    pea     .msg_timeout_ready(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$F00,$DFF180           ; Red background
    bra     .done

.timeout_drq:
    pea     .msg_timeout_drq(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    move.w  #$F00,$DFF180           ; Red background
    bra     .done

.done:
    movem.l (sp)+,d0-d2/a0-a2
    rts

; Messages
.msg_waiting:
    dc.b    "IDE: Waiting for drive ready...",13,10,0
.msg_status:
    dc.b    "IDE: Initial status register: $%x.b",13,10,0
.msg_read_result:
    dc.b    "IDE: Read sector returned: %d",13,10,0
.msg_no_drive:
    dc.b    "IDE: No drive present (status $7F)",13,10,0
.msg_error_code:
    dc.b    "IDE: Error code: %d",13,10,0
.msg_read_complete:
    dc.b    "IDE: Read complete!",13,10,0
.msg_first_bytes:
    dc.b    "IDE: First 16 bytes: %x.l %x.l %x.l %x.l",13,10,0
.msg_signature:
    dc.b    "IDE: Boot signature: $%x.w",13,10,0
.msg_valid_boot:
    dc.b    "IDE: Valid boot signature found!",13,10,0
.msg_invalid_boot:
    dc.b    "IDE: Invalid boot signature",13,10,0
.msg_read_error:
    dc.b    "IDE: Read error",13,10,0
.msg_timeout_ready:
    dc.b    "IDE: Timeout waiting for drive ready",13,10,0
.msg_timeout_drq:
    dc.b    "IDE: Timeout waiting for DRQ",13,10,0
    even
