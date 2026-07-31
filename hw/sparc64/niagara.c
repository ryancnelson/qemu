/*
 * QEMU Sun4v/Niagara System Emulator
 *
 * Copyright (c) 2016 Artyom Tarasenko
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int-common.h"
#include "qemu/units.h"
#include "cpu.h"
#include "hw/boards.h"
#if 1
#include "hw/char/serial-mm.h"
#else
#include "hw/char/serial.h"
#endif
#include "hw/misc/unimp.h"
#include "hw/loader.h"
#include "hw/sparc/sparc64.h"
#include "hw/rtc/sun4v-rtc.h"
#include "system/block-backend.h"
#include "qemu/error-report.h"
#include "system/qtest.h"
#include "system/system.h"
#include "qapi/error.h"

#if 1
#include "hw/sparc64/dklabel.h"
#endif

#if 0
#define  NEW_VDISK
typedef struct NiagaraBoardState {
    MemoryRegion hv_ram;
    MemoryRegion nvram;
    MemoryRegion md_rom;
    MemoryRegion hv_rom;
#ifdef NEW_VDISK
    MemoryRegion vdisk_mapped[NUM_VDISK][NDLMAP];
    struct disk_part {
        long    offset;
        long    size;
        void   *addr;
   } disk_part[NUM_VDISK][NDLMAP];
#endif
    MemoryRegion vdisk_ram;
    MemoryRegion prom;
} NiagaraBoardState;
#else
#include "hw/sparc64/niagara_board.h"
#endif /* 0 */

#define	BE32(x)	(((x) & 0xff000000UL) >> 24 | ((x) & 0x00ff0000UL) >> 8 | \
		((x) & 0x0000ff00UL) << 8 | ((x) & 0x000000ffUL) << 24)
#define	BE64(x)	(BE32(x >> 32ULL) | (BE32((x) & 0x0FFFFFFFFULL)) << 32ULL)
#if 0
typedef struct NiagaraBoardState {
    MemoryRegion hv_ram;
    MemoryRegion nvram;
    MemoryRegion md_rom;
    MemoryRegion hv_rom;
    MemoryRegion vdisk_ram;
    MemoryRegion prom;
} NiagaraBoardState;
#endif /* 0 */

#define NIAGARA_HV_RAM_BASE 0x400000ULL /* 0x100000ULL */
#define NIAGARA_HV_RAM_SIZE 0x3c00000ULL /* 60 MiB */

#define NIAGARA_PARTITION_RAM_BASE 0x80000000ULL

#define NIAGARA_UART_BASE   0x1f10000000ULL
#if 1
#define NIAGARA_FPGA_UART_BASE   0xfff0c2c000ULL
#endif

#define NIAGARA_NVRAM_BASE  0x1f11000000ULL
#define NIAGARA_NVRAM_SIZE  0x2000

#define NIAGARA_MD_ROM_BASE 0x1f12000000ULL
//#define NIAGARA_MD_ROM_SIZE 0x2000
#define NIAGARA_MD_ROM_SIZE 0x4000

#define NIAGARA_HV_ROM_BASE 0x1f12080000ULL
#define NIAGARA_HV_ROM_SIZE 0x2000

#define	NIAGARA_CLOCKBASE   0x9600000000ULL
#define	NIAGARA_CLOCKSIZE   0x0100000000ULL
#define	NIAGARA_MEMORYBANK  0x9700000000ULL
#define	NIAGARA_MEMORYBANKSIZE   0x0100000000ULL
#define NIAGARA_IOBBASE     0x9800000000ULL
#define NIAGARA_IOBSIZE     0x0100000000ULL

#define NIAGARA_JBIBASE     0x8000000000ULL
#define NIAGARA_JBISIZE     0x0100000000ULL

#define NIAGARA_JBUSBASE    0x9f00000000ULL
#define NIAGARA_JBUSSIZE    0x0100000000ULL

#define NIAGARA_L2CBANK     0xa000000000ULL
#define NIAGARA_L2CBSIZE    0x1f00000000ULL

#define NIAGARA_SSIBASE     0xff00000000ULL
#define NIAGARA_SSISIZE     0x0010000000ULL

#define NIAGARA_VDISK_BASE  0x1f40000000ULL
//#define NIAGARA_RTC_BASE    0xfff0c1fff8ULL
#define NIAGARA_RTC_BASE    0xfff0d22000ULL

