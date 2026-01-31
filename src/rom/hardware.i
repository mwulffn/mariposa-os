; ============================================================
; hardware.i - Hardware definitions for Amiga A500
; ============================================================
; Include this in all source files that access hardware
; ============================================================

; ============================================================
; Custom chip base and registers
; ============================================================
CUSTOM          equ $DFF000

; DMA and interrupt control
DMACONR         equ $002
DMACON          equ $096
INTENA          equ $09A
INTREQ          equ $09C

; Beam position
VPOSR           equ $004
VHPOSR          equ $006

; Bitplane control
BPLCON0         equ $100
BPLCON1         equ $102
BPLCON2         equ $104
BPL1MOD         equ $108
BPL2MOD         equ $10A

; Bitplane pointers
BPL1PTH         equ $0E0
BPL1PTL         equ $0E2
BPL2PTH         equ $0E4
BPL2PTL         equ $0E6

; Display window
DIWSTRT         equ $08E
DIWSTOP         equ $090
DDFSTRT         equ $092
DDFSTOP         equ $094

; Color registers
COLOR00         equ $180
COLOR01         equ $182
COLOR02         equ $184
COLOR03         equ $186

; Copper
COP1LCH         equ $080
COP1LCL         equ $082
COP2LCH         equ $084
COP2LCL         equ $086
COPJMP1         equ $088
COPJMP2         equ $08A

; Blitter
BLTCON0         equ $040
BLTCON1         equ $042
BLTAFWM         equ $044
BLTALWM         equ $046
BLTCPTH         equ $048
BLTCPTL         equ $04A
BLTBPTH         equ $04C
BLTBPTL         equ $04E
BLTAPTH         equ $050
BLTAPTL         equ $052
BLTDPTH         equ $054
BLTDPTL         equ $056
BLTSIZE         equ $058
BLTCMOD         equ $060
BLTBMOD         equ $062
BLTAMOD         equ $064
BLTDMOD         equ $066
BLTCDAT         equ $070
BLTBDAT         equ $072
BLTADAT         equ $074

; ============================================================
; CIA-A registers (directly accessible)
; ============================================================
CIAA_PRA        equ $BFE001         ; Port A (active low)
CIAA_PRB        equ $BFE101         ; Port B
CIAA_DDRA       equ $BFE201         ; Data direction A
CIAA_DDRB       equ $BFE301         ; Data direction B
CIAA_TALO       equ $BFE401         ; Timer A low
CIAA_TAHI       equ $BFE501         ; Timer A high
CIAA_TBLO       equ $BFE601         ; Timer B low
CIAA_TBHI       equ $BFE701         ; Timer B high
CIAA_TODLO      equ $BFE801         ; TOD low
CIAA_TODMID     equ $BFE901         ; TOD mid
CIAA_TODHI      equ $BFEA01         ; TOD high
CIAA_SDR        equ $BFEC01         ; Serial data
CIAA_ICR        equ $BFED01         ; Interrupt control
CIAA_CRA        equ $BFEE01         ; Control A
CIAA_CRB        equ $BFEF01         ; Control B

; ============================================================
; CIA-B registers
; ============================================================
CIAB_PRA        equ $BFD000
CIAB_PRB        equ $BFD100
CIAB_DDRA       equ $BFD200
CIAB_DDRB       equ $BFD300
CIAB_TALO       equ $BFD400
CIAB_TAHI       equ $BFD500
CIAB_TBLO       equ $BFD600
CIAB_TBHI       equ $BFD700
CIAB_ICR        equ $BFDD00
CIAB_CRA        equ $BFDE00
CIAB_CRB        equ $BFDF00

; ============================================================
; Serial port registers (Custom chip)
; ============================================================
SERDATR         equ $018            ; Serial data/status (read)
SERDAT          equ $030            ; Serial data (write)
SERPER          equ $032            ; Serial period (baud rate)

; SERDATR status bits
SERDATR_TBE     equ 13              ; Transmit buffer empty
SERDATR_RBF     equ 11              ; Receive buffer full

; ============================================================
; Memory layout
; ============================================================
; Chip RAM - Custom chip accessible memory
CHIP_RAM_START  equ $000000
CHIP_RAM_MAX    equ $200000         ; 2MB max (ECS)

; Slow RAM - Trapdoor expansion (A500/A500+)
; Same speed as chip RAM but not DMA accessible
SLOW_RAM_START  equ $C00000
SLOW_RAM_MAX    equ $D80000         ; 1.8MB max, typically 512KB

