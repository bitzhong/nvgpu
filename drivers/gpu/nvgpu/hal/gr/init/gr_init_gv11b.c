/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/io.h>

#include "gr_init_gv11b.h"

#include <nvgpu/hw/gv11b/hw_gr_gv11b.h>

int gv11b_gr_init_preemption_state(struct gk20a *g, u32 gfxp_wfi_timeout_count,
	bool gfxp_wfi_timeout_unit_usec)
{
	u32 debug_2;
	u32 unit;

	nvgpu_log_fn(g, " ");

	if (gfxp_wfi_timeout_unit_usec) {
		unit = gr_debug_2_gfxp_wfi_timeout_unit_usec_f();
	} else {
		unit = gr_debug_2_gfxp_wfi_timeout_unit_sysclk_f();
	}

	debug_2 = nvgpu_readl(g, gr_debug_2_r());
	debug_2 = set_field(debug_2,
		gr_debug_2_gfxp_wfi_timeout_unit_m(),
		unit);
	nvgpu_writel(g, gr_debug_2_r(), debug_2);

	return 0;
}

