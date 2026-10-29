/*
 * Platform Expert for Qualcomm MSM8916 (Snapdragon 410).
 *
 * Primary target is the Motorola Moto G4 Play ("harpia", XT1603), but the
 * code only depends on the SoC-level blocks that are common across MSM8916
 * boards: the BLSP UARTDM console, the QGIC2 interrupt controller and the
 * ARMv7 architected (generic) timer.
 */

#if defined(BOARD_CONFIG_MSM8916)

#include <mach/mach_types.h>

#include <IOKit/IOPlatformExpert.h>

#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>
#include <pexpert/arm/boot.h>

#include <machine/machine_routines.h>

#include <vm/pmap.h>
#include <arm/pmap.h>

#include "pe_msm8916.h"

#define KPRINTF_PREFIX  "PE_msm8916: "

/* Serial console is driven byte-at-a-time; tick runs at 100 Hz. */
#define MSM8916_TICK_HZ                 100

extern void rtclock_intr(arm_saved_state_t * regs);
extern void rtc_configure(uint64_t hz);
extern void irq_iokit_dispatch(uint32_t irq);

uint64_t msm8916_get_timebase(void);
uint64_t msm8916_timer_value(void);
void msm8916_timer_enabled(int enable);

static vm_offset_t gMsm8916UartBase = 0;
static vm_offset_t gMsm8916GicDistBase = 0;
static vm_offset_t gMsm8916GicCpuBase = 0;

static uint64_t clock_decrementer = 0;
static boolean_t clock_initialized = FALSE;
static boolean_t clock_had_irq = FALSE;

/*
 * ARMv7 architected timer accessors. The non-secure EL1 physical timer
 * (CNTP) is used for the tick and CNTPCT for the free-running timebase;
 * the secure firmware is expected to have enabled CNTHCTL.PL1PCEN.
 */
static inline uint32_t msm8916_read_cntfrq(void)
{
    uint32_t value;

    __asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r" (value));
    return value;
}

static inline uint64_t msm8916_read_cntpct(void)
{
    uint32_t lo, hi;

    __asm__ __volatile__("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
    return ((uint64_t) hi << 32) | lo;
}

static inline uint32_t msm8916_read_cntp_tval(void)
{
    uint32_t value;

    __asm__ __volatile__("mrc p15, 0, %0, c14, c2, 0" : "=r" (value));
    return value;
}

static inline void msm8916_write_cntp_tval(uint32_t value)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 0" :: "r" (value));
}

static inline void msm8916_write_cntp_ctl(uint32_t value)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1" :: "r" (value));
}

static void timer_configure(void)
{
    uint64_t hz = msm8916_read_cntfrq();

    if (!hz)
        hz = MSM8916_TIMEBASE_FREQ_HZ;

    gPEClockFrequencyInfo.timebase_frequency_hz = hz;

    clock_decrementer = hz / MSM8916_TICK_HZ;
    kprintf(KPRINTF_PREFIX "decrementer frequency = %llu\n", clock_decrementer);

    rtc_configure(hz);
}

void msm8916_putc(int c)
{
    if (!gMsm8916UartBase)
        return;

    if (c == '\n')
        msm8916_putc('\r');

    /*
     * NCF_TX is only safe to write once the TX FIFO has drained.
     */
    while (!(regread32(gMsm8916UartBase + UARTDM_SR) & UARTDM_SR_TXEMT))
        barrier();

    regwrite32(gMsm8916UartBase + UARTDM_NCF_TX, 1);

    while (!(regread32(gMsm8916UartBase + UARTDM_SR) & UARTDM_SR_TXRDY))
        barrier();

    regwrite32(gMsm8916UartBase + UARTDM_TF, (uint32_t) (c & 0xFF));
}

int msm8916_getc(void)
{
    int c = -1;

    if (!gMsm8916UartBase)
        return c;

    if (regread32(gMsm8916UartBase + UARTDM_SR) & UARTDM_SR_RXRDY) {
        c = (int) (regread32(gMsm8916UartBase + UARTDM_RF) & 0xFF);
    }

    return c;
}

