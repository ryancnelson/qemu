/*
 * QEMU Sun4u/Sun4v System Emulator common routines
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/sparc/sparc64.h"
#include "qemu/timer.h"
#include "system/reset.h"
#include "trace.h"

#include "qemu/log.h"


#define TICK_MAX             0x7fffffffffffffffULL

static void cpu_kick_irq(SPARCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUSPARCState *env = &cpu->env;

    /* wakeup cpu */
#if 1
    if (cpu_test_interrupt(cs, CPU_INTERRUPT_HALT)) {
	qemu_log/*_mask*/(/*CPU_LOG_INT,*/
	    "cpu:%d %s CPU_INTERRUPT_HALT pended,"
	    " soft:0x%x intr_ix:0x%x pil:%d, pst:%x hpst:%lx\n",
	    cs->cpu_index, __func__, env->softint, env->interrupt_index,
	    env->psrpil, env->pstate, env->hpstate);
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HALT);
    }
#endif

#ifndef CONF_MP_INTR
    //cs->halted = 0;
    cpu_check_irqs(env);
#else
    /* make cpu acknowledge softint */
    cpu_set_interrupt(env_cpu(env), CPU_INTERRUPT_HARD);
#endif
#if 1
    qemu_log_mask(CPU_LOG_INT,
	"cpu:%d %s softint:0x%x intr_ix:0x%x pil:%d, pst:%x hpst:%lx locked:%d\n",
	cs->cpu_index, __func__, env->softint, env->interrupt_index,
	env->psrpil, env->pstate, env->hpstate, bql_locked());
#endif
#if 1 /* ndef CONF_MP_INTR mandatory */
    qemu_cpu_kick(cs);
#endif
}

void sparc64_cpu_set_ivec_irq(void *opaque, int irq, int level)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;
    CPUState *cs;

    if (level) {
        if (!(env->ivec_status & 0x20)) {
            trace_sparc64_cpu_ivec_raise_irq(irq);
            cs = CPU(cpu);
            cs->halted = 0;
            env->interrupt_index = TT_IVEC;
            env->ivec_status |= 0x20;
            env->ivec_data[0] = (0x1f << 6) | irq;
            env->ivec_data[1] = 0;
            env->ivec_data[2] = 0;
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    } else {
        if (env->ivec_status & 0x20) {
            trace_sparc64_cpu_ivec_lower_irq(irq);
            cs = CPU(cpu);
            env->ivec_status &= ~0x20;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

typedef struct ResetData {
    SPARCCPU *cpu;
    uint64_t prom_addr;
#if 1 /* sun4v */
    uint64_t membase;
    uint64_t memsize; 
    uint64_t hypervisor_desc;
    uint64_t strand_start_set;
    uint64_t total_memsize; 
#endif
} ResetData;

static CPUTimer *cpu_timer_create(const char *name, SPARCCPU *cpu,
                                  QEMUBHFunc *cb, uint32_t frequency,
                                  uint64_t disabled_mask, uint64_t npt_mask)
{
    CPUTimer *timer = g_new0(CPUTimer, 1);

    timer->name = name;
    timer->frequency = frequency;
    timer->disabled_mask = disabled_mask;
    timer->npt_mask = npt_mask;

    timer->disabled = 1;
    timer->npt = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer->qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cb, cpu);

    return timer;
}

static void cpu_timer_reset(CPUTimer *timer)
{
    timer->disabled = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer_del(timer->qtimer);
}

#if 1 /* sun4v */
#define	VER_MASK_SHIFT		24
#define	VER_MASK_MASK		0xff
#define	VER_MASK_MAJOR_SHIFT	(VER_MASK_SHIFT + 4)
#define	VER_MASK_MAJOR_MASK	0xf
#endif
static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUSPARCState *env = &s->cpu->env;
    static unsigned int nr_resets;

    cpu_reset(CPU(s->cpu));

    cpu_timer_reset(env->tick);
    cpu_timer_reset(env->stick);
    cpu_timer_reset(env->hstick);

    env->gregs[1] = 0; /* Memory start */
    env->gregs[2] = current_machine->ram_size; /* Memory size */
    env->gregs[3] = 0; /* Machine description XXX */
#if 0  /* XXX this cause hang, why ? */
    env->gregs[4] = (1UL << current_machine->smp.cpus) - 1; /* strand start */
    env->gregs[5] = 0;	/* total physical memory, not used */
#endif
#if 1 /* sun4v */
    env->ssr = ((CPU(s->cpu)->cpu_index) << 8) | 1;
    env->hver = 2 << VER_MASK_MAJOR_SHIFT;	/* Niagara 1 */
#endif
    if (nr_resets++ == 0) {
        /* Power on reset */
        env->pc = s->prom_addr + 0x20ULL;
    } else {
	CPU(s->cpu)->halted = 0;
        env->pc = s->prom_addr + 0x20ULL;
    }
    env->npc = env->pc + 4;
}
#if 1 /* sun4v  doesn't work, why? */
/*
 * Niagara %ver
 */
static void main_cpu_reset_sun4v(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUSPARCState *env = &s->cpu->env;

    cpu_reset(CPU(s->cpu));

    cpu_timer_reset(env->tick);
    cpu_timer_reset(env->stick);
    cpu_timer_reset(env->hstick);

    env->gregs[1] = s->membase; /* Memory start */
    env->gregs[2] = current_machine->ram_size; /* Memory size */
    env->gregs[3] = s->hypervisor_desc;
    env->gregs[4] = (1UL << current_machine->smp.cpus) - 1; /* strand start */
    env->gregs[5] = 0;	/* total physical memory, not used */
    env->ssr = ((CPU(s->cpu)->cpu_index) << 8) | 1;
    env->hver = 2 << VER_MASK_MAJOR_SHIFT;	/* Niagara 1 */
    if (CPU(s->cpu)->cpu_index == 0) {
        /* Power on reset */
        env->pc = s->prom_addr + 0x20;
    } else {
	/*
	 * XXX:
	 * brain damaged early niagara cpus with reset.bin start
	 * from +0x20, not 0x30.
	 */
        env->pc = s->prom_addr + 0x20;
	/* XXX workaround for now */
	//CPU(s->cpu)->halted = 1;
	CPU(s->cpu)->halted = 0;
	//CPU(s->cpu)->stop = 1;
	//CPU(s->cpu)->stopped = 1;
    }
    env->npc = env->pc + 4;
}
#endif
static void tick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->tick;
#if 1
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    qemu_log_mask(CPU_LOG_INT,
	"cpu:%d %s: disabled:%d, now:%ld\n",
	env_cpu(env)->cpu_index, __func__, timer->disabled, now);
}
#endif

    if (timer->disabled) {
        trace_sparc64_cpu_tick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_tick_irq_fire();
    }