/* Firmware layout
 *
 * |------------------|
 * |   openboot.bin   |
 * |------------------| PROM_ADDR + OBP_OFFSET
 * |      q.bin       |
 * |------------------| PROM_ADDR + Q_OFFSET
 * |     reset.bin    |
 * |------------------| PROM_ADDR
 */
#define NIAGARA_PROM_BASE   0xfff0000000ULL
#define NIAGARA_Q_OFFSET    0x10000ULL
#define NIAGARA_OBP_OFFSET  0x80000ULL
#define PROM_SIZE_MAX       (4 * MiB)

static void add_rom_or_fail(const char *file, const hwaddr addr)
{
    /* XXX remove qtest_enabled() check once firmware files are
     * in the qemu tree
     */
    if (!qtest_enabled() && rom_add_file_fixed(file, addr, -1)) {
        error_report("Unable to load a firmware for -M niagara");
        exit(1);
    }

}

#ifdef NEW_VDISK
/*
 *  * Construct checksum for the new disk label
 *
 */
#define	CK_MAKESUM	1
static short get_checksum(struct dk_label *dk_label, int mode);
static short
get_checksum(struct dk_label *dk_label, int mode)
{
        short sum, *sp;
        int i;

        sum = 0;
        sp = (short *)dk_label;
        i = sizeof (*dk_label) / sizeof (*sp);

        /*
         * If we are generating a checksum, don't include the checksum
         * in the rolling xor.
         */
        if (mode == CK_MAKESUM)
                i -= 1;

        /*
         * Take the xor of all the half-words in the label.
         */
        while (i--) {
#if 0
            sum ^= *sp++;
#else
            uint16_t tmp16;

            tmp16 = *sp++;
            sum ^= be16toh(tmp16);

#endif
        }

        return (sum);
}

#define	VDISK_BUS_ID	0
#define	VDISK_UNIT_BASE	100