void msm8916_uart_init(void)
{
    gMsm8916UartBase = ml_io_map(MSM8916_UART1_BASE, MSM8916_UART1_SIZE);
    assert(gMsm8916UartBase);

    /* 8 data bits, no parity, 1 stop bit; Data Mover disabled. */
    regwrite32(gMsm8916UartBase + UARTDM_MR1, 0);
    regwrite32(gMsm8916UartBase + UARTDM_MR2, UARTDM_MR2_8N1);
    regwrite32(gMsm8916UartBase + UARTDM_DMEN, 0);
    regwrite32(gMsm8916UartBase + UARTDM_IMR, 0);

    /* Flush the state machines, then bring TX/RX up. */
    regwrite32(gMsm8916UartBase + UARTDM_CR, UARTDM_CR_CMD_RESET_RX);
    regwrite32(gMsm8916UartBase + UARTDM_CR, UARTDM_CR_CMD_RESET_TX);
    regwrite32(gMsm8916UartBase + UARTDM_CR, UARTDM_CR_CMD_RESET_ERR_STAT);
    regwrite32(gMsm8916UartBase + UARTDM_CR, UARTDM_CR_TX_ENABLE);
    regwrite32(gMsm8916UartBase + UARTDM_CR, UARTDM_CR_RX_ENABLE);
}

/* Initialize the QGIC2 distributor. */
static void msm8916_gic_dist_init(void)
{
    uint32_t num_irq;
    uint32_t cpumask = 1;
    uint32_t i;

    cpumask |= cpumask << 8;
    cpumask |= cpumask << 16;

    /* Take the distributor offline while it is reconfigured. */
    regwrite32(gMsm8916GicDistBase + GIC_DIST_CTRL, 0);

    /* Number of implemented interrupt lines. */
    num_irq = (regread32(gMsm8916GicDistBase + GIC_DIST_CTR) & 0x1F);
    num_irq = (num_irq + 1) * 32;

    /* All SPIs level-sensitive, active high. */
    for (i = 32; i < num_irq; i += 16)
        regwrite32(gMsm8916GicDistBase + GIC_DIST_CONFIG + (i / 16) * 4, 0);

    /* Route every SPI to this CPU. */
    for (i = 32; i < num_irq; i += 4)
        regwrite32(gMsm8916GicDistBase + GIC_DIST_TARGET + i, cpumask);

    /* Equal priority for everything. */
    for (i = 0; i < num_irq; i += 4)
        regwrite32(gMsm8916GicDistBase + GIC_DIST_PRI + i, 0xA0A0A0A0);

    /* Mask everything, then leave PPIs/SGIs alone. */
    for (i = 0; i < num_irq; i += 32)
        regwrite32(gMsm8916GicDistBase + GIC_DIST_ENABLE_CLEAR + (i / 32) * 4, 0xFFFFFFFF);

    regwrite32(gMsm8916GicDistBase + GIC_DIST_CTRL, 1);
}

/* Initialize the QGIC2 CPU interface. */
static void msm8916_gic_cpu_init(void)
{
    regwrite32(gMsm8916GicCpuBase + GIC_CPU_PRIMASK, 0xF0);
    regwrite32(gMsm8916GicCpuBase + GIC_CPU_CTRL, 1);
}

void msm8916_interrupt_init(void)
{
    ml_set_interrupts_enabled(FALSE);

    gMsm8916GicDistBase = ml_io_map(MSM8916_GIC_DIST_BASE, MSM8916_GIC_DIST_SIZE);
    gMsm8916GicCpuBase = ml_io_map(MSM8916_GIC_CPU_BASE, MSM8916_GIC_CPU_SIZE);
    assert(gMsm8916GicDistBase && gMsm8916GicCpuBase);

    msm8916_gic_dist_init();
    msm8916_gic_cpu_init();
}

void msm8916_timebase_init(void)
{
    uint32_t reg;
    uint32_t bit;

    timer_configure();

    ml_set_interrupts_enabled(FALSE);

    msm8916_timer_enabled(FALSE);

    /* Unmask the architected timer PPI in the distributor. */
    reg = GIC_DIST_ENABLE_SET + (INT_MSM8916_TIMER / 32) * 4;
    bit = 1 << (INT_MSM8916_TIMER & 31);
    regwrite32(gMsm8916GicDistBase + reg, bit);

    ml_set_interrupts_enabled(TRUE);

    msm8916_timer_enabled(TRUE);

    clock_initialized = TRUE;

    kprintf(KPRINTF_PREFIX "waiting for timer...\n");
    while (!clock_had_irq)
        barrier();

    kprintf(KPRINTF_PREFIX "timer is now up, timebase %llu\n", msm8916_get_timebase());
}

