/*
 * amiga_hw.h - Amiga hardware register definitions
 * 
 * Standalone header for bare-metal programming.
 * Based on Amiga NDK, no dependencies.
 */

#ifndef AMIGA_HW_H
#define AMIGA_HW_H

/* Basic types */
typedef unsigned char  UBYTE;
typedef unsigned short UWORD;
typedef unsigned long  ULONG;
typedef signed char    BYTE;
typedef signed short   WORD;
typedef signed long    LONG;

typedef volatile UBYTE VUBYTE;
typedef volatile UWORD VUWORD;
typedef volatile ULONG VULONG;

/* ============================================================
 * Custom Chip Registers ($DFF000)
 * ============================================================ */

struct Custom {
    VUWORD bltddat;      /* 000 Blitter dest data (read) */
    VUWORD dmaconr;      /* 002 DMA control read */
    VUWORD vposr;        /* 004 Vert beam pos (high) */
    VUWORD vhposr;       /* 006 Vert/horiz beam pos */
    VUWORD dskdatr;      /* 008 Disk data read */
    VUWORD joy0dat;      /* 00A Joystick 0 data */
    VUWORD joy1dat;      /* 00C Joystick 1 data */
    VUWORD clxdat;       /* 00E Collision data */
    VUWORD adkconr;      /* 010 Audio/disk control read */
    VUWORD pot0dat;      /* 012 Pot counter 0 */
    VUWORD pot1dat;      /* 014 Pot counter 1 */
    VUWORD potinp;       /* 016 Pot pin data read */
    VUWORD serdatr;      /* 018 Serial data read */
    VUWORD dskbytr;      /* 01A Disk data byte read */
    VUWORD intenar;      /* 01C Interrupt enable read */
    VUWORD intreqr;      /* 01E Interrupt request read */
    VULONG dskpt;        /* 020 Disk pointer */
    VUWORD dsklen;       /* 024 Disk length */
    VUWORD dskdat;       /* 026 Disk data write */
    VUWORD refptr;       /* 028 Refresh pointer */
    VUWORD vposw;        /* 02A Vert beam pos write */
    VUWORD vhposw;       /* 02C Vert/horiz pos write */
    VUWORD copcon;       /* 02E Copper control */
    VUWORD serdat;       /* 030 Serial data write */
    VUWORD serper;       /* 032 Serial period */
    VUWORD potgo;        /* 034 Pot control */
    VUWORD joytest;      /* 036 Joystick test */
    VUWORD strequ;       /* 038 Strobe for horiz sync */
    VUWORD strvbl;       /* 03A Strobe for vert blank */
    VUWORD strhor;       /* 03C Strobe for horiz blank */
    VUWORD strlong;      /* 03E Strobe for long line */
    VUWORD bltcon0;      /* 040 Blitter control 0 */
    VUWORD bltcon1;      /* 042 Blitter control 1 */
    VUWORD bltafwm;      /* 044 Blitter A first word mask */
    VUWORD bltalwm;      /* 046 Blitter A last word mask */
    VULONG bltcpt;       /* 048 Blitter C pointer */
    VULONG bltbpt;       /* 04C Blitter B pointer */
    VULONG bltapt;       /* 050 Blitter A pointer */
    VULONG bltdpt;       /* 054 Blitter D pointer */
    VUWORD bltsize;      /* 058 Blitter size (starts blit) */
    VUWORD bltcon0l;     /* 05A Blitter control 0 low (ECS) */
    VUWORD bltsizv;      /* 05C Blitter V size (ECS) */
    VUWORD bltsizh;      /* 05E Blitter H size (ECS) */
    VUWORD bltcmod;      /* 060 Blitter C modulo */
    VUWORD bltbmod;      /* 062 Blitter B modulo */
    VUWORD bltamod;      /* 064 Blitter A modulo */
    VUWORD bltdmod;      /* 066 Blitter D modulo */
    VUWORD pad1[4];      /* 068-06F reserved */
    VUWORD bltcdat;      /* 070 Blitter C data */
    VUWORD bltbdat;      /* 072 Blitter B data */
    VUWORD bltadat;      /* 074 Blitter A data */
    VUWORD pad2[3];      /* 076-07B reserved */
    VUWORD deniseid;     /* 07C Denise ID (ECS) */
    VUWORD dsksync;      /* 07E Disk sync pattern */
    VULONG cop1lc;       /* 080 Copper 1 location */
    VULONG cop2lc;       /* 084 Copper 2 location */
    VUWORD copjmp1;      /* 088 Copper 1 restart */
    VUWORD copjmp2;      /* 08A Copper 2 restart */
    VUWORD copins;       /* 08C Copper instruction fetch */
    VUWORD diwstrt;      /* 08E Display window start */
    VUWORD diwstop;      /* 090 Display window stop */
    VUWORD ddfstrt;      /* 092 Display data fetch start */
    VUWORD ddfstop;      /* 094 Display data fetch stop */
    VUWORD dmacon;       /* 096 DMA control write */
    VUWORD clxcon;       /* 098 Collision control */
    VUWORD intena;       /* 09A Interrupt enable */
    VUWORD intreq;       /* 09C Interrupt request */
    VUWORD adkcon;       /* 09E Audio/disk control */
    struct {
        VULONG lc;       /* Audio channel location */
        VUWORD len;      /* Audio channel length */
        VUWORD per;      /* Audio channel period */
        VUWORD vol;      /* Audio channel volume */
        VUWORD dat;      /* Audio channel data */
        VUWORD pad[2];
    } aud[4];            /* 0A0-0DF Audio channels 0-3 */
    VULONG bplpt[8];     /* 0E0-0FF Bitplane pointers */
    VUWORD bplcon0;      /* 100 Bitplane control 0 */
    VUWORD bplcon1;      /* 102 Bitplane control 1 */
    VUWORD bplcon2;      /* 104 Bitplane control 2 */
    VUWORD bplcon3;      /* 106 Bitplane control 3 (ECS) */
    VUWORD bpl1mod;      /* 108 Bitplane 1 modulo (odd) */
    VUWORD bpl2mod;      /* 10A Bitplane 2 modulo (even) */
    VUWORD bplcon4;      /* 10C Bitplane control 4 (AGA) */
    VUWORD clxcon2;      /* 10E Collision control 2 (AGA) */
    VUWORD bpldat[8];    /* 110-11F Bitplane data */
    VULONG sprpt[8];     /* 120-13F Sprite pointers */
    struct {
        VUWORD pos;      /* Sprite vert-horiz start */
        VUWORD ctl;      /* Sprite vert stop, control */
        VUWORD data;     /* Sprite data A */
        VUWORD datb;     /* Sprite data B */
    } spr[8];            /* 140-17F Sprite data */
    VUWORD color[32];    /* 180-1BF Color registers */
    VUWORD htotal;       /* 1C0 Horiz total (ECS) */
    VUWORD hsstop;       /* 1C2 Horiz sync stop (ECS) */
    VUWORD hbstrt;       /* 1C4 Horiz blank start (ECS) */
    VUWORD hbstop;       /* 1C6 Horiz blank stop (ECS) */
    VUWORD vtotal;       /* 1C8 Vert total (ECS) */
    VUWORD vsstop;       /* 1CA Vert sync stop (ECS) */
    VUWORD vbstrt;       /* 1CC Vert blank start (ECS) */
    VUWORD vbstop;       /* 1CE Vert blank stop (ECS) */
    VUWORD sprhstrt;     /* 1D0 (ECS) */
    VUWORD sprhstop;     /* 1D2 (ECS) */
    VUWORD bplhstrt;     /* 1D4 (ECS) */
    VUWORD bplhstop;     /* 1D6 (ECS) */
    VUWORD hhposw;       /* 1D8 (ECS) */
    VUWORD hhposr;       /* 1DA (ECS) */
    VUWORD beamcon0;     /* 1DC Beam control (ECS) */
    VUWORD hsstrt;       /* 1DE Horiz sync start (ECS) */
    VUWORD vsstrt;       /* 1E0 Vert sync start (ECS) */
    VUWORD hcenter;      /* 1E2 Horiz center (ECS) */
    VUWORD diwhigh;      /* 1E4 Display window high (ECS) */
    VUWORD pad3[11];     /* 1E6-1FB reserved */
    VUWORD fmode;        /* 1FC Fetch mode (AGA) */
    VUWORD noop;         /* 1FE No-op (NULL) */
};

