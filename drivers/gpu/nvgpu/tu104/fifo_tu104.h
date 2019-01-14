/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef NVGPU_FIFO_TU104_H
#define NVGPU_FIFO_TU104_H

#include <nvgpu/types.h>

struct gk20a;
struct channel_gk20a;

int channel_tu104_setup_ramfc(struct channel_gk20a *c,
                u64 gpfifo_base, u32 gpfifo_entries,
                unsigned long acquire_timeout, u32 flags);
int tu104_init_fifo_setup_hw(struct gk20a *g);
void tu104_ring_channel_doorbell(struct channel_gk20a *c);
u64 tu104_fifo_usermode_base(struct gk20a *g);
u32 tu104_fifo_doorbell_token(struct channel_gk20a *c);

int tu104_init_pdb_cache_war(struct gk20a *g);
void tu104_deinit_pdb_cache_war(struct gk20a *g);
u32 tu104_fifo_read_pbdma_data(struct gk20a *g, u32 pbdma_id);
void tu104_fifo_reset_pbdma_header(struct gk20a *g, u32 pbdma_id);

#endif /* NVGPU_FIFO_TU104_H */
