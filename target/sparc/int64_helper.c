/*
 * Sparc64 interrupt helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/helper-proto.h"
#include "exec/log.h"
#if 0
#include "exec/exec-all.h"
#endif
#if 1
#include "exec/translation-block.h"
#include "hw/boards.h"
#include "system/block-backend.h"
#endif
#include "trace.h"
#include "qemu/plugin.h"

#ifdef NEW_VDISK
//#define       SUPPORT_ASYNC
//#define SUPPORT_IOV
#define USE_BLK_DRAIN
#endif

#define DEBUG_PCALL

#ifdef DEBUG_PCALL
static const char * const excp_names[0x80] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_TMISS] = "Instruction Access MMU Miss",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_TOVF] = "Tag Overflow",
    [TT_CLRWIN] = "Clean Windows",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_DFAULT] = "Data Access Fault",
    [TT_DMISS] = "Data Access MMU Miss",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DPROT] = "Data Protection Error",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_PRIV_ACT] = "Privileged Action",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
#if 1 /* BUG fix sunv4 */
    [TT_IVEC] = "vector interrupt",
#endif
};
#endif


#define COMPAT_LEGION
#ifdef COMPAT_LEGION
#define	LEGION_TRAP_SETDBG	0x170
#define	LEGION_TRAP_SIMEXIT	0x171
#define	LEGION_TRAP_GOT_HERE	0x172
#define	LEGION_TRAP_LOG_DUMP	0x174
#define	LEGION_TRAP_PABCOPY	0x175
#define	LEGION_TRAP_INST_CNT	0x176
#define	LEGION_TRAP_ALLDBG	0x177
#define	LEGION_TRAP_NO_DBG	0x178
#define	LEGION_TRAP_SAVE_STATE	0x179

#define	LEGION_TRAP_START	0x170
#define	LEGION_TRAP_END	0x179
#define	NUM_LEGION_TRAPS	(LEGION_TRAP_END - LEGION_TRAP_START + 1)
static const char *const legion_trap_names[NUM_LEGION_TRAPS] = {
[LEGION_TRAP_SETDBG - LEGION_TRAP_START] = 	"SETDBG",
[LEGION_TRAP_SIMEXIT - LEGION_TRAP_START] = 	"SIMEXIT",
[LEGION_TRAP_GOT_HERE - LEGION_TRAP_START] = 	"GOT_HERE",
[LEGION_TRAP_LOG_DUMP - LEGION_TRAP_START] = 	"LOG_DUMP",
[LEGION_TRAP_PABCOPY - LEGION_TRAP_START] = 	"PABCOPY",
[LEGION_TRAP_INST_CNT - LEGION_TRAP_START] = 	"INT_CNT",
[LEGION_TRAP_ALLDBG - LEGION_TRAP_START] = 	"ALLDBG",
[LEGION_TRAP_NO_DBG - LEGION_TRAP_START] = 	"NO_DBG",
[LEGION_TRAP_SAVE_STATE - LEGION_TRAP_START] =	"SAVE_STATE",
};

#include "hw/sparc64/niagara_board.h"
#ifdef NEW_VDISK_BLKDEV
#  include "block/coroutines.h"
#  include "block/block-common.h"
#  include "exec/translation-block.h"
#endif
#define DEBUG_PABCOPY
static bool
sparc_legion_trap(CPUState *cs)
{
    SPARCCPU	*cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    int		intno = cs->exception_index;

    if (intno <LEGION_TRAP_START || intno > LEGION_TRAP_END) {
        /* do nothing */
	return (FALSE);
    }

    switch (intno) {
    case LEGION_TRAP_PABCOPY:
#ifdef DEBUG_PABCOPY
        qemu_log("%s tt:0x%x, %s, src: 0x%" PRIx64 " dst: 0x%" PRIx64 "len: 0x%" PRIx64 "\n",
            __func__, intno,
            legion_trap_names[intno - LEGION_TRAP_START],
            env->gregs[1],
            env->gregs[2],
            env->gregs[3]
           );
#endif /* DEBUG_PABCOPY */
        env->gregs[4] = -1;	/* means pbcopy failed */
        break;
    default:
        qemu_log("%s tt:0x%x %s\n",
            __func__, intno,
            legion_trap_names[intno - LEGION_TRAP_START]);
    }
    return (TRUE);
}
#endif /* COMPAT_LEGION */