static void
niagara_load_vdisk(NiagaraBoardState *s, long vdisk_base)
{
        int	i;
	int	unit;
        long	offset;
	uint16_t	tmp16;
	uint32_t	tmp32;
	struct dk_label *sdlp;
        DriveInfo *dinfo;
	int	nhead = 4;
	int	nsect = 256;
	int	spc;
	struct disk_part	*dp;

	spc = nhead * nsect;

	s -> use_new_vdisk = 1;
	bzero(&s->disk_unit, sizeof (s->disk_unit));

	for (unit = 0; unit < NUM_VDISK; unit++) {
		int		have_disk;
		BlockBackend	*blk;
		long		size;
#ifndef  NEW_VDISK_MMAP
		char	work512b[512];
#endif
		/*
		 * initialize disk units and their partition tables
		 */

        	offset = 0;

		dp = &s->disk_unit[unit].disk_part[0];
		for (int j = 0; j < NUM_PART; j++, dp++) {
			dp->offset = 0;
			dp->size = 0;


			/* initialize block back */
#if defined(NEW_VDISK_MMAP)
			dp->be.map.addr = NULL;
#elif defined(NEW_VDISK_BLKDEV)
			dp->be.bb = NULL;
#else /* fd */
			dp->be.fd = -1;
#endif
			/* set back pointer to struct disk_unit */
			dp->dup = &s->disk_unit[unit];
		}
#if NUM_PART > 1
		dinfo = drive_get(IF_NONE,
		    VDISK_BUS_ID, 2 + NUM_PART*unit + VDISK_UNIT_BASE);
#else
		dinfo = drive_get(IF_NONE,
		    VDISK_BUS_ID, unit + VDISK_UNIT_BASE);
#endif
		if (dinfo != NULL) {

			/* we have slice2, just use this only  */

			dp = &s->disk_unit[unit].disk_part[0];
			error_report("%s: unit:%d slice2 found\n",
			    __func__, unit);

			blk = blk_by_legacy_dinfo(dinfo);
			size = blk_getlength(blk);

			error_report("%s: unit:%d slice2 size:%ld\n",
			    __func__, unit, size);

			s->disk_unit[unit].dtyp =
			    bdrv_is_read_only(blk_bs(blk))
			    ? 5 /* RODIRECT */ : 0 /* DIRECT */;

			/* make vdisk partition */
			dp->offset = 0;
			dp->size   = size;

			/* setup block back */
#if defined(NEW_VDISK_MMAP)
			dp->be.map.addr =
			    qemu_ram_get_host_addr(dp->be.map.ram_block);
#elif defined(NEW_VDISK_BLKDEV)
			dp->be.bb = blk;
{
			Error	*error_abort = NULL;
			int	ret;

			ret = blk_set_perm(dp->be.bb,
			    bdrv_is_read_only(blk_bs(blk))
			      ? BLK_PERM_CONSISTENT_READ
			      : BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
			    BLK_PERM_ALL,
			    &error_abort);
			if (ret < 0) {
				error_report("%s: blk_set_perm failed\n", __func__);
				return;
			}
}
#else
			dp->be.fd = open(blk_bs(blk)->filename,
			    bdrv_is_read_only(blk_bs(blk)) ? O_RDONLY : O_RDWR);

			if (dp->be.fd < 0) {
				error_report("%s: open failed %s (%d)\n",
				    __func__, strerror(errno), errno);
			}
#endif
			/* ignore other slices of the disk unit */
			continue;
		}

		/* initialize disk label */
		dp = s->disk_unit[unit].disk_part;
		sdlp =
#if defined(NEW_VDISK_MMAP)
		    (struct dk_label *)qemu_ram_get_host_addr(
		    dp->be.map.vdisk_mapped.ram_block);
#else
		    (struct dk_label *)work512b;

		bzero(work512b, 512);
		sdlp->dkl_asciilabel[0] = 0;    /* XXX */
		sdlp->dkl_pcyl = htobe16(0);    /* XXX */
		sdlp->dkl_ncyl = htobe16(0);    /* XXX */
		sdlp->dkl_acyl = htobe16(0);    /* XXX */
		sdlp->dkl_nhead = htobe16(0);   /* XXX */
		sdlp->dkl_nsect = htobe16(0);   /* XXX */
#endif

		for (int j = 0; j < NUM_PART; j++) {
			sdlp->dkl_map[j].dkl_cylno = htobe32(0);
			sdlp->dkl_map[j].dkl_nblk = htobe32(0);
		}

		have_disk = 0;

		/*
		 * Open Block Back for this unit
		 */
		for (i = 0; i < NUM_PART; i++) {
			dp = &s->disk_unit[unit].disk_part[i];

			if (i == 2) {
				/*
				 * we generate slice2 automatically,
				 * so ignore slice 2.
				 */
				continue;
			}
			dinfo = drive_get(IF_NONE,
			    VDISK_BUS_ID, i + NUM_PART*unit + VDISK_UNIT_BASE);
			if (dinfo == NULL) {
				continue;
			}

			blk = blk_by_legacy_dinfo(dinfo);
		 	size = blk_getlength(blk);
			if (size <= 0) {
				/* no file */
				continue;
			}

			have_disk = 1;

			if (i == 6) {
				/* slice f (6) is default partition (sun4v) of
				 * install CD/DVD */
				dinfo->is_default = 1;
			}

			/* add this partition to vdisk partition table */
			dp->offset = offset;
			dp->size   = size;

			/* add this partition to sun disk label */
			tmp32 = offset / 512 / spc;
			sdlp->dkl_map[i].dkl_cylno = htobe32(tmp32);
			tmp32 = size / 512;
			sdlp->dkl_map[i].dkl_nblk = htobe32(tmp32);

			error_report(
			    "blk_get_length(%s) is 0x%lx, "
			    "real_host_page_size is 0x%x, readonly is %d\n",
			    blk_bs(blk)->filename, size,
			    (unsigned)qemu_real_host_page_size(),
			    bdrv_is_read_only(blk_bs(blk)));

			/* prepare block back */
#if defined(NEW_VDISK_MMAP)
			/* map it into "sun4v_vdisk.ram" space */
			memory_region_init_ram_from_file(
			    &dp->be.map.vdisk_mapped,
			    NULL,
			    "sun4v_vdisk.ram",
			    size,
			    0x1000 /* 4K align */,
			    bdrv_is_read_only(blk_bs(blk)) ? 0 : RAM_SHARED,
			    blk_bs(blk)->filename,
			    &error_fatal);

			/* NOTE:*/
			error_report("mapped address of %s is 0x%lx +0x%lx\n",
			    blk_bs(blk)->filename,
			    (long)qemu_ram_get_host_addr(
			        dp->be.map.vdisk_mapped.ram_block),
			    size);

			if (error_fatal ||
			    dp->be.map.vdisk_mapped.ram_block == NULL) {
				error_report("could not load ram disk '%s[%d]'",
				    blk_bs(blk)->filename, i);
				exit(1);
			}
#   ifndef NEW_VDISK_LARGE
			/* XXX - ? */
			memory_region_add_subregion(
			    get_system_memory(),
			    vdisk_base + offset,
			    &dp->be.map.vdisk_mapped);
#   endif

			dp->be.map.addr = qemu_ram_get_host_addr(
			    dp->be.map.vdisk_mapped.ram_block));

#elif defined(NEW_VDISK_BLKDEV)
			/* use found blk for block back */
			dp->be.bb = blk;
#else /* fd */
			/* open file */
			dp->be.fd = open(blk_bs(blk)->filename,
			    bdrv_is_read_only(blk_bs(blk)) ? O_RDONLY : O_RDWR);

			if (dp->be.fd < 0) {
				error_report("%s: open failed %s (%d)\n",
				    __func__, strerror(errno), errno);
			}
#endif
			/* increase offset, rount up to next cylinder */
			offset += ((size + spc*512 - 1) / (spc*512)) * spc*512;
		}

		/* initialize the rest of disk label */
		sdlp->dkl_magic = htobe16(DKL_MAGIC);

		/* make slice 2 of sun disk label */
		sdlp->dkl_map[2].dkl_cylno = htobe32(0);
		tmp32 = offset/512;
		sdlp->dkl_map[2].dkl_nblk = htobe32(tmp32);

		sdlp->dkl_ncyl = htobe16(tmp32/spc);    /* XXX */
		sdlp->dkl_acyl = htobe16(0);    /* XXX */
		sdlp->dkl_nhead = htobe16(nhead);   /* XXX */
		sdlp->dkl_nsect = htobe16(nsect);   /* XXX */

		/* fix check sum */
		tmp16 = get_checksum(sdlp, CK_MAKESUM);
		sdlp->dkl_cksum = htobe16(tmp16);

		if (have_disk == 0) {
			continue;
		}

		/* dump sun disk label */
		error_report("DUMP virtual disk%d label", unit);
		error_report("ascii label'%s'", sdlp->dkl_asciilabel);
		error_report("magic 0x%x, cksum 0x%x",
		    be16toh(sdlp->dkl_magic), be16toh(sdlp->dkl_cksum));
		error_report(" pcyl %d, ncyl %d, acyl %d, nhead %d, nsect %d",
		    be16toh(sdlp->dkl_pcyl), be16toh(sdlp->dkl_ncyl),
		    be16toh(sdlp->dkl_acyl),
		    be16toh(sdlp->dkl_nhead), be16toh(sdlp->dkl_nsect));

		error_report("  slice   cylno     nblk");
		for (i = 0; i < NUM_PART; i++) {
			error_report(" %7d %7d %7d",
			    i, be32toh(sdlp->dkl_map[i].dkl_cylno),
			    be32toh(sdlp->dkl_map[i].dkl_nblk));
		}

		/* write the disk label to the head of slice0 */
		/* XXX shoud write to slice0 always? */
		dp = &s->disk_unit[unit].disk_part[0];
#ifdef  NEW_VDISK_MMAP
		/* do thing */
#elif defined(NEW_VDISK_BLKDEV)
		if (blk_pwrite(dp->be.bb, 0, 512, sdlp, 0) < 0) {
 			error_report("blk_pwrite failed");
		}
#else /* file descriptor */
 		if (pwrite(dp->be.fd, sdlp, 512, 0) != 512) {
 			error_report("pwrite failed");
 		}
#endif
	}
}
NiagaraBoardState *NiagaraBoardStatePtr;
#endif /* NEW_VDISK */