void msm8916_handle_interrupt(void *context)
{
    uint32_t irq;

    if (!gMsm8916GicCpuBase)
        return;

    irq = regread32(gMsm8916GicCpuBase + GIC_CPU_INTACK);

    /* Spurious or out-of-range interrupt. */
    if (irq >= GIC_INT_SPURIOUS)
        return;

    if (irq == INT_MSM8916_TIMER) {
        msm8916_timer_enabled(FALSE);

        rtclock_intr((arm_saved_state_t *) context);

        msm8916_timer_enabled(TRUE);

        clock_had_irq = TRUE;
    } else {
        irq_iokit_dispatch(irq);
    }

    regwrite32(gMsm8916GicCpuBase + GIC_CPU_EOI, irq);
}

uint64_t msm8916_get_timebase(void)
{
    if (!clock_initialized)
        return 0;

    return msm8916_read_cntpct();
}

uint64_t msm8916_timer_value(void)
{
    return msm8916_read_cntp_tval();
}

void msm8916_timer_enabled(int enable)
{
    if (enable) {
        msm8916_write_cntp_tval((uint32_t) clock_decrementer);
        msm8916_write_cntp_ctl(MSM8916_CNTP_CTL_ENABLE);
    } else {
        msm8916_write_cntp_ctl(MSM8916_CNTP_CTL_IMASK);
    }
}

/*
 * Stub for printing out to the framebuffer.
 */
void vcputc(__unused int l, __unused int u, int c);

static void _fb_putc(int c)
{
    if (c == '\n') {
        _fb_putc('\r');
    }
    vcputc(0, 0, c);
    msm8916_putc(c);
}

void msm8916_framebuffer_init(void)
{
    /*
     * Emulated framebuffer until the real MDP/DSI panel path exists.
     * Sized for the harpia panel: 720x1280 at 32bpp.
     */
    uint32_t depth = 4;
    uint32_t width = 720;
    uint32_t height = 1280;

    void *framebuffer = pmap_steal_memory(width * height * depth);
    void *framebuffer_phys = (void *) pmap_extract(kernel_pmap, (vm_map_offset_t) framebuffer);

    PE_state.video.v_baseAddr = (unsigned long) framebuffer_phys;
    PE_state.video.v_rowBytes = width * depth;
    PE_state.video.v_width = width;
    PE_state.video.v_height = height;
    PE_state.video.v_depth = depth * 8;

    kprintf(KPRINTF_PREFIX "fake framebuffer initialized\n");
    bzero(framebuffer, width * height * depth);

    initialize_screen((void *) &PE_state.video, kPETextMode);
}

int msm8916_halt_restart(int type)
{
    return 0;
}

/*
 * Setup the MSM8916 SoC dispatch table
 */
void PE_init_SocSupport_msm8916(void)
{
    gPESocDispatch.uart_getc = msm8916_getc;
    gPESocDispatch.uart_putc = msm8916_putc;
    gPESocDispatch.uart_init = msm8916_uart_init;

    gPESocDispatch.interrupt_init = msm8916_interrupt_init;
    gPESocDispatch.timebase_init = msm8916_timebase_init;

    gPESocDispatch.get_timebase = msm8916_get_timebase;

    gPESocDispatch.handle_interrupt = msm8916_handle_interrupt;

    gPESocDispatch.timer_value = msm8916_timer_value;
    gPESocDispatch.timer_enabled = msm8916_timer_enabled;

    gPESocDispatch.framebuffer_init = msm8916_framebuffer_init;

    msm8916_uart_init();
    msm8916_framebuffer_init();

    PE_halt_restart = msm8916_halt_restart;
}

void PE_init_SocSupport_stub(void)
{
    PE_early_puts("PE_init_SocSupport: Initializing for Qualcomm MSM8916\n");
    PE_init_SocSupport_msm8916();
}

#endif /* !BOARD_CONFIG_MSM8916 */