#ifdef NEW_VDISK
#define	FAST_TRAP	0x80
#define	DISK_READ	0xf0	/* in o5 */
#define	DISK_WRITE	0xf1	/* in o5 */

#define	HCALL_DISKIO_DIR_MASK 8
#define	HCALL_DISKIO_UNIT_MASK 56
#define	HCALL_DISKIO_UNIT_SHIFT 8

#define	HSIMD_CMD_DATAIO	0
#define	HSIMD_CMD_READCAPACITY	1
#define	HSIMD_CMD_DATAIO_ASYNC	2
#define	HSIMD_CMD_GET_CAP	3
#define		HSIMD_CAP_ASYNC	0x0001
#define		HSIMD_CAP_CDROM	0x0002
#define		HSIMD_CAP_IOV	0x0004
#define	HSIMD_CMD_GET_IOSTATUS	4
#define		HSIMD_IOSTATUS_OK	0
#define		HSIMD_IOSTATUS_NOAIO	1
#define		HSIMD_IOSTATUS_ERR	2
#define		HSIMD_IOSTATUS_BUSY	3
#define	HSIMD_CMD_IOV		5
#define	HSIMD_CMD_IOV_ASYNC	6
#define	HSIMD_CMD_SHIFT		60
#define	HSIMD_CMD_MASK		0xf
#define	HSIMD_UNIT_SHIFT	32
#define	HSIMD_UNIT_MASK		0x0fffffffULL
#define	HSIMD_SIZE_SHIFT	0
#define	HSIMD_SIZE_MASK		0xffffffffULL

#include "system/address-spaces.h"

//#define DEBUG_VDISK

#ifdef NEW_VDISK_BLKDEV
static void
vdisk_rw_done_cb(void *opaque, int ret)
{
    int	i;
    struct disk_part *dp = opaque;
    struct disk_unit *du = dp->dup;

#ifdef DEBUG_VDISK
    qemu_log("%s: called, ret:%d status:0x%x\n", __func__, ret, du->status);
#endif
    if (ret < 0) {
	qemu_log("%s: called, ret:%d\n", __func__, ret);
    }
    g_assert(du->aiocb != NULL);
    du->aiocb = NULL;

#ifdef NEW_VDISK_BLKDEV_ACCT
    //aio_context_acquire(blk_get_aio_context(dp->be.bb));
    if (ret < 0) {
	block_acct_failed(blk_get_stats(dp->be.bb), &du->acct);
    } else {
        block_acct_done(blk_get_stats(dp->be.bb), &du->acct);
    }
    /* free resource */
    //aio_context_release(blk_get_aio_context(dp->be.bb));
#endif /* BLK_ACOUNTING */

    if (du->status & D_ASYNC) {
	/* free allocated rsources for this I/O */
	for (i = 0; i < du->qiov.niov; i++) {
	    if (du->mrs[i].mr) {
		memory_region_unref(du->mrs[i].mr);
	        du->mrs[i].mr = NULL;
	    }
	}
	if (du->qiov.nalloc) {
	    qemu_iovec_destroy(&du->qiov);
	    du->qiov.nalloc = 0;
	}
	du->qiov.niov = 0;
	/* NB: don't reset D_ASYNC flag */
    }

    /* update status */
    if (ret < 0) {
	du->status |= D_ERROR;
    }
    /* XXX - should be atomic */
    du->status &= ~D_BUSY;

    if ((du->status & D_ASYNC) == 0) {
	/* sync call */
	//aio_wait_kick();
    } else {
	//NiagaraBoardStatePtr->cpu[0]->env.softint |= 1U << 7;
    }
#ifdef DEBUG_VDISK
    qemu_log("%s: end, status:0x%x\n", __func__, du->status);
#endif
}
#endif /* NEW_VDISK_BLKDEV */