#if 1 /* sum4v */
    qatomic_or(&env->softint, SOFTINT_TIMER);
#else
    env->softint |= SOFTINT_TIMER;
#endif
    cpu_kick_irq(cpu);
}

static void stick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->stick;
#if 1
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    qemu_log_mask(CPU_LOG_INT,
	"cpu:%d %s: disabled:%d, now:%ld\n",
	env_cpu(env)->cpu_index, __func__, timer->disabled, now);
}
#endif

    if (timer->disabled) {
        trace_sparc64_cpu_stick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_stick_irq_fire();
    }

#if 1 /* sun4v */
    qatomic_or(&env->softint, SOFTINT_STIMER);
#else
    env->softint |= SOFTINT_STIMER;
#endif
    cpu_kick_irq(cpu);
}

static void hstick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->hstick;
#if 1
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    qemu_log_mask(CPU_LOG_INT,
	"cpu:%d %s: disabled:%d, now:%ld\n",
	env_cpu(env)->cpu_index, __func__, timer->disabled, now);
}
#endif
    if (timer->disabled) {
        trace_sparc64_cpu_hstick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_hstick_irq_fire();
    }

#if 1 /* sun4v */
    qatomic_or(&env->softint, SOFTINT_STIMER);
#else
    env->softint |= SOFTINT_STIMER;
#endif
    cpu_kick_irq(cpu);
}

static int64_t cpu_to_timer_ticks(int64_t cpu_ticks, uint32_t frequency)
{
    return muldiv64(cpu_ticks, NANOSECONDS_PER_SECOND, frequency);
}

static uint64_t timer_to_cpu_ticks(int64_t timer_ticks, uint32_t frequency)
{
    return muldiv64(timer_ticks, frequency, NANOSECONDS_PER_SECOND);
}

void cpu_tick_set_count(CPUTimer *timer, uint64_t count)
{
    uint64_t real_count = count & ~timer->npt_mask;
    uint64_t npt_bit = count & timer->npt_mask;

    int64_t vm_clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                    cpu_to_timer_ticks(real_count, timer->frequency);

    trace_sparc64_cpu_tick_set_count(timer->name, real_count,
                                     timer->npt ? "disabled" : "enabled",
                                     timer);

    timer->npt = npt_bit ? 1 : 0;
    timer->clock_offset = vm_clock_offset;
}