#define CUSTOM_BASE ((struct Custom *)0xDFF000)
#define custom (*CUSTOM_BASE)

/* ============================================================
 * DMA Control (DMACON/DMACONR)
 * ============================================================ */

#define DMAF_SETCLR     (1<<15)  /* Set/clear bit */
#define DMAF_AUDIO0     (1<<0)   /* Audio channel 0 */
#define DMAF_AUDIO1     (1<<1)   /* Audio channel 1 */
#define DMAF_AUDIO2     (1<<2)   /* Audio channel 2 */
#define DMAF_AUDIO3     (1<<3)   /* Audio channel 3 */
#define DMAF_DISK       (1<<4)   /* Disk DMA */
#define DMAF_SPRITE     (1<<5)   /* Sprite DMA */
#define DMAF_BLITTER    (1<<6)   /* Blitter DMA */
#define DMAF_COPPER     (1<<7)   /* Copper DMA */
#define DMAF_RASTER     (1<<8)   /* Bitplane DMA */
#define DMAF_MASTER     (1<<9)   /* Master DMA enable */
#define DMAF_BLITHOG    (1<<10)  /* Blitter hog mode */
#define DMAF_BLTDONE    (1<<14)  /* Blitter done (read only) */

#define DMAF_AUDIO      (DMAF_AUDIO0|DMAF_AUDIO1|DMAF_AUDIO2|DMAF_AUDIO3)
#define DMAF_ALL        (DMAF_AUDIO|DMAF_DISK|DMAF_SPRITE|DMAF_BLITTER|DMAF_COPPER|DMAF_RASTER)