static bool
sparc_vdisk_trap(CPUState *cs)
{
    SPARCCPU		*cpu = SPARC_CPU(cs);
    CPUSPARCState	*env = &cpu->env;
    int		intno = cs->exception_index;
    struct disk_unit	*du;
    struct disk_part	*dp;
    //MemoryRegionSection mrs;
    MemoryRegionSection	iov_mrs;
    void	*host_addr;
    hwaddr	addr;
    uint64_t	size;
    uint64_t	offset;
    uint64_t	unit;
    uint64_t	cmd;
    int		i;
    int		err;
#ifdef NEW_VDISK_BLKDEV
    uint64_t	iov;
    struct iovec	*iov_haddr = NULL;
#else
    uint64_t	reqsize;
#endif
    int		niov;

    niov = 0;			/* sanity */
    iov_mrs.mr = NULL;
    err = 1;

    if (!(intno == (FAST_TRAP + 0x100) &&
        (env->regwptr[5] == DISK_READ || env->regwptr[5] == DISK_WRITE))) {
        /* do nothing */
	return (FALSE);
    }

    if (!NiagaraBoardStatePtr->use_new_vdisk) {
	return (FALSE);
    }

#ifdef DEBUG_VDISK
    qemu_log("%s tt:0x%x, 0x%" PRIx64 ",  0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64 ",  0x%" PRIx64 ", 0x%" PRIx64 "\n",
            __func__, intno,
            env->regwptr[0],
            env->regwptr[1],
            env->regwptr[2],
            env->regwptr[3],
            env->regwptr[4],
            env->regwptr[5]);
#endif /* DEBUG_VDISK */

    offset = env->regwptr[0];
    addr = env->regwptr[1];
    size = env->regwptr[2];
    unit = (size >> HSIMD_UNIT_SHIFT) & HSIMD_UNIT_MASK;
    cmd  = (size >> HSIMD_CMD_SHIFT) & HSIMD_CMD_MASK;
    size = size & HSIMD_SIZE_MASK;

    du = &NiagaraBoardStatePtr->disk_unit[unit];
    dp = &du->disk_part[0]; /* slice 0 */

#ifdef DEBUG_VDISK
    qemu_log("%s offset:0x%lx addr:0x%lx size:0x%lx, unit:0x%lx, cmd:0x%lx, size:0x%lx\n",
	__func__, offset, addr, env->regwptr[2], unit, cmd, size);
#endif
    switch (cmd) {
    case HSIMD_CMD_DATAIO_ASYNC:
    case HSIMD_CMD_IOV_ASYNC:
#ifndef SUPPORT_ASYNC
        qemu_log("%s: async i/o command(%ld) is not supported\n",
	    __func__, cmd);
	env->regwptr[0] = 1;
	env->regwptr[1] = 0;
	return (TRUE);
#endif	/* SUPPORT_ASYNC */

    case HSIMD_CMD_DATAIO:
    case HSIMD_CMD_IOV:
	/* XXX -- should use tas instruction */
        if (du->status & D_BUSY) {
	    /* we do not allow double I/O issue */
	    env->regwptr[0] = 0;
	    env->regwptr[1] = -2;	/* busy */

	    qemu_log("%s: vdisk%ld: cmd 0x%lx: busy ret:0x%lx 0x%lx\n",
		__func__, unit, cmd, env->regwptr[0], env->regwptr[1]);
	    return (TRUE);
        }
        du->status |= D_BUSY;

        if (cmd == HSIMD_CMD_IOV || cmd == HSIMD_CMD_IOV_ASYNC) {
	    goto do_iov;
	}
#if 0
    	goto do_io;
#else
	goto do_iov;
#endif
    }
    
    if (env->regwptr[5] == DISK_READ) {
	/*
	 * Special IOCTL
	 */
        switch (cmd) {
	case HSIMD_CMD_READCAPACITY:
	    size = 0;
	    for (i = 0; i < NUM_PART; i++, dp++) {
		if (dp->size == 0) {
		    /* no exist */
		    continue;
		}
		/* calc the end */
		size =  MAX(dp->offset + dp->size, size);
	    }
	    /* success */
	    env->regwptr[0] = 0;
	    env->regwptr[1] = size;
	    break;

	case HSIMD_CMD_GET_CAP:
	    env->regwptr[0] = 0;
	    env->regwptr[1] = 0;
#ifdef NEW_VDISK_BLKDEV
#ifdef SUPPORT_IOV
	    env->regwptr[1] |= HSIMD_CAP_IOV;
#endif
#ifdef SUPPORT_ASYNC
	    env->regwptr[1] |= HSIMD_CAP_ASYNC;
#endif
#endif
	    if (du->dtyp == 5 /* RODIRECT */) {
		 env->regwptr[1] |= HSIMD_CAP_CDROM;
	    }
	    break;

	case HSIMD_CMD_GET_IOSTATUS:
	    env->regwptr[0] = 0;

	    if (du->status & D_BUSY) { 
		/* in progress */
		env->regwptr[1] = HSIMD_IOSTATUS_BUSY;
		break;
	    } 

	    if ((du->status & D_ASYNC) == 0) { 
		/* no pended aio result code */
		qemu_log("%s: cmd get status: 0x%x 0x%x\n",
		    __func__, du->status, D_ASYNC);
		env->regwptr[1] = HSIMD_IOSTATUS_NOAIO;
		break;
	    } 

	    /* return valid iostatus */
	    du->status &= ~D_ASYNC;
	    if (du->status & D_ERROR) {
		/* error while aio */
	        du->status &= ~D_ERROR;
		env->regwptr[1] = HSIMD_IOSTATUS_ERR;
	    } else {
		env->regwptr[1] = HSIMD_IOSTATUS_OK;
	    }
	    break;

	default:
	    /* failure: illegal cmd */
	    env->regwptr[0] = 1;
	    break;
	}
#ifdef DEBUG_VDISK
	qemu_log("%s ret: 0x%lx 0x%lx\n",
	    __func__, env->regwptr[0], env->regwptr[1]);
#endif
    } else /* DISK_WRITE */ {
	/* failure: illegal cmd */
	env->regwptr[0] = 1;
    }
    return (TRUE);

do_iov:
#ifdef NEW_VDISK_BLKDEV 
    /* find slice */
    for (i = 0; i < NUM_PART; i++, dp++) {
        if (dp->size == 0) {
	    continue;
	}
#ifdef DEBUG_VDISK
	qemu_log("%s %ld -> [%d] 0x%lx (+0x%lx) + 0x%lx\n",
	    __func__, offset,
	    i, dp->offset, dp->size, offset - dp->offset);
#endif /* DEBUG_VDISK */
	if (dp->offset <= offset &&
            dp->offset + dp->size > offset) {
            /* found */ 
            goto found_v;
        }
    }

    qemu_log("%s: vdisk%ld: "
	    "offset 0x%lx is not found in disk partition table\n",
        __func__, unit, offset);
    goto x;

found_v:
    /* find host address of iov for real address */
    du->qiov.nalloc = 0;	/* sanity */
    iov = addr;
    if (cmd == HSIMD_CMD_DATAIO || cmd == HSIMD_CMD_DATAIO_ASYNC) {
	niov = 1;
	goto workaround;
    } else {
        niov = size;
        if (niov > VDISK_NIOV) {
	    /* too many */
            qemu_log("%s: vdisk%ld: noiv 0x%x is too many\n",
	        __func__, unit, niov);
	    goto x;
        }
    }
    bzero(du->mrs, sizeof (du->mrs));

    /* map in iov */
    iov_mrs = memory_region_find(get_system_memory(), iov, 1);
    if (!iov_mrs.mr) {
        qemu_log("%s: vdisk%ld: "
	    "No memory is mapped at iov address 0x%" HWADDR_PRIx "\n",
	    __func__, unit, iov);
	niov = 0;	/* no need to free qiov */
	goto err_unref;
    }

    if (!memory_region_is_ram(iov_mrs.mr) &&
	!memory_region_is_romd(iov_mrs.mr)) {

        qemu_log("%s: vdisk%ld: "
	    "Memory at iov address 0x%" HWADDR_PRIx "is not RAM\n",
	    __func__, unit, iov);
	niov = 0;	/* no need to free qiov */
	goto err_unref;
    }

    iov_haddr = qemu_map_ram_ptr(iov_mrs.mr->ram_block,
	iov_mrs.offset_within_region);

    /* copy in iovec from virtual machine memory */
    qemu_iovec_init(&du->qiov, niov);
workaround:
    for (i = 0; i < niov; i++, iov_haddr++) {
        if (cmd == HSIMD_CMD_DATAIO || cmd == HSIMD_CMD_DATAIO_ASYNC) {
	    g_assert(niov == 1);
	    ;
	} else {
	    addr = be64_to_cpu((long)iov_haddr->iov_base); /* sparc is BE */
	    size = be64_to_cpu((long)iov_haddr->iov_len);
	}
	/* find host address for real address */
	du->mrs[i] = memory_region_find(get_system_memory(), addr, 1);

	if (!du->mrs[i].mr) {
	    qemu_log("%s: vdisk%ld: "
	 	"No memory is mapped at iov:%d address 0x%" HWADDR_PRIx "\n",
		__func__, unit, i, addr);
	    goto err_unref;
	}

	if (!memory_region_is_ram(du->mrs[i].mr) &&
	    !memory_region_is_romd(du->mrs[i].mr)) {
	    qemu_log("%s: vdisk%ld: "
		"Memory at iov address 0x%" HWADDR_PRIx "is not RAM\n",
		__func__, unit, addr);
	    goto err_unref;
	}
	host_addr = qemu_map_ram_ptr(du->mrs[i].mr->ram_block,
	    du->mrs[i].offset_within_region);

	qemu_iovec_add(&du->qiov, host_addr, size);
#ifdef DEBUG_VDISK
        qemu_log("%s: vdisk%ld: "
	    "%d addr: 0x%" HWADDR_PRIx " size: %lx -> haddr: 0x%lx\n",
	    __func__, unit, i, addr, size, (long)host_addr);
#endif
        if (env->regwptr[5] == DISK_WRITE) {
	    /* invalidate tb in the memory */

	    tb_invalidate_phys_range(cs, (tb_page_addr_t)addr,
		(tb_page_addr_t)(addr + size - 1));
	}
    }
    size = du->qiov.size;	/* get total size */

    //g_assert(iov_mrs.mr);
    memory_region_unref(iov_mrs.mr);
    iov_mrs.mr = NULL;

    if (cmd == HSIMD_CMD_IOV_ASYNC ||
        cmd == HSIMD_CMD_DATAIO_ASYNC) {
	/* free resource on aio done */
	du->status |= D_ASYNC;
    }
    if (cmd == HSIMD_CMD_IOV_ASYNC ||
        cmd == HSIMD_CMD_DATAIO_ASYNC) {
        if (env->regwptr[5] == DISK_READ) {
            du->aiocb = blk_aio_preadv(dp->be.bb,
    	        offset - dp->offset, &du->qiov, 0, vdisk_rw_done_cb, (void *)dp);
        } else {
            du->aiocb = blk_aio_pwritev(dp->be.bb,
    	        offset - dp->offset, &du->qiov, 0, vdisk_rw_done_cb, (void *)dp);
        }
    } else {
        if (env->regwptr[5] == DISK_READ) {
            blk_preadv(dp->be.bb, offset - dp->offset, size, &du->qiov, 0);
        } else {
            blk_pwritev(dp->be.bb, offset - dp->offset, size, &du->qiov, 0);
	}
    }

    if (cmd == HSIMD_CMD_IOV_ASYNC ||
        cmd == HSIMD_CMD_DATAIO_ASYNC) {
	if (du->aiocb) {
	    /* success */
	    env->regwptr[0] = 0;
	    env->regwptr[1] = 0;
	    return (TRUE);
	}
	/* failed to issue aio request */
	g_assert(du->status & D_BUSY);
	du->status |= D_ERROR;
	du->status &= ~(D_BUSY | D_ASYNC);

        qemu_log("%s: vdisk%ld: failed to issue aio request\n",
	    __func__, unit);
	goto err_unref;
    }
#if 0
    /* sync: wait for aio done */
#ifdef USE_BLK_DRAIN
    blk_drain(dp->be.bb);
#else
    /* XXX didnt work, context was not released */
    while (du->status & D_BUSY)
	;
#endif
#endif

#else /* NEW_VDISK_BLKDEV */

    /* find disk slice */
    for (i = 0; i < NUM_PART; i++, dp++) {
        if (dp->size == 0) {
	    continue;
	}
#ifdef DEBUG_VDISK
	qemu_log("%s: vdisk:%ld:  %ld -> [%d] 0x%lx (+0x%lx) + 0x%lx\n",
	    __func__, unit, offset,
	    i, dp->offset, dp->size, offset - dp->offset);
#endif /* DEBUG_VDISK */
	if (dp->offset <= offset &&
            dp->offset + dp->size > offset) {
            /* found */ 
            goto found;
        }
    }
#ifdef DEBUG_VDISK
    qemu_log("%s: vdisk%ld: "
	    "offset 0x%lx is not found in disk partition table\n",
        __func__, unit, offset);
#endif /* DEBUG_VDISK */
    goto x;

found:
#ifdef DEBUG_VDISK
    qemu_log("%s: vdisk%ld: "
	    "offset 0x%lx + 0x%lx is in [%d] (0x%lx + 0x%lx)\n",
        __func__, unit, offset, size, i, dp->offset, dp->size);
#endif /* DEBUG_VDISK */
    du->qiov.nalloc = 0;	/* sanity */
    niov = 1;
    iov_mrs.mr = NULL;
    bzero(&du->mrs[0], sizeof (du->mrs[0]));

    /* trim I/O size */
    reqsize = MIN(size, dp->size - (offset - dp->offset));

    /* find host address for real address */
    du->mrs[0] = memory_region_find(get_system_memory(), addr, 1);
    if (!du->mrs[0].mr) {
        qemu_log("%s: vdisk%ld: "
	    "No memory is mapped at single address 0x%" HWADDR_PRIx "\n",
	    __func__, unit, addr);
        goto x;
    }

    if (!memory_region_is_ram(du->mrs[0].mr) &&
	!memory_region_is_romd(du->mrs[0].mr)) {
        qemu_log("%s: vdisk%ld: "
	    "Memory at address 0x%" HWADDR_PRIx "is not RAM\n",
	    __func__, unit, addr);
        goto err_unref;
    }

    host_addr = qemu_map_ram_ptr(du->mrs[0].mr->ram_block,
	du->mrs[0].offset_within_region);

#ifdef DEBUG_VDISK
    qemu_log("%s: vdisk%ld:  real 0x%lx -> host 0x%lx\n",
        __func__, unit, addr, (long)host_addr);
#endif /* DEBUG_VDISK */
    qemu_iovec_init(&du->qiov, 1);
    qemu_iovec_add(&du->qiov, host_addr, reqsize);
again:
    if (env->regwptr[5] == DISK_READ) {

	/* invalidate tb in the memory */
	tb_invalidate_phys_range(
	    cs, (tb_page_addr_t)addr, (tb_page_addr_t)(addr + reqsize - 1));

#ifdef NEW_VDISK_MMAP
	memcpy(host_addr, dp->be.map.addr + (offset - dp->offset), reqsize);
	size = reqsize;
#else
	du->status &= ~D_ERROR;
	if ((size = pread(dp->be.fd,
	    host_addr, reqsize, offset - dp->offset)) != reqsize) {

	    /* retry */
	    if (size == -1 && errno == EINTR) {
		goto again;
	    }

	    /* error */
	    qemu_log("%s: vdisk%ld: pread failed, size:0x%lx, reqsize:0x%lx, errno:%d\n",
		__func__, unit, size, reqsize, errno);
	    du->status |= D_ERROR;
        }
#endif /* NEW_VDISK_MMAP */

    } else {
        /* DISK_WRITE */
#ifdef NEW_VDISK_MMAP
	memcpy(dp->be.map.addr + (offset - dp->offset), host_addr, reqsize);
	size = reqsize;
#else
	if ((size = pwrite(dp->be.fd,
	    host_addr, reqsize, offset - dp->offset)) != reqsize) {

	    /* retry */
	    if (size == -1 && errno == EINTR) {
		goto again;
	    }

	    /* error */
	    qemu_log("%s: vdisk%ld: pwrite failed, size:0x%lx, reqsize:0x%lx\n",
		__func__, unit, size, reqsize);
	    du->status |= D_ERROR;
	}
#endif /* NEW_VDISK_MMAP */
    }
#endif /* NEW_VDISK_BLKDEV */

    /* check result */
    if (du->status & D_ERROR) {
	/* error */
	qemu_log("%s: vdisk%ld: %s failed\n",
	    __func__, unit,
	    (env->regwptr[5] == DISK_READ)
	    ? "blk_aio_preadv" : "blk_aio_pwrite");
    } else {
        /* success */
	err = 0;
        env->regwptr[0] = 0;
        env->regwptr[1] = size;
    }

    /* unlock the memery region */
err_unref:
    if (iov_mrs.mr) {
        memory_region_unref(iov_mrs.mr);
    }
    for (i = 0; i < niov; i++) {
        if (du->mrs[i].mr) {
	    memory_region_unref(du->mrs[i].mr);
	}
    }
    if (du->qiov.nalloc) {
	qemu_iovec_destroy(&du->qiov);
    }
    du->qiov.niov = 0;
x:
    if (err) {
	env->regwptr[0] = err;
    }
    du->status &= ~D_BUSY;
    g_assert((du->status & D_BUSY) == 0);
    return (TRUE);
}
#endif /* NEW_VDISK */
#if !defined(CONFIG_USER_ONLY)
void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;
    uint32_t pil = env->pil_in |
                  (env->softint & ~(SOFTINT_TIMER | SOFTINT_STIMER));

    /* We should be holding the BQL before we mess with IRQs */
    g_assert(bql_locked());

