; ============================================================
; ide.s - IDE/ATA hard drive interface
; ============================================================
; Provides basic IDE sector read functionality using LBA mode
; ============================================================

; ============================================================
; IDE Register Addresses (Gayle - 4-byte spacing)
; ============================================================
IDE_BASE        equ $DA0000
IDE_DATA        equ $DA0002         ; 16-bit data port
IDE_ERROR       equ $DA0006         ; Error register (read)
IDE_NSECTOR     equ $DA000A         ; Sector count
IDE_SECTOR      equ $DA000E         ; LBA bits 0-7
IDE_LCYL        equ $DA0012         ; LBA bits 8-15
IDE_HCYL        equ $DA0016         ; LBA bits 16-23
IDE_SELECT      equ $DA001A         ; LBA bits 24-27 + mode
IDE_STATUS      equ $DA001E         ; Status (read)
IDE_COMMAND     equ $DA001E         ; Command (write)

; Status register bits
IDE_BSY         equ 7               ; Busy
IDE_DRDY        equ 6               ; Drive ready
IDE_DRQ         equ 3               ; Data request
IDE_ERR         equ 0               ; Error

; Commands
IDE_CMD_READ    equ $20             ; Read sectors

; LBA mode + master drive
IDE_LBA_MASTER  equ $E0

; Timeout value
IDE_TIMEOUT     equ $100000         ; Timeout counter

; Destination address for test read
IDE_DEST        equ $30000

; ============================================================
; IDERead - Read sectors from IDE drive
; ============================================================
; Input:
;   A0.l = Destination buffer (must be word-aligned)
;   D0.l = Starting LBA (bits 0-27 used)
;   D1.w = Number of sectors to read (1-256, 0 treated as error)
; Output:
;   D0.l = 0 success, -1 error
; Preserves: D2-D7/A2-A6 (Amiga convention)
; Scratches: D0-D1/A0-A1
; ============================================================
IDERead:
    movem.l d2-d3,-(sp)

    ; Validate inputs
    tst.w   d1
    beq     .invalid                ; D1 == 0 is error
    cmp.w   #256,d1
    bhi     .invalid                ; D1 > 256 is error

    ; Wait for drive not busy
    bsr     IDEWaitNotBusy
    tst.l   d0
    bne     .exit                   ; Timeout, return -1

    ; Set up LBA registers from D0
    ; Save D1 (sector count) in D3
    move.w  d1,d3

    ; D0.l = LBA, need to split into 4 bytes
    move.b  d0,IDE_SECTOR           ; LBA bits 0-7
    lsr.l   #8,d0
    move.b  d0,IDE_LCYL             ; LBA bits 8-15
    lsr.l   #8,d0
    move.b  d0,IDE_HCYL             ; LBA bits 16-23
    lsr.l   #8,d0
    and.b   #$0F,d0                 ; LBA bits 24-27
    or.b    #IDE_LBA_MASTER,d0      ; Add LBA mode + master
    move.b  d0,IDE_SELECT

    ; Set sector count
    move.b  d3,IDE_NSECTOR          ; Low byte of D3

    ; Issue read command
    move.b  #IDE_CMD_READ,IDE_COMMAND

    ; Read sectors
.sector_loop:
    ; Wait for DRQ or error
    bsr     IDEWaitDRQ
    tst.l   d0
    bne     .exit                   ; Error, return -1

    ; Read 256 words (512 bytes)
    move.w  #255,d2
.word_loop:
    move.w  IDE_DATA,(a0)+
    dbf     d2,.word_loop

    ; Next sector
    subq.w  #1,d3
    bne.s   .sector_loop

    ; Wait for drive not busy after transfer
    bsr     IDEWaitNotBusy
    tst.l   d0
    bne     .exit                   ; Timeout, return -1

    ; Check final status for errors
    move.b  IDE_STATUS,d0
    btst    #IDE_ERR,d0
    bne.s   .error

    ; Success
    moveq   #0,d0
    bra.s   .exit

.invalid:
.error:
    moveq   #-1,d0

.exit:
    movem.l (sp)+,d2-d3
    rts

; ============================================================
; IDETestRead - Read sector 0 to $30000
; ============================================================
; Input:  None
; Output: D0 = 0 success, -1 error
; Preserves: All registers except D0
; ============================================================
IDETestRead:
    movem.l d1-d7/a0-a6,-(sp)

    ; Print start message
    pea     .msg_start(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Check if drive is present (status should not be $7F)
    move.b  IDE_STATUS,d0
    cmp.b   #$7F,d0
    bne.s   .drive_present

    ; No drive detected
    pea     .msg_no_drive(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    bra     .exit

.drive_present:
    ; Call IDERead(dest=$30000, lba=0, count=1)
    lea     IDE_DEST,a0
    moveq   #0,d0                   ; LBA 0
    moveq   #1,d1                   ; 1 sector
    bsr     IDERead
    tst.l   d0
    bne     .error

    ; Success! Print first 4 bytes
    move.l  IDE_DEST,d0
    move.l  d0,-(sp)
    pea     .msg_success(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    moveq   #0,d0                   ; Return success
    bra.s   .exit

.error:
    ; Print error with status and error registers
    moveq   #0,d1
    moveq   #0,d2
    move.b  IDE_STATUS,d1
    move.b  IDE_ERROR,d2
    move.l  d2,-(sp)                ; Error register
    move.l  d1,-(sp)                ; Status register
    pea     .msg_error(pc)
    bsr     SerialPrintf
    lea     12(sp),sp
    moveq   #-1,d0

.exit:
    movem.l (sp)+,d1-d7/a0-a6
    rts

.msg_start:
    dc.b    "IDE: Reading sector 0...",13,10,0
.msg_no_drive:
    dc.b    "IDE: No drive (status=$7F)",13,10,0
.msg_error:
    dc.b    "IDE: Error status=%x.b error=%x.b",13,10,0
.msg_success:
    dc.b    "IDE: Success! First bytes: %x.l",13,10,0
    even

; ============================================================
; IDEWaitNotBusy - Wait for BSY bit to clear
; ============================================================
; Output: D0 = 0 success, -1 timeout
; Preserves: All except D0
; ============================================================
IDEWaitNotBusy:
    movem.l d1-d2,-(sp)
    move.l  #IDE_TIMEOUT,d1
.loop:
    move.b  IDE_STATUS,d2
    btst    #IDE_BSY,d2
    beq.s   .done
    subq.l  #1,d1
    bne.s   .loop
    ; Timeout
    moveq   #-1,d0
    bra.s   .exit
.done:
    moveq   #0,d0
.exit:
    movem.l (sp)+,d1-d2
    rts

; ============================================================
; IDEWaitDRQ - Wait for DRQ bit or error
; ============================================================
; Output: D0 = 0 (DRQ set), -1 (error/timeout)
; Preserves: All except D0
; ============================================================
IDEWaitDRQ:
    movem.l d1-d2,-(sp)
    move.l  #IDE_TIMEOUT,d1
.loop:
    move.b  IDE_STATUS,d2
    btst    #IDE_ERR,d2
    bne.s   .error
    btst    #IDE_DRQ,d2
    bne.s   .done
    subq.l  #1,d1
    bne.s   .loop
    ; Timeout
.error:
    moveq   #-1,d0
    bra.s   .exit
.done:
    moveq   #0,d0
.exit:
    movem.l (sp)+,d1-d2
    rts