/* ============================================================
 * Interrupt Control (INTENA/INTREQ)
 * ============================================================ */

#define INTF_SETCLR     (1<<15)  /* Set/clear bit */
#define INTF_TBE        (1<<0)   /* Serial transmit buffer empty */
#define INTF_DSKBLK     (1<<1)   /* Disk block done */
#define INTF_SOFTINT    (1<<2)   /* Software interrupt */
#define INTF_PORTS      (1<<3)   /* I/O ports and timers */
#define INTF_COPER      (1<<4)   /* Copper */
#define INTF_VERTB      (1<<5)   /* Vertical blank */
#define INTF_BLIT       (1<<6)   /* Blitter done */
#define INTF_AUD0       (1<<7)   /* Audio channel 0 */
#define INTF_AUD1       (1<<8)   /* Audio channel 1 */
#define INTF_AUD2       (1<<9)   /* Audio channel 2 */
#define INTF_AUD3       (1<<10)  /* Audio channel 3 */
#define INTF_RBF        (1<<11)  /* Serial receive buffer full */
#define INTF_DSKSYNC    (1<<12)  /* Disk sync found */
#define INTF_EXTER      (1<<13)  /* External interrupt */
#define INTF_INTEN      (1<<14)  /* Master interrupt enable */

/* ============================================================
 * Bitplane Control (BPLCON0)
 * ============================================================ */