; Fast RAM - Zorro II expansion (edge connector)
; Full CPU speed, no chip access
FAST_RAM_START  equ $200000
FAST_RAM_MAX    equ $A00000         ; 8MB max on Zorro II

; ROM
ROM_START       equ $FC0000
ROM_END         equ $FFFFFF

; ============================================================
; Our memory map (follows docs/rom_design.md)
; ============================================================
REG_DUMP_AREA   equ $000400         ; Register dump (80 bytes)
HEX_BUFFER      equ $000450         ; Hex output buffer (12 bytes)
CHIP_RAM_VAR    equ $000460         ; ChipRAMSize variable (4 bytes)
SLOW_RAM_VAR    equ $000464         ; SlowRAMSize variable (4 bytes)
FAST_RAM_VAR    equ $000468         ; FastRAMSize variable (4 bytes)
FAST_RAM_BASE   equ $00046C         ; FastRAMBase address (4 bytes)
DBG_STACK       equ $00084F         ; Debugger stack top
DBG_CMD_BUF     equ $000850         ; Command buffer (128 bytes)
COPPERLIST      equ $000950         ; Copper list (256 bytes)
SCREEN          equ $000A50         ; Display bitplane (10KB)
MEMMAP_TABLE    equ $003250         ; Memory map (432 bytes)
KERNEL_CHIP     equ $004000         ; Kernel-managed chip RAM start

; ============================================================
; ROM identification
; ============================================================
ROM_MAGIC       equ $414D4147       ; 'AMAG'
ROM_VERSION     equ $0001           ; Version 0.1
ROM_FLAGS       equ $0000           ; No flags

; ============================================================
; Memory map types
; ============================================================
MEM_TYPE_FREE   equ 1               ; Available RAM
MEM_TYPE_ROM    equ 2               ; Read-only memory
MEM_TYPE_RESERVED equ 3             ; Reserved (vectors, ROM data)
MEM_TYPE_END    equ 0               ; Table terminator

; ============================================================
; Display constants
; ============================================================
SCREEN_WIDTH    equ 320
SCREEN_HEIGHT   equ 256
SCREEN_DEPTH    equ 1               ; Bitplanes
BYTES_PER_ROW   equ (SCREEN_WIDTH/8)    ; 40 bytes

; ============================================================
; DMACON bits
; ============================================================
DMAF_SETCLR     equ $8000
DMAF_COPPER     equ $0080
DMAF_BLITTER    equ $0040
DMAF_SPRITE     equ $0020
DMAF_DISK       equ $0010
DMAF_AUD3       equ $0008
DMAF_AUD2       equ $0004
DMAF_AUD1       equ $0002
DMAF_AUD0       equ $0001
DMAF_AUDIO      equ $000F
DMAF_BLTPRI     equ $0400
DMAF_DMAEN      equ $0200
DMAF_BPLEN      equ $0100

; ============================================================
; INTENA/INTREQ bits
; ============================================================
INTF_SETCLR     equ $8000
INTF_INTEN      equ $4000
INTF_EXTER      equ $2000           ; CIA-B
INTF_DSKSYN     equ $1000
INTF_RBF        equ $0800           ; Serial receive
INTF_AUD3       equ $0400
INTF_AUD2       equ $0200
INTF_AUD1       equ $0100
INTF_AUD0       equ $0080
INTF_BLIT       equ $0040
INTF_VERTB      equ $0020           ; Vertical blank
INTF_COPER      equ $0010
INTF_PORTS      equ $0008           ; CIA-A
INTF_SOFTINT    equ $0004
INTF_DSKBLK     equ $0002
INTF_TBE        equ $0001           ; Serial transmit

; ============================================================
; 68000 exception vectors
; ============================================================
VEC_RESET_SSP   equ $000
VEC_RESET_PC    equ $004
VEC_BUS_ERROR   equ $008
VEC_ADDR_ERROR  equ $00C
VEC_ILLEGAL     equ $010
VEC_ZERO_DIV    equ $014
VEC_CHK         equ $018
VEC_TRAPV       equ $01C
VEC_PRIV_VIOL   equ $020
VEC_TRACE       equ $024
VEC_LINE_A      equ $028
VEC_LINE_F      equ $02C
VEC_SPURIOUS    equ $060
VEC_AUTOVEC1    equ $064
VEC_AUTOVEC2    equ $068
VEC_AUTOVEC3    equ $06C
VEC_AUTOVEC4    equ $070
VEC_AUTOVEC5    equ $074
VEC_AUTOVEC6    equ $078
VEC_AUTOVEC7    equ $07C
VEC_TRAP0       equ $080
VEC_TRAP15      equ $0BC
