/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 */

#pragma once

#include <linux/bpf.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <stddef.h>
#include <stdint.h>

#include "bpfilter/cgen/fixup.h"
#include "bpfilter/cgen/printer.h"
#include "core/chain.h"
#include "core/dump.h"
#include "core/flavor.h"
#include "core/front.h"
#include "core/helper.h"
#include "core/hook.h"
#include "core/list.h"

#include "external/filter.h"

#define PIN_PATH_LEN 64

/**
 * Convenience macro to get the offset of a field in @ref
 * bf_program_context based on the frame pointer in @c BPF_REG_10 .
 */
#define BF_PROG_CTX_OFF(field)                                                 \
    (-(int)sizeof(struct bf_program_context) +                                 \
     (int)offsetof(struct bf_program_context, field))

/** Convenience macro to get an address in the scratch area of
 * @ref bf_program_context . */
#define BF_PROG_SCR_OFF(offset)                                                \
    (-(int)sizeof(struct bf_program_context) +                                 \
     (int)offsetof(struct bf_program_context, scratch) + (offset))

#define EMIT(program, x)                                                       \
    ({                                                                         \
        int __r = bf_program_emit((program), (x));                             \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

#define EMIT_KFUNC_CALL(program, function)                                     \
    ({                                                                         \
        int __r = bf_program_emit_kfunc_call((program), (function));           \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

#define EMIT_FIXUP(program, type, insn)                                        \
    ({                                                                         \
        int __r = bf_program_emit_fixup((program), (type), (insn), NULL);      \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

#define EMIT_FIXUP_CALL(program, function)                                     \
    ({                                                                         \
        int __r = bf_program_emit_fixup_call((program), (function));           \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

#define EMIT_FIXUP_JMP_NEXT_RULE(program, insn)                                \
    ({                                                                         \
        int __r = bf_program_emit_fixup(                                       \
            (program), BF_FIXUP_TYPE_JMP_NEXT_RULE, (insn), NULL);             \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

#define EMIT_LOAD_COUNTERS_FD_FIXUP(program, reg)                              \
    ({                                                                         \
        const struct bpf_insn ld_insn[2] = {BPF_LD_MAP_FD(reg, 0)};            \
        int __r = bf_program_emit_fixup(                                       \
            (program), BF_FIXUP_TYPE_COUNTERS_MAP_FD, ld_insn[0], NULL);       \
        if (__r < 0)                                                           \
            return __r;                                                        \
        __r = bf_program_emit((program), ld_insn[1]);                          \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

/**
 * Load a specific set's file descriptor.
 *
 * @note Similarly to every @c EMIT_* macro, it must be called from a function
 * returning an @c int , if the call fails, the macro will return a negative
 * errno value.
 *
 * @param program Program to generate the bytecode for. Can't be NULL.
 * @param reg Register to store the set file descriptor in.
 * @param index Index of the set in the program.
 */
#define EMIT_LOAD_SET_FD_FIXUP(program, reg, index)                            \
    ({                                                                         \
        union bf_fixup_attr __attr = {.set_index = (index)};                   \
        const struct bpf_insn ld_insn[2] = {BPF_LD_MAP_FD(reg, 0)};            \
        int __r = bf_program_emit_fixup((program), BF_FIXUP_TYPE_SET_MAP_FD,   \
                                        ld_insn[0], &__attr);                  \
        if (__r < 0)                                                           \
            return __r;                                                        \
        __r = bf_program_emit((program), ld_insn[1]);                          \
        if (__r < 0)                                                           \
            return __r;                                                        \
    })

struct bf_chain;
struct bf_map;
struct bf_marsh;
struct bf_counter;

/**
 * BPF program runtime context.
 *
 * This structure is used to easily read and write data from the program's
 * stack. At runtime, the first stack frame of each generated program will
 * contain data according to @ref bf_program_context .
 *
 * The generated programs uses BPF dynamic pointer slices to safely access the
 * packet's data. @c bpf_dynptr_slice requires a user-provided buffer into which
 * it might copy the requested data, depending on the BPF program type: that is
 * the purpose of the anonynous unions, big enough to store the supported
 * protocol headers. @c bpf_dynptr_slice returns the address of the requested
 * data, which is either the address of the user-buffer, or the address of the
 * data in the packet (if the data hasn't be copied). The program will store
 * this address into the runtime context (i.e. @c l2_hdr , @c l3_hdr , and
 * @c l4_hdr ), and it will be used to access the packet's data.
 *
 * While earlier versions of this structure contained the L3 and L4 protocol IDs,
 * they have been move to registers instead, as old version of the verifier
 * can't keep track of scalar values in the stack, leading to verification
 * failures.
 *
 * @warning Not all the BPF verifier versions are born equal as older ones might
 * require stack access to be 8-bytes aligned to work properly.
 */
struct bf_program_context
{
    /** Argument passed to the BPF program, its content depends on the BPF
     * program type. */
    void *arg;

    /** BPF dynamic pointer representing the packet data. Dynamic pointers are
     * used with every program type. */
    struct bpf_dynptr dynptr;

    /** Total size of the packet. */
    uint64_t pkt_size;

    /** Offset of the layer 3 protocol. */
    uint32_t l3_offset;

    /** Offset of the layer 4 protocol. */
    uint32_t l4_offset;

    /** On ingress, index of the input interface. On egress, index of the
     * output interface. */
    uint32_t ifindex;

    /** Pointer to the L2 protocol header. */
    void *l2_hdr;

    /** Pointer to the L3 protocol header. */
    void *l3_hdr;

    /** Pointer to the L4 protocol header. */
    void *l4_hdr;

    /** Layer 2 header. */
    union
    {
        struct ethhdr _ethhdr;
        char l2_raw[0];
    } bf_aligned(8);

    /** Layer 3 header. */
    union
    {
        struct iphdr _ip4hdr;
        struct ipv6hdr _ip6hdr;
        char l3_raw[0];
    } bf_aligned(8);

    /** Layer 3 header. */
    union
    {
        struct icmphdr _icmphdr;
        struct udphdr _udphdr;
        struct tcphdr _tcphdr;
        struct icmp6hdr _icmp6hdr;
        char l4_raw[0];
    } bf_aligned(8);

    uint8_t bf_aligned(8) scratch[64];
} bf_aligned(8);

static_assert(sizeof(struct bf_program_context) % 8 == 0,
              "struct bf_program_context must be 8-bytes aligned");

struct bf_program
{
    enum bf_hook hook;
    enum bf_front front;
    char prog_name[BPF_OBJ_NAME_LEN];
    char link_name[BPF_OBJ_NAME_LEN];
    /// Printer map name.
    char pmap_name[BPF_OBJ_NAME_LEN];
    char prog_pin_path[PIN_PATH_LEN];
    char link_pin_path[PIN_PATH_LEN];
    /// Pinter map pinning path.
    char pmap_pin_path[PIN_PATH_LEN];

    /// Counters map
    struct bf_map *counters;

    /// List of @ref bf_map used to store the sets.
    bf_list sets;

    /// Log messages printer.
    struct bf_printer *printer;

    /** Number of counters in the counters map. Not all of them are used by
     * the program, but this value is common for all the programs of a given
     * codegen. */
    size_t num_counters;

    /* Bytecode */
    uint32_t functions_location[_BF_FIXUP_FUNC_MAX];
    struct bpf_insn *img;
    size_t img_size;
    size_t img_cap;
    bf_list fixups;

    /** Runtime data used to interact with the program and cache information.
     * This data is not serialized. */
    struct
    {
        /** File descriptor of the program. */
        int prog_fd;
        /** File descriptor of the program's link. */
        int link_fd;
        /** File descriptor of the printer map. */
        int pmap_fd;
        /** Hook-specific ops to use to generate the program. */
        const struct bf_flavor_ops *ops;

        /** Chain the program is generated from. This is a non-owning pointer:
         * the @ref bf_program doesn't have to manage its lifetime. */
        const struct bf_chain *chain;
    } runtime;
};

#define _cleanup_bf_program_ __attribute__((__cleanup__(bf_program_free)))

int bf_program_new(struct bf_program **program, enum bf_hook hook,
                   enum bf_front front, const struct bf_chain *chain);
void bf_program_free(struct bf_program **program);
int bf_program_marsh(const struct bf_program *program, struct bf_marsh **marsh);
int bf_program_unmarsh(const struct bf_marsh *marsh,
                       struct bf_program **program,
                       const struct bf_chain *chain);
void bf_program_dump(const struct bf_program *program, prefix_t *prefix);
int bf_program_grow_img(struct bf_program *program);

int bf_program_emit(struct bf_program *program, struct bpf_insn insn);
int bf_program_emit_kfunc_call(struct bf_program *program, const char *name);
int bf_program_emit_fixup(struct bf_program *program, enum bf_fixup_type type,
                          struct bpf_insn insn,
                          const union bf_fixup_attr *attr);
int bf_program_emit_fixup_call(struct bf_program *program,
                               enum bf_fixup_func function);
int bf_program_generate(struct bf_program *program);

/**
 * Load and attach the program to the kernel.
 *
 * Perform the loading and attaching of the program to the kernel in one
 * step. If a similar program already exists, @p old_prog should be a pointer
 * to it, and will be replaced.
 *
 * @param new_prog New program to load and attach to the kernel. Can't be NULL.
 * @param old_prog Existing program to replace.
 * @return 0 on success, or negative errno value on failure.
 */
int bf_program_load(struct bf_program *new_prog, struct bf_program *old_prog);

int bf_program_unload(struct bf_program *program);

int bf_program_get_counter(const struct bf_program *program,
                           uint32_t counter_idx, struct bf_counter *counter);
int bf_program_set_counters(struct bf_program *program,
                            const struct bf_counter *counters);