#define CONFIG_IOB

#ifdef CONFIG_IOB
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/rtc/sun4v-rtc.h"
#include "trace.h"
#include "qom/object.h"

#ifdef CONFIG_CLOCK
/*=== CLOCK ===*/
#define TYPE_SUN4V_CLOCK "sun4v_clock"
OBJECT_DECLARE_SIMPLE_TYPE(Sun4vClock, SUN4V_CLOCK)

struct Sun4vClock {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

static uint64_t sun4v_clock_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    uint64_t val;

    val = 0;
    //trace_sun4v_clock_read(addr, val);
    return val;
}

static void sun4v_clock_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    //trace_sun4v_clock_write(addr, val);
}

static const MemoryRegionOps sun4v_clock_ops = {
    .read = sun4v_clock_read,
    .write = sun4v_clock_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sun4v_clock_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_SUN4V_CLOCK);
    s = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
}

static void sun4v_clock_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Sun4vClock *s = SUN4V_CLOCK(dev);
    memory_region_init_io(&s->iomem, OBJECT(s), &sun4v_clock_ops, s,
                          "sun4v-clock", NIAGARA_CLOCKSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sun4v_clock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sun4v_clock_realize;
}

static const TypeInfo sun4v_clock_info = {
    .name          = TYPE_SUN4V_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Sun4vClock),
    .class_init    = sun4v_clock_class_init,
};