uint64_t cpu_tick_get_count(CPUTimer *timer)
{
    uint64_t real_count = timer_to_cpu_ticks(
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->clock_offset,
                    timer->frequency);

    trace_sparc64_cpu_tick_get_count(timer->name, real_count,
                                     timer->npt ? "disabled" : "enabled",
                                     timer);

    if (timer->npt) {
        real_count |= timer->npt_mask;
    }

    return real_count;
}

void cpu_tick_set_limit(CPUTimer *timer, uint64_t limit)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    uint64_t real_limit = limit & ~timer->disabled_mask;
    timer->disabled = (limit & timer->disabled_mask) ? 1 : 0;

    int64_t expires = cpu_to_timer_ticks(real_limit, timer->frequency) +
                    timer->clock_offset;

    if (expires < now) {
        expires = now + 1;
    }
#if 1
    qemu_log_mask(CPU_LOG_INT, "cpu:%d %s: now:%ld limit:%ld\n",
	current_cpu->cpu_index, __func__, now, expires - now);
#endif
    trace_sparc64_cpu_tick_set_limit(timer->name, real_limit,
                                     timer->disabled ? "disabled" : "enabled",
                                     timer, limit,
                                     timer_to_cpu_ticks(
                                         now - timer->clock_offset,
                                         timer->frequency
                                     ),
                                     timer_to_cpu_ticks(
                                         expires - now, timer->frequency
                                     ));

    if (!real_limit) {
        trace_sparc64_cpu_tick_set_limit_zero(timer->name);
        timer_del(timer->qtimer);
    } else if (timer->disabled) {
        timer_del(timer->qtimer);
    } else {
        timer_mod(timer->qtimer, expires);
    }
}

SPARCCPU *sparc64_cpu_devinit(const char *cpu_type, uint64_t prom_addr)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;
    ResetData *reset_info;

    uint32_t   tick_frequency = 100 * 1000000;
    uint32_t  stick_frequency = 100 * 1000000;
    uint32_t hstick_frequency = 100 * 1000000;

    cpu = SPARC_CPU(object_new(cpu_type));
    qdev_init_gpio_in_named(DEVICE(cpu), sparc64_cpu_set_ivec_irq,
                            "ivec-irq", IVEC_MAX);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);
    env = &cpu->env;

    env->tick = cpu_timer_create("tick", cpu, tick_irq,
                                  tick_frequency, TICK_INT_DIS,
                                  TICK_NPT_MASK);

    env->stick = cpu_timer_create("stick", cpu, stick_irq,
                                   stick_frequency, TICK_INT_DIS,
                                   TICK_NPT_MASK);

    env->hstick = cpu_timer_create("hstick", cpu, hstick_irq,
                                    hstick_frequency, TICK_INT_DIS,
                                    TICK_NPT_MASK);

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = cpu;
    reset_info->prom_addr = prom_addr;
    qemu_register_reset(main_cpu_reset, reset_info);

    return cpu;
}
#if 1 /* sun4v */
SPARCCPU *sparc64_cpu_devinit_sun4v(const char *cpu_type,
    uint64_t prom_addr, uint64_t membase, uint64_t hypervisor_desc)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;
    ResetData *reset_info;

    uint32_t   tick_frequency = 200 * 1000000;
    uint32_t  stick_frequency = 200 * 1000000;
    uint32_t hstick_frequency = 200 * 1000000;

    cpu = SPARC_CPU(object_new(cpu_type));
    qdev_init_gpio_in_named(DEVICE(cpu), sparc64_cpu_set_ivec_irq,
                            "ivec-irq", IVEC_MAX);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);
    env = &cpu->env;

    env->tick = cpu_timer_create("tick", cpu, tick_irq,
                                  tick_frequency, TICK_INT_DIS,
                                  TICK_NPT_MASK);

    env->stick = cpu_timer_create("stick", cpu, stick_irq,
                                   stick_frequency, TICK_INT_DIS,
                                   TICK_NPT_MASK);

    env->hstick = cpu_timer_create("hstick", cpu, hstick_irq,
                                    hstick_frequency, TICK_INT_DIS,
                                    TICK_NPT_MASK);

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = cpu;
    reset_info->prom_addr = prom_addr;
    reset_info->membase = membase;
    reset_info->hypervisor_desc = hypervisor_desc;
    qemu_register_reset(main_cpu_reset_sun4v, reset_info);

    return cpu;
}
#endif