#if 1 /* BUG fix sun4v */
    if (env->ivec_status) {
        return;
    }
#else
    /* TT_IVEC has a higher priority (16) than TT_EXTINT (31..17) */
    if (env->ivec_status & 0x20) {
        return;
    }
#endif
    cs = env_cpu(env);
    /*
     * check if TM or SM in SOFTINT are set
     * setting these also causes interrupt 14
     */
    if (env->softint & (SOFTINT_TIMER | SOFTINT_STIMER)) {
        pil |= 1 << 14;
    }

    /*
     * The bit corresponding to psrpil is (1<< psrpil),
     * the next bit is (2 << psrpil).
     */
    if (pil < (2 << env->psrpil)) {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_HARD)) {
            trace_sparc64_cpu_check_irqs_reset_irq(env->interrupt_index);
            env->interrupt_index = 0;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        return;
    }

    if (cpu_interrupts_enabled(env)) {

        unsigned int i;

        for (i = 15; i > env->psrpil; i--) {
            if (pil & (1 << i)) {
                int old_interrupt = env->interrupt_index;
                int new_interrupt = TT_EXTINT | i;

                if (unlikely(env->tl > 0 && cpu_tsptr(env)->tt > new_interrupt
                  && ((cpu_tsptr(env)->tt & 0x1f0) == TT_EXTINT))) {
                    trace_sparc64_cpu_check_irqs_noset_irq(env->tl,
                                                      cpu_tsptr(env)->tt,
                                                      new_interrupt);
                } else if (old_interrupt != new_interrupt) {
                    env->interrupt_index = new_interrupt;
                    trace_sparc64_cpu_check_irqs_set_irq(i, old_interrupt,
                                                         new_interrupt);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (cpu_test_interrupt(cs, CPU_INTERRUPT_HARD)) {
        trace_sparc64_cpu_check_irqs_disabled(pil, env->pil_in, env->softint,
                                              env->interrupt_index);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}
#endif

void sparc_cpu_do_interrupt(CPUState *cs)
{
    CPUSPARCState *env = cpu_env(cs);
    int intno = cs->exception_index;
    trap_state *tsptr;

#ifdef COMPAT_LEGION
    if (sparc_legion_trap(cs)) {
        /* ignore this instruction */
        env->pc = env->npc;
        env->npc = env->npc + 4;
        cs->exception_index = -1;
        return;
    }
#endif /* COMPAT_LEGION */

#ifdef NEW_VDISK
    if (sparc_vdisk_trap(cs)) {
        /* process this instruction */
        env->pc = env->npc;
        env->npc = env->npc + 4;
        cs->exception_index = -1;
	return;
    }
#endif /* NEW_VDISK */

#ifdef DEBUG_PCALL
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x1ff) {
            name = "Unknown";
        } else if (intno >= 0x180) {
            name = "Hyperprivileged Trap Instruction";
        } else if (intno >= 0x100) {
            name = "Trap Instruction";
        } else if (intno >= 0xc0) {
            name = "Window Fill";
        } else if (intno >= 0x80) {
            name = "Window Spill";
        } else {
            name = excp_names[intno];
            if (!name) {
                name = "Unknown";
            }
        }

        qemu_log("%6d: %s (v=%04x)\n", count, name, intno);
        log_cpu_state(cs, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            qemu_log("       code=");
            ptr = (uint8_t *)env->pc;
            for (i = 0; i < 16; i++) {
                qemu_log(" %02x", ldub(ptr + i));
            }
            qemu_log("\n");
        }
#endif
        count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY)
    if (env->tl >= env->maxtl) {
        cpu_abort(cs, "Trap 0x%04x while trap level (%d) >= MAXTL (%d),"
                  " Error state", cs->exception_index, env->tl, env->maxtl);
        return;
    }
#endif
    if (env->tl < env->maxtl - 1) {
        env->tl++;
    } else {
        env->pstate |= PS_RED;
        if (env->tl < env->maxtl) {
            env->tl++;
        }
    }
    tsptr = cpu_tsptr(env);

    tsptr->tstate = sparc64_tstate(env);
    tsptr->tpc = env->pc;
    tsptr->tnpc = env->npc;
    tsptr->tt = intno;

    if (cpu_has_hypervisor(env)) {
        env->htstate[env->tl] = env->hpstate;
        /* XXX OpenSPARC T1 - UltraSPARC T3 have MAXPTL=2
           but this may change in the future */
        if (env->tl > 2) {
            env->hpstate |= HS_PRIV;
        }
    }

    if (env->def.features & CPU_FEATURE_GL) {
        cpu_gl_switch_gregs(env, env->gl + 1);
        env->gl++;
    }

    switch (intno) {
    case TT_IVEC:
        if (!cpu_has_hypervisor(env)) {
            cpu_change_pstate(env, PS_PEF | PS_PRIV | PS_IG);
        }
        break;
    case TT_TFAULT:
    case TT_DFAULT:
    case TT_TMISS ... TT_TMISS + 3:
    case TT_DMISS ... TT_DMISS + 3:
    case TT_DPROT ... TT_DPROT + 3:
        if (cpu_has_hypervisor(env)) {
            env->hpstate |= HS_PRIV;
            env->pstate = PS_PEF | PS_PRIV;
        } else {
            cpu_change_pstate(env, PS_PEF | PS_PRIV | PS_MG);
        }
        break;
    case TT_INSN_REAL_TRANSLATION_MISS ... TT_DATA_REAL_TRANSLATION_MISS:
    case TT_HTRAP ... TT_HTRAP + 127:
        env->hpstate |= HS_PRIV;
        break;
    default:
        cpu_change_pstate(env, PS_PEF | PS_PRIV | PS_AG);
        break;
    }

    if (intno == TT_CLRWIN) {
        cpu_set_cwp(env, cpu_cwp_dec(env, env->cwp - 1));
    } else if ((intno & 0x1c0) == TT_SPILL) {
        cpu_set_cwp(env, cpu_cwp_dec(env, env->cwp - env->cansave - 2));
    } else if ((intno & 0x1c0) == TT_FILL) {
        cpu_set_cwp(env, cpu_cwp_inc(env, env->cwp + 1));
    }

    if (cpu_hypervisor_mode(env)) {
        env->pc = (env->htba & ~0x3fffULL) | (intno << 5);
    } else {
        env->pc = env->tbr  & ~0x7fffULL;
        env->pc |= ((env->tl > 1) ? 1 << 14 : 0) | (intno << 5);
    }
    env->npc = env->pc + 4;
    cs->exception_index = -1;

    switch (intno) {
    case TT_EXTINT:
    case TT_IVEC:
        qemu_plugin_vcpu_interrupt_cb(cs, tsptr->tpc);
        break;
    default:
        qemu_plugin_vcpu_exception_cb(cs, tsptr->tpc);
    }
}

trap_state *cpu_tsptr(CPUSPARCState* env)
{
    return &env->ts[env->tl & MAXTL_MASK];
}

static bool do_modify_softint(CPUSPARCState *env, uint32_t value)
{
    if (env->softint != value) {
        env->softint = value;
#if !defined(CONFIG_USER_ONLY)
        if (cpu_interrupts_enabled(env)) {
            bql_lock();
            cpu_check_irqs(env);
            bql_unlock();
        }
#endif
        return true;
    }
    return false;
}

void helper_set_softint(CPUSPARCState *env, uint64_t value)
{
    if (do_modify_softint(env, env->softint | (uint32_t)value)) {
        trace_int_helper_set_softint(env->softint);
    }
}

void helper_clear_softint(CPUSPARCState *env, uint64_t value)
{
    if (do_modify_softint(env, env->softint & (uint32_t)~value)) {
        trace_int_helper_clear_softint(env->softint);
    }
}

void helper_write_softint(CPUSPARCState *env, uint64_t value)
{
    if (do_modify_softint(env, (uint32_t)value)) {
        trace_int_helper_write_softint(env->softint);
    }
}