static void sun4v_clock_register_types(void)
{
    type_register_static(&sun4v_clock_info);
}

type_init(sun4v_clock_register_types)
/* end of CLOCK */
#endif

/* start of IOB */
#define TYPE_SUN4V_IOB "sun4v_iob"
OBJECT_DECLARE_SIMPLE_TYPE(Sun4vIob, SUN4V_IOB)

struct Sun4vIob {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

static uint64_t sun4v_iob_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    uint64_t val;

    val = 0;
    //trace_sun4v_iob_read(addr, val);
    return val;
}

static void sun4v_iob_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
#if 0
    uint_t	cpu;
    switch (addr) {
    case 0x0000:
	/* INT_MAN register */
	cpu = (val >> 8) & 0x1f 
	vec = val & 0x3f;
	break;

   case 0x0400:
	/* INT_CTL register */
#endif
    //trace_sun4v_iob_write(addr, val);
}

static const MemoryRegionOps sun4v_iob_ops = {
    .read = sun4v_iob_read,
    .write = sun4v_iob_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sun4v_iob_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_SUN4V_IOB);
    s = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
}

static void sun4v_iob_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Sun4vIob *s = SUN4V_IOB(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &sun4v_iob_ops, s,
                          "sun4v-iob", NIAGARA_IOBSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sun4v_iob_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sun4v_iob_realize;
}

static const TypeInfo sun4v_iob_info = {
    .name          = TYPE_SUN4V_IOB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Sun4vIob),
    .class_init    = sun4v_iob_class_init,
};

static void sun4v_iob_register_types(void)
{
    type_register_static(&sun4v_iob_info);
}

type_init(sun4v_iob_register_types)
#endif /* CONFIG_IOB */

/* Niagara hardware initialisation */
static void niagara_init(MachineState *machine)
{
    NiagaraBoardState *s = g_new(NiagaraBoardState, 1);
#ifdef NEW_VDISK
    DriveInfo *dinfo_vdisk = drive_get(IF_NONE, VDISK_BUS_ID, VDISK_UNIT_BASE);
#endif
    DriveInfo *dinfo = /*drive_get_next(IF_PFLASH);*/ drive_get(IF_PFLASH, 0, 0);
    MemoryRegion *sysmem = get_system_memory();
#ifdef NEW_VDISK
    NiagaraBoardStatePtr = s;
#if NUM_PART > 1
    if (dinfo_vdisk == NULL) {
        dinfo_vdisk = drive_get(IF_NONE, VDISK_BUS_ID, VDISK_UNIT_BASE + 2);
    }
#endif /* NUM_PART */
#endif /* NEW_VDISK */
    /* init CPUs */
#if 0
    s->cpu[0] = sparc64_cpu_devinit(machine->cpu_type, NIAGARA_PROM_BASE);
#else
    for (int i = 0; i < machine->smp.cpus; i++) {
        s->cpu[i] = sparc64_cpu_devinit(machine->cpu_type, NIAGARA_PROM_BASE);
    }
#endif
    /* set up devices */
    memory_region_init_ram(&s->hv_ram, NULL, "sun4v-hv.ram",
                           NIAGARA_HV_RAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_HV_RAM_BASE, &s->hv_ram);

    memory_region_add_subregion(sysmem, NIAGARA_PARTITION_RAM_BASE,
                                machine->ram);

    memory_region_init_ram(&s->nvram, NULL, "sun4v.nvram", NIAGARA_NVRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_NVRAM_BASE, &s->nvram);
    memory_region_init_ram(&s->md_rom, NULL, "sun4v-md.rom",
                           NIAGARA_MD_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_MD_ROM_BASE, &s->md_rom);
    memory_region_init_ram(&s->hv_rom, NULL, "sun4v-hv.rom",
                           NIAGARA_HV_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_HV_ROM_BASE, &s->hv_rom);
    memory_region_init_ram(&s->prom, NULL, "sun4v.prom", PROM_SIZE_MAX,
                           &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_PROM_BASE, &s->prom);

    add_rom_or_fail("nvram1", NIAGARA_NVRAM_BASE);
#if 0
    add_rom_or_fail("1up-md.bin", NIAGARA_MD_ROM_BASE);
    add_rom_or_fail("1up-hv.bin", NIAGARA_HV_ROM_BASE);
#else
    add_rom_or_fail("md.bin", NIAGARA_MD_ROM_BASE);
    add_rom_or_fail("hv.bin", NIAGARA_HV_ROM_BASE);
#endif

    add_rom_or_fail("reset.bin", NIAGARA_PROM_BASE);
    add_rom_or_fail("q.bin", NIAGARA_PROM_BASE + NIAGARA_Q_OFFSET);
    add_rom_or_fail("openboot.bin", NIAGARA_PROM_BASE + NIAGARA_OBP_OFFSET);

#ifdef NEW_VDISK
    if (dinfo_vdisk) {
        niagara_load_vdisk(s, NIAGARA_VDISK_BASE);
    } else
#endif
    /* the virtual ramdisk is kind of initrd, but it resides
       outside of the partition RAM */
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
#ifdef NEW_VDISK
        long size = blk_getlength(blk);
#else
        int size = blk_getlength(blk);
#endif
        if (size > 0) {
            memory_region_init_ram(&s->vdisk_ram, NULL, "sun4v_vdisk.ram", size,
                                   &error_fatal);
            memory_region_add_subregion(get_system_memory(),
                                        NIAGARA_VDISK_BASE, &s->vdisk_ram);
            dinfo->is_default = 1;
            rom_add_file_fixed(blk_bs(blk)->filename, NIAGARA_VDISK_BASE, -1);
        } else {
            error_report("could not load ram disk '%s'",
                         blk_bs(blk)->filename);
            exit(1);
        }
    }