#define BPLCON0_HIRES   (1<<15)  /* Hi-res mode */
#define BPLCON0_BPU2    (1<<14)  /* Bitplanes bit 2 */
#define BPLCON0_BPU1    (1<<13)  /* Bitplanes bit 1 */
#define BPLCON0_BPU0    (1<<12)  /* Bitplanes bit 0 */
#define BPLCON0_HAM     (1<<11)  /* HAM mode */
#define BPLCON0_DPF     (1<<10)  /* Dual playfield */
#define BPLCON0_COLOR   (1<<9)   /* Composite color enable */
#define BPLCON0_GAUD    (1<<8)   /* Genlock audio enable */
#define BPLCON0_UHRES   (1<<7)   /* Ultra hi-res (ECS) */
#define BPLCON0_SHRES   (1<<6)   /* Super hi-res (ECS) */
#define BPLCON0_BYPASS  (1<<5)   /* Bypass color table (ECS) */
#define BPLCON0_LPEN    (1<<3)   /* Light pen enable */
#define BPLCON0_LACE    (1<<2)   /* Interlace enable */
#define BPLCON0_ERSY    (1<<1)   /* External resync */
#define BPLCON0_ECSENA  (1<<0)   /* ECS enable (ECS) */

/* ============================================================
 * CIA Registers ($BFE001, $BFD000)
 * ============================================================ */

struct CIA {
    VUBYTE pra;      UBYTE pad0[0xFF];
    VUBYTE prb;      UBYTE pad1[0xFF];
    VUBYTE ddra;     UBYTE pad2[0xFF];
    VUBYTE ddrb;     UBYTE pad3[0xFF];
    VUBYTE talo;     UBYTE pad4[0xFF];
    VUBYTE tahi;     UBYTE pad5[0xFF];
    VUBYTE tblo;     UBYTE pad6[0xFF];
    VUBYTE tbhi;     UBYTE pad7[0xFF];
    VUBYTE todlo;    UBYTE pad8[0xFF];
    VUBYTE todmid;   UBYTE pad9[0xFF];
    VUBYTE todhi;    UBYTE padA[0xFF];
    VUBYTE pad_unused; UBYTE padB[0xFF];
    VUBYTE sdr;      UBYTE padC[0xFF];
    VUBYTE icr;      UBYTE padD[0xFF];
    VUBYTE cra;      UBYTE padE[0xFF];
    VUBYTE crb;
};

#define CIAA_BASE ((struct CIA *)0xBFE001)
#define CIAB_BASE ((struct CIA *)0xBFD000)
#define ciaa (*CIAA_BASE)
#define ciab (*CIAB_BASE)

/* CIA-A PRA bits */
#define CIAA_PA_OVL     (1<<0)   /* ROM overlay */
#define CIAA_PA_LED     (1<<1)   /* Power LED (active low) */
#define CIAA_PA_CHNG    (1<<2)   /* Disk change */
#define CIAA_PA_WPRO    (1<<3)   /* Disk write protect */
#define CIAA_PA_TK0     (1<<4)   /* Disk track 0 */
#define CIAA_PA_RDY     (1<<5)   /* Disk ready */
#define CIAA_PA_FIR0    (1<<6)   /* Fire button 0 */
#define CIAA_PA_FIR1    (1<<7)   /* Fire button 1 */

/* CIA-B PRA bits */
#define CIAB_PA_BUSY    (1<<0)   /* Parallel busy */
#define CIAB_PA_POUT    (1<<1)   /* Parallel out */
#define CIAB_PA_SEL     (1<<2)   /* Parallel select */
#define CIAB_PA_DSR     (1<<3)   /* Serial DSR */
#define CIAB_PA_CTS     (1<<4)   /* Serial CTS */
#define CIAB_PA_CD      (1<<5)   /* Serial CD */
#define CIAB_PA_RTS     (1<<6)   /* Serial RTS */
#define CIAB_PA_DTR     (1<<7)   /* Serial DTR */

/* CIA-B PRB bits (accent disk control) */
#define CIAB_PB_STEP    (1<<0)   /* Disk step */
#define CIAB_PB_DIR     (1<<1)   /* Disk direction */
#define CIAB_PB_SIDE    (1<<2)   /* Disk side select */
#define CIAB_PB_SEL0    (1<<3)   /* Disk select 0 */
#define CIAB_PB_SEL1    (1<<4)   /* Disk select 1 */
#define CIAB_PB_SEL2    (1<<5)   /* Disk select 2 */
#define CIAB_PB_SEL3    (1<<6)   /* Disk select 3 */
#define CIAB_PB_MTR     (1<<7)   /* Disk motor */

