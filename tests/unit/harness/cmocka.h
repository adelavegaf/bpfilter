/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 */

/**
 * @file
 * @brief CMocka test harness
 *
 * CMocka doesn't impot the headers it uses, so we have to do it here.
 */

// clang-format off
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <cmocka.h>
// clanf-format on

#include "shared/helper.h"

#define NOT_NULL ((void *)0xdeadbeef)

#define Test(group, name)                                                      \
    __attribute__((section(".bf_test"))) void group##__##name(bf_unused void **state)
