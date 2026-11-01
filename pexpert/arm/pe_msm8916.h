/*
 * Qualcomm MSM8916 / Snapdragon 410 platform definitions.
 *
 * Primary target is the Motorola Moto G4 Play ("harpia", XT1603).
 */

#ifndef _PEXPERT_MSM8916_H_
#define _PEXPERT_MSM8916_H_

/* Physical addresses. */
#define MSM8916_PERIPH_SS_BASE          0x07800000
#define MSM8916_APPS_SS_BASE            0x0B000000

/* BLSP1 UART1: the harpia console/debug port. */
#define MSM8916_UART1_BASE              (MSM8916_PERIPH_SS_BASE + 0x000AF000)
#define MSM8916_UART1_SIZE              0x200

/*
 * UARTDM v1.4 ("BLSP") register offsets.
 *
 * These differ from the pre-BLSP uartdm layout: on v1.4 the FIFO and the
 * status/control registers live in the 0xa0-0x140 window instead of the
 * legacy 0x08-0x70 window.
 */
#define UARTDM_MR1                      0x000
#define UARTDM_MR2                      0x004
#define UARTDM_IPR                      0x018
#define UARTDM_TFWR                     0x01C
#define UARTDM_RFWR                     0x020
#define UARTDM_HCR                      0x024
#define UARTDM_DMRX                     0x034
#define UARTDM_DMEN                     0x03C
#define UARTDM_NCF_TX                   0x040
#define UARTDM_TXFS                     0x04C
#define UARTDM_RXFS                     0x050
#define UARTDM_CSR                      0x0A0
#define UARTDM_SR                       0x0A4
#define UARTDM_CR                       0x0A8
#define UARTDM_MISR                     0x0AC
#define UARTDM_IMR                      0x0B0
#define UARTDM_ISR                      0x0B4
#define UARTDM_RX_TOTAL_SNAP            0x0BC
#define UARTDM_TF                       0x100
#define UARTDM_RF                       0x140

/* UARTDM_SR status bits. */
#define UARTDM_SR_RXRDY                 (1 << 0)
#define UARTDM_SR_RXFULL                (1 << 1)
#define UARTDM_SR_TXRDY                 (1 << 2)
#define UARTDM_SR_TXEMT                 (1 << 3)
#define UARTDM_SR_OVERRUN               (1 << 4)
#define UARTDM_SR_PAR_FRAME_ERR         (1 << 5)

/* UARTDM_CR bits. */
#define UARTDM_CR_RX_ENABLE             (1 << 0)
#define UARTDM_CR_RX_DISABLE            (1 << 1)
#define UARTDM_CR_TX_ENABLE             (1 << 2)
#define UARTDM_CR_TX_DISABLE            (1 << 3)

/* UARTDM_CR channel commands: 4-bit opcode split across bits 4-7 and 11-14. */
#define UARTDM_CR_CMD(cmd)              ((((cmd) & 0x0F) << 4) | (((cmd) >> 4) << 11))
#define UARTDM_CR_CMD_RESET_RX          UARTDM_CR_CMD(0x01)
#define UARTDM_CR_CMD_RESET_TX          UARTDM_CR_CMD(0x02)
#define UARTDM_CR_CMD_RESET_ERR_STAT    UARTDM_CR_CMD(0x03)
#define UARTDM_CR_CMD_RESET_STALE_INT   UARTDM_CR_CMD(0x08)
#define UARTDM_CR_CMD_RESET_TX_READY    (3 << 8)

/* UARTDM_DMEN bits; v1.4 uses the *_BAM_ENABLE bits. */
#define UARTDM_DMEN_TX_BAM_ENABLE       (1 << 2)
#define UARTDM_DMEN_RX_BAM_ENABLE       (1 << 3)

/* MR2: 8 data bits, no parity, 1 stop bit. */
#define UARTDM_MR2_8N1                  0x03

/* QGIC2 (GICv2) register windows. */
#define MSM8916_GIC_DIST_BASE           (MSM8916_APPS_SS_BASE + 0x000000)
#define MSM8916_GIC_CPU_BASE            (MSM8916_APPS_SS_BASE + 0x002000)

#define MSM8916_GIC_DIST_SIZE           0x1000
#define MSM8916_GIC_CPU_SIZE            0x2000

/* GIC distributor registers. */
#define GIC_DIST_CTRL                   0x000
#define GIC_DIST_CTR                    0x004
#define GIC_DIST_ENABLE_SET             0x100
#define GIC_DIST_ENABLE_CLEAR           0x180
#define GIC_DIST_PENDING_SET            0x200
#define GIC_DIST_PENDING_CLEAR          0x280
#define GIC_DIST_ACTIVE_BIT             0x300
#define GIC_DIST_PRI                    0x400
#define GIC_DIST_TARGET                 0x800
#define GIC_DIST_CONFIG                 0xC00
#define GIC_DIST_SOFTINT                0xF00

/* GIC CPU interface registers. */
#define GIC_CPU_CTRL                    0x00
#define GIC_CPU_PRIMASK                 0x04
#define GIC_CPU_BINPOINT                0x08
#define GIC_CPU_INTACK                  0x0C
#define GIC_CPU_EOI                     0x10
#define GIC_CPU_RUNNINGPRI              0x14
#define GIC_CPU_HIGHPRI                 0x18

/* Interrupt space layout. */
#define GIC_PPI_START                   16
#define GIC_SPI_START                   32
#define GIC_INT_SPURIOUS                1023

/* Non-secure physical timer (PPI 3). */
#define INT_MSM8916_TIMER               (GIC_PPI_START + 3)

/* BLSP1 UART1 (GIC SPI 107). */
#define INT_MSM8916_UART1               (GIC_SPI_START + 107)

#define NR_MSM8916_IRQS                 256
#define NR_MSM8916_IRQS_TOTAL           (NR_MSM8916_IRQS + 32)

/* Architected (generic) timer control bits. */
#define MSM8916_CNTP_CTL_ENABLE         (1 << 0)
#define MSM8916_CNTP_CTL_IMASK          (1 << 1)
#define MSM8916_CNTP_CTL_ISTATUS        (1 << 2)

/* Nominal architected timer frequency, used if CNTFRQ reads back as zero. */
#define MSM8916_TIMEBASE_FREQ_HZ        19200000

#endif /* !_PEXPERT_MSM8916_H_ */