/* CIA ICR bits */
#define CIAICRB_TA      0        /* Timer A */
#define CIAICRB_TB      1        /* Timer B */
#define CIAICRB_ALRM    2        /* TOD alarm */
#define CIAICRB_SP      3        /* Serial port */
#define CIAICRB_FLG     4        /* FLAG pin */
#define CIAICRB_IR      7        /* Interrupt (read) / Set-clear (write) */

#define CIAICRF_TA      (1<<CIAICRB_TA)
#define CIAICRF_TB      (1<<CIAICRB_TB)
#define CIAICRF_ALRM    (1<<CIAICRB_ALRM)
#define CIAICRF_SP      (1<<CIAICRB_SP)
#define CIAICRF_FLG     (1<<CIAICRB_FLG)
#define CIAICRF_IR      (1<<CIAICRB_IR)
#define CIAICRF_SETCLR  (1<<CIAICRB_IR)

/* CIA CRA bits */
#define CIACRAB_START   0        /* Start timer */
#define CIACRAB_PBON    1        /* PB6 output */
#define CIACRAB_OUTMODE 2        /* Toggle/pulse */
#define CIACRAB_RUNMODE 3        /* One-shot/continuous */
#define CIACRAB_LOAD    4        /* Force load */
#define CIACRAB_INMODE  5        /* PHI2/CNT */
#define CIACRAB_SPMODE  6        /* Serial port mode */
#define CIACRAB_TODIN   7        /* 50/60 Hz TOD input */

#define CIACRAF_START   (1<<CIACRAB_START)
#define CIACRAF_PBON    (1<<CIACRAB_PBON)
#define CIACRAF_OUTMODE (1<<CIACRAB_OUTMODE)
#define CIACRAF_RUNMODE (1<<CIACRAB_RUNMODE)
#define CIACRAF_LOAD    (1<<CIACRAB_LOAD)
#define CIACRAF_INMODE  (1<<CIACRAB_INMODE)
#define CIACRAF_SPMODE  (1<<CIACRAB_SPMODE)
#define CIACRAF_TODIN   (1<<CIACRAB_TODIN)

/* ============================================================
 * Serial (SERDAT/SERDATR/SERPER)
 * ============================================================ */

#define SERDATF_OVRUN   (1<<15)  /* Overrun */
#define SERDATF_RBF     (1<<14)  /* Receive buffer full */
#define SERDATF_TBE     (1<<13)  /* Transmit buffer empty */
#define SERDATF_TSRE    (1<<12)  /* Transmit shift empty */
#define SERDATF_RXD     (1<<11)  /* RXD pin state */

/* Serial period for common baud rates (PAL: 3546895 Hz) */
#define SERPER_9600     (368)    /* 3546895 / 9600 - 1 */
#define SERPER_19200    (184)    /* 3546895 / 19200 - 1 */
#define SERPER_38400    (91)     /* 3546895 / 38400 - 1 */
#define SERPER_57600    (60)     /* 3546895 / 57600 - 1 */
#define SERPER_115200   (30)     /* 3546895 / 115200 - 1 */

/* ============================================================
 * Utility Macros
 * ============================================================ */

/* Wait for vertical blank */
#define WaitVBL() while (!(custom.intreqr & INTF_VERTB)); \
                  custom.intreq = INTF_VERTB

/* Wait for blitter */
#define WaitBlit() while (custom.dmaconr & DMAF_BLTDONE)

/* RGB4 color (0-15 per component) */
#define RGB4(r,g,b) (((r)<<8)|((g)<<4)|(b))

/* Copper instructions */
#define CMOVE(reg,val) ((((ULONG)(reg) & 0x1FE) << 16) | ((UWORD)(val)))
#define CWAIT(vp,hp)   ((((ULONG)(vp) & 0xFF) << 24) | (((ULONG)(hp) & 0xFE) << 16) | 0xFFFE)
#define CEND           0xFFFFFFFE

#endif /* AMIGA_HW_H */
