#include "hw/sparc64/dklabel.h"

#define  NEW_VDISK
//#define  NEW_VDISK_LARGE
//#define  NEW_VDISK_MMAP
//#define NEW_VDISK_BLKDEV
//#define NEW_VDISK_BLKDEV_AIO
//#define NEW_VDISK_BLKDEV_ACCT
//#define	MAX_VDISK_BITS	32
//#define	MAX_VDISK	(1U << (MAX_VDISK_BITS))
#define	NUM_VDISK	8

#ifdef  NEW_VDISK_LARGE
#define	NUM_PART	NDKMAP
#else
#define	NUM_PART	1
#endif /* NEW_VDISK_LARGE */

#define	MAX_CPUS_N2	64
typedef struct NiagaraBoardState {
    MemoryRegion hv_ram;
    MemoryRegion nvram;
    MemoryRegion md_rom;
    MemoryRegion hv_rom;

#ifdef NEW_VDISK
    int use_new_vdisk;

    struct disk_unit {
	/* attribute */
	uint32_t	dtyp;	/* scsi dtype: DIRECT(disk):0, RODIRECT(cdrom):5  */

	/* io management */
	uint32_t	status;
#define	D_BUSY 	0x0001
#define	D_ASYNC 0x0002
#define	D_ERROR 0x0004

	/* scatter and gather */
#define	VDISK_NIOV	((1024*1024/8192) + 1)	/* SunOS maxphys 1024k */
	MemoryRegionSection	mrs[VDISK_NIOV]; /* work info i/o buffer */
	QEMUIOVector	qiov;		/* for qemu_aio_ pwritev/preadv */
	BlockAcctCookie	acct;		/* for stat */

#ifdef NEW_VDISK_BLKDEV
	BlockAIOCB	*aiocb;	/* active aio context */
#endif
	/* complex partition backing end */
        struct disk_part {
            uint64_t    offset;		/* in bytes */
            uint64_t    size;		/* in bytes */
            union {
#ifdef  NEW_VDISK_MMAP
		struct {
		    MemoryRegionSection	mrs;
                    MemoryRegion    vdisk_mapped;
                    void	    *addr;	/* host address space */
		} map;
#endif
#ifdef NEW_VDISK_BLKDEV
	        BlockBackend	*bb;
#endif
	        int	fd;	/* file descriptor of backend file */
            } be;
	    struct disk_unit *dup; /* back pointer for disk_unit */
        } disk_part[NUM_PART];
    } disk_unit[NUM_VDISK];
#endif /* NEW_VDISK */
    /* original member */
    MemoryRegion vdisk_ram;
    MemoryRegion prom;
    SPARCCPU	*cpu[MAX_CPUS_N2];
} NiagaraBoardState;

extern NiagaraBoardState *NiagaraBoardStatePtr;