//FOR_ZEUS
    {
	uint64_t *p;
	p = rom_ptr(NIAGARA_PROM_BASE + 0x100, 64);
        if (p[0] == 0) {
	    p[0] = BE64(4*1024*1024ULL);	/* membase */
	    p[1] = BE64(60 * 1024 * 1024ULL);	/* memsize */
	    p[2] = BE64(0x1f12080000ULL);	/* HV PD */
	    p[3] = BE64(0xfff0010000ULL);	/* HV addr */
        }
    }
#if 0
    serial_mm_init(sysmem, NIAGARA_UART_BASE, 0, NULL,
                   115200, serial_hd(0), DEVICE_BIG_ENDIAN);
#else
    serial_mm_init(sysmem, NIAGARA_FPGA_UART_BASE, 0, NULL,
                   115200, serial_hd(0), DEVICE_BIG_ENDIAN); /* hypervisor serial */

    serial_mm_init(sysmem, NIAGARA_UART_BASE, 0, NULL,
                   115200, serial_hd(1), DEVICE_BIG_ENDIAN); /* ttya */

//    serial_mm_init(sysmem, NIAGARA_UART_BASE + 0x2000, 0, NULL,
//                   115200, serial_hd(2), DEVICE_BIG_ENDIAN);
#endif
#if 1
    create_unimplemented_device("sun4v-clock", NIAGARA_CLOCKBASE, NIAGARA_CLOCKSIZE);
#endif
#if 0
    create_unimplemented_device("sun4v-iob", NIAGARA_IOBBASE, NIAGARA_IOBSIZE);
#else
    sun4v_iob_init(NIAGARA_IOBBASE);
#endif
#if 0
    create_unimplemented_device("sun4v-memorybank", NIAGARA_MEMORYBANK, NIAGARA_MEMORYBANKSIZE);
#endif
    create_unimplemented_device("sun4v-jbi", NIAGARA_JBIBASE, NIAGARA_JBISIZE);
    create_unimplemented_device("sun4v-jbus", NIAGARA_JBUSBASE, NIAGARA_JBUSSIZE);
    create_unimplemented_device("sun4v-l2cbank", NIAGARA_L2CBANK, NIAGARA_L2CBSIZE);
    create_unimplemented_device("sun4v-ssi", NIAGARA_SSIBASE, NIAGARA_SSISIZE);
    sun4v_rtc_init(NIAGARA_RTC_BASE);
}

static void niagara_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform, Niagara";
    mc->init = niagara_init;
    mc->max_cpus = 32 /* 1 */; /* XXX for now */
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Sun-UltraSparc-T1");
    mc->default_ram_id = "sun4v-partition.ram";
}

static const TypeInfo niagara_type = {
    .name = MACHINE_TYPE_NAME("niagara"),
    .parent = TYPE_MACHINE,
    .class_init = niagara_class_init,
};

static void niagara_register_types(void)
{
    type_register_static(&niagara_type);
}

type_init(niagara_register_types)
