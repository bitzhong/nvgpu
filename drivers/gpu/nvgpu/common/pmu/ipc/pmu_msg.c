/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/pmu/allocator.h>
#include <nvgpu/engine_fb_queue.h>
#include <nvgpu/engine_queue.h>
#include <nvgpu/pmu/msg.h>
#include <nvgpu/string.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/pmu/lsfm.h>
#include <nvgpu/pmu/super_surface.h>
#include <nvgpu/pmu/pmu_perfmon.h>
#include <nvgpu/pmu/fw.h>

static int pmu_payload_extract(struct nvgpu_pmu *pmu, struct pmu_sequence *seq)
{
	struct nvgpu_engine_fb_queue *fb_queue =
				nvgpu_pmu_seq_get_cmd_queue(seq);
	struct gk20a *g = gk20a_from_pmu(pmu);
	struct pmu_fw_ver_ops *fw_ops = &g->pmu.fw.ops;
	u32 fbq_payload_offset = 0U;
	int err = 0;

	nvgpu_log_fn(g, " ");

	if (nvgpu_pmu_seq_get_out_payload_fb_queue(seq)) {
		fbq_payload_offset =
			nvgpu_engine_fb_queue_get_offset(fb_queue) +
			nvgpu_pmu_seq_get_fbq_out_offset(seq) +
			(nvgpu_pmu_seq_get_fbq_element_index(seq) *
			 nvgpu_engine_fb_queue_get_element_size(fb_queue));

		nvgpu_mem_rd_n(g, nvgpu_pmu_super_surface_mem(g,
			pmu, pmu->super_surface), fbq_payload_offset,
			nvgpu_pmu_seq_get_out_payload(seq),
			fw_ops->allocation_get_dmem_size(pmu,
				fw_ops->get_seq_out_alloc_ptr(seq)));

	} else {
		if (fw_ops->allocation_get_dmem_size(pmu,
				fw_ops->get_seq_out_alloc_ptr(seq)) != 0U) {
			err = nvgpu_falcon_copy_from_dmem(&pmu->flcn,
					fw_ops->allocation_get_dmem_offset(pmu,
					  fw_ops->get_seq_out_alloc_ptr(seq)),
					nvgpu_pmu_seq_get_out_payload(seq),
					fw_ops->allocation_get_dmem_size(pmu,
					  fw_ops->get_seq_out_alloc_ptr(seq)),
					0);
			if (err != 0) {
				nvgpu_err(g, "PMU falcon DMEM copy failed");
				return err;
			}
		}
	}

	return err;
}

static void pmu_payload_free(struct nvgpu_pmu *pmu, struct pmu_sequence *seq)
{
	struct nvgpu_engine_fb_queue *fb_queue =
				nvgpu_pmu_seq_get_cmd_queue(seq);
	struct gk20a *g = gk20a_from_pmu(pmu);
	struct pmu_fw_ver_ops *fw_ops = &g->pmu.fw.ops;
	struct nvgpu_mem *in_mem = nvgpu_pmu_seq_get_in_mem(seq);
	struct nvgpu_mem *out_mem = nvgpu_pmu_seq_get_out_mem(seq);
	void *seq_in_ptr = fw_ops->get_seq_in_alloc_ptr(seq);
	void *seq_out_ptr = fw_ops->get_seq_out_alloc_ptr(seq);
	int err;

	nvgpu_log_fn(g, " ");

	if (nvgpu_pmu_fb_queue_enabled(&pmu->queues)) {
		nvgpu_free(&pmu->dmem, nvgpu_pmu_seq_get_fbq_heap_offset(seq));

		/*
		 * free FBQ allocated work buffer
		 * set FBQ element work buffer to NULL
		 * Clear the in use bit for the queue entry this CMD used.
		 */
		err = nvgpu_engine_fb_queue_free_element(fb_queue,
				nvgpu_pmu_seq_get_fbq_element_index(seq));
		if (err != 0) {
			nvgpu_err(g, "fb queue element free failed %d", err);
		}
	} else {
		/* free DMEM space payload*/
		if (fw_ops->allocation_get_dmem_size(pmu,
			seq_in_ptr) != 0U) {
			nvgpu_free(&pmu->dmem,
				fw_ops->allocation_get_dmem_offset(pmu,
					seq_in_ptr));

			fw_ops->allocation_set_dmem_size(pmu,
				seq_in_ptr, 0);
		}

		if (fw_ops->allocation_get_dmem_size(pmu,
			seq_out_ptr) != 0U) {
			nvgpu_free(&pmu->dmem,
				fw_ops->allocation_get_dmem_offset(pmu,
					seq_out_ptr));

			fw_ops->allocation_set_dmem_size(pmu,
				seq_out_ptr, 0);
		}
	}

	if (out_mem != NULL) {
		(void) memset(fw_ops->allocation_get_fb_addr(pmu,
				seq_out_ptr), 0x0,
				fw_ops->allocation_get_fb_size(pmu,
					seq_out_ptr));

		nvgpu_pmu_surface_free(g, out_mem);
		if (out_mem != in_mem) {
			nvgpu_kfree(g, out_mem);
		}
	}

	if (in_mem != NULL) {
		(void) memset(fw_ops->allocation_get_fb_addr(pmu,
				seq_in_ptr), 0x0,
				fw_ops->allocation_get_fb_size(pmu,
					seq_in_ptr));

		nvgpu_pmu_surface_free(g, in_mem);
		nvgpu_kfree(g, in_mem);
	}

	nvgpu_pmu_seq_payload_free(g, seq);
}

static int pmu_response_handle(struct nvgpu_pmu *pmu,
			struct pmu_msg *msg)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	enum pmu_seq_state state;
	struct pmu_sequence *seq;
	int err = 0;
	u8 id;

	nvgpu_log_fn(g, " ");

	seq = nvgpu_pmu_sequences_get_seq(pmu->sequences, msg->hdr.seq_id);
	state = nvgpu_pmu_seq_get_state(seq);
	id = nvgpu_pmu_seq_get_id(seq);

	if (state != PMU_SEQ_STATE_USED) {
		nvgpu_err(g, "msg for an unknown sequence %u", (u32) id);
		err = -EINVAL;
		goto exit;
	}

	if (msg->hdr.unit_id == PMU_UNIT_RC &&
		msg->msg.rc.msg_type == PMU_RC_MSG_TYPE_UNHANDLED_CMD) {
		nvgpu_err(g, "unhandled cmd: seq %u", (u32) id);
		err = -EINVAL;
	} else {
		err = pmu_payload_extract(pmu, seq);
	}

exit:
	/*
	 * free allocated space for payload in
	 * DMEM/FB-surface/FB_QUEUE as data is
	 * copied to buffer pointed by
	 * seq->out_payload
	 */
	pmu_payload_free(pmu, seq);

	nvgpu_pmu_seq_callback(g, seq, msg, err);

	nvgpu_pmu_seq_release(g, pmu->sequences, seq);

	/* TBD: notify client waiting for available dmem */

	nvgpu_log_fn(g, "done err %d", err);

	return err;
}

static int pmu_handle_event(struct nvgpu_pmu *pmu, struct pmu_msg *msg)
{
	int err = 0;
	struct gk20a *g = gk20a_from_pmu(pmu);

	nvgpu_log_fn(g, " ");
	switch (msg->hdr.unit_id) {
	case PMU_UNIT_PERFMON:
	case PMU_UNIT_PERFMON_T18X:
		err = nvgpu_pmu_handle_perfmon_event(pmu, &msg->msg.perfmon);
		break;
	case PMU_UNIT_PERF:
		if (g->ops.pmu_perf.handle_pmu_perf_event != NULL) {
			err = g->ops.pmu_perf.handle_pmu_perf_event(g,
				(void *)&msg->msg.perf);
		} else {
			WARN_ON(true);
		}
		break;
	case PMU_UNIT_THERM:
		if (pmu->therm_event_handler != NULL) {
			pmu->therm_event_handler(g, pmu, msg, NULL);
		}
		break;
	default:
		break;
	}

	return err;
}

static bool pmu_engine_mem_queue_read(struct nvgpu_pmu *pmu,
	u32 queue_id, void *data,
	u32 bytes_to_read, int *status)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	u32 bytes_read;
	int err;

	err = nvgpu_pmu_queue_pop(&pmu->queues, &pmu->flcn, queue_id, data,
				  bytes_to_read, &bytes_read);
	if (err != 0) {
		nvgpu_err(g, "fail to read msg: err %d", err);
		*status = err;
		return false;
	}
	if (bytes_read != bytes_to_read) {
		nvgpu_err(g, "fail to read requested bytes: 0x%x != 0x%x",
			bytes_to_read, bytes_read);
		*status = -EINVAL;
		return false;
	}

	return true;
}

static bool pmu_read_message(struct nvgpu_pmu *pmu, u32 queue_id,
	struct pmu_msg *msg, int *status)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	u32 read_size;
	int err;

	*status = 0;

	if (nvgpu_pmu_queue_is_empty(&pmu->queues, queue_id)) {
		return false;
	}

	if (!pmu_engine_mem_queue_read(pmu, queue_id, &msg->hdr,
				       PMU_MSG_HDR_SIZE, status)) {
		nvgpu_err(g, "fail to read msg from queue %d", queue_id);
		goto clean_up;
	}

	if (msg->hdr.unit_id == PMU_UNIT_REWIND) {
		if (!nvgpu_pmu_fb_queue_enabled(&pmu->queues)) {
			err = nvgpu_pmu_queue_rewind(&pmu->queues, queue_id,
						     &pmu->flcn);
			if (err != 0) {
				nvgpu_err(g, "fail to rewind queue %d",
					  queue_id);
				*status = err;
				goto clean_up;
			}
		}

		/* read again after rewind */
		if (!pmu_engine_mem_queue_read(pmu, queue_id, &msg->hdr,
				PMU_MSG_HDR_SIZE, status)) {
			nvgpu_err(g, "fail to read msg from queue %d",
				queue_id);
			goto clean_up;
		}
	}

	if (!PMU_UNIT_ID_IS_VALID(msg->hdr.unit_id)) {
		nvgpu_err(g, "read invalid unit_id %d from queue %d",
			msg->hdr.unit_id, queue_id);
			*status = -EINVAL;
			goto clean_up;
	}

	if (msg->hdr.size > PMU_MSG_HDR_SIZE) {
		read_size = msg->hdr.size - PMU_MSG_HDR_SIZE;
		if (!pmu_engine_mem_queue_read(pmu, queue_id, &msg->msg,
					       read_size, status)) {
			nvgpu_err(g, "fail to read msg from queue %d",
				queue_id);
			goto clean_up;
		}
	}

	return true;

clean_up:
	return false;
}

static void pmu_read_init_msg_fb(struct gk20a *g, struct nvgpu_pmu *pmu,
	u32 element_index, u32 size, void *buffer)
{
	u32 fbq_msg_queue_ss_offset = 0U;

	fbq_msg_queue_ss_offset =
		nvgpu_pmu_get_ss_msg_fbq_element_offset(g, pmu,
			pmu->super_surface, element_index);

	nvgpu_mem_rd_n(g, nvgpu_pmu_super_surface_mem(g,
		pmu, pmu->super_surface), fbq_msg_queue_ss_offset,
		buffer, size);
}

static int pmu_process_init_msg_fb(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct pmu_msg *msg)
{
	u32 tail = 0U;
	int err = 0;

	nvgpu_log_fn(g, " ");

	g->ops.pmu.pmu_msgq_tail(pmu, &tail, QUEUE_GET);

	pmu_read_init_msg_fb(g, pmu, tail, PMU_MSG_HDR_SIZE,
		(void *)&msg->hdr);

	if (msg->hdr.unit_id != PMU_UNIT_INIT) {
		nvgpu_err(g, "FB MSG Q: expecting init msg");
		err = -EINVAL;
		goto exit;
	}

	pmu_read_init_msg_fb(g, pmu, tail, msg->hdr.size,
		(void *)&msg->hdr);

	if (msg->msg.init.msg_type != PMU_INIT_MSG_TYPE_PMU_INIT) {
		nvgpu_err(g, "FB MSG Q: expecting pmu init msg");
		err = -EINVAL;
		goto exit;
	}

	/* Queue is not yet constructed, so inline next element code here.*/
	tail++;
	if (tail >= NV_PMU_FBQ_MSG_NUM_ELEMENTS) {
		tail = 0U;
	}

	g->ops.pmu.pmu_msgq_tail(pmu, &tail, QUEUE_SET);

exit:
	return err;
}

static int pmu_process_init_msg_dmem(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct pmu_msg *msg)
{
	u32 tail = 0U;
	int err = 0;

	nvgpu_log_fn(g, " ");

	g->ops.pmu.pmu_msgq_tail(pmu, &tail, QUEUE_GET);

	err = nvgpu_falcon_copy_from_dmem(&pmu->flcn, tail,
		(u8 *)&msg->hdr, PMU_MSG_HDR_SIZE, 0);
	if (err != 0) {
		nvgpu_err(g, "PMU falcon DMEM copy failed");
		goto exit;
	}
	if (msg->hdr.unit_id != PMU_UNIT_INIT) {
		nvgpu_err(g, "expecting init msg");
		err = -EINVAL;
		goto exit;
	}

	err = nvgpu_falcon_copy_from_dmem(&pmu->flcn, tail + PMU_MSG_HDR_SIZE,
		(u8 *)&msg->msg, (u32)msg->hdr.size - PMU_MSG_HDR_SIZE, 0);
	if (err != 0) {
		nvgpu_err(g, "PMU falcon DMEM copy failed");
		goto exit;
	}

	if (msg->msg.init.msg_type != PMU_INIT_MSG_TYPE_PMU_INIT) {
		nvgpu_err(g, "expecting pmu init msg");
		err = -EINVAL;
		goto exit;
	}

	tail += ALIGN(msg->hdr.size, PMU_DMEM_ALIGNMENT);
	g->ops.pmu.pmu_msgq_tail(pmu, &tail, QUEUE_SET);

exit:
	return err;
}

static int pmu_process_init_msg(struct nvgpu_pmu *pmu,
			struct pmu_msg *msg)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	struct pmu_fw_ver_ops *fw_ops = &g->pmu.fw.ops;
	union pmu_init_msg_pmu *init;
	struct pmu_sha1_gid_data gid_data;
	int err = 0;

	nvgpu_log_fn(g, " ");

	nvgpu_pmu_dbg(g, "init received\n");

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_PMU_RTOS_FBQ)) {
		err = pmu_process_init_msg_fb(g, pmu, msg);
	} else {
		err = pmu_process_init_msg_dmem(g, pmu, msg);
	}

	/* error check for above init message process*/
	if (err != 0) {
		goto exit;
	}

	init = fw_ops->get_init_msg_ptr(&(msg->msg.init));
	if (!pmu->gid_info.valid) {
		u32 *gid_hdr_data = &gid_data.signature;

		err = nvgpu_falcon_copy_from_dmem(&pmu->flcn,
			fw_ops->get_init_msg_sw_mngd_area_off(init),
			gid_data.sign_bytes,
			(u32)sizeof(struct pmu_sha1_gid_data), 0);
		if (err != 0) {
			nvgpu_err(g, "PMU falcon DMEM copy failed");
			goto exit;
		}

		pmu->gid_info.valid =
			(*gid_hdr_data == PMU_SHA1_GID_SIGNATURE);

		if (pmu->gid_info.valid) {

			WARN_ON(sizeof(pmu->gid_info.gid) !=
				sizeof(gid_data.gid));

			nvgpu_memcpy((u8 *)pmu->gid_info.gid,
				(u8 *)gid_data.gid,
				sizeof(pmu->gid_info.gid));
		}
	}

	err = nvgpu_pmu_queues_init(g, init, &pmu->queues,
			nvgpu_pmu_super_surface_mem(g, pmu,
			pmu->super_surface));
	if (err != 0) {
		return err;
	}

	nvgpu_pmu_dmem_allocator_init(g, pmu, &pmu->dmem, init);

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_PMU_SUPER_SURFACE)) {
		nvgpu_pmu_ss_create_ssmd_lookup_table(g,
			pmu, pmu->super_surface);
	}

	nvgpu_pmu_set_fw_ready(g, pmu, true);

	nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_INIT_RECEIVED, true);
exit:
	nvgpu_pmu_dbg(g, "init received end, err %x", err);
	return err;
}

int nvgpu_pmu_process_message(struct nvgpu_pmu *pmu)
{
	struct pmu_msg msg;
	int status;
	struct gk20a *g = gk20a_from_pmu(pmu);
	int err;

	if (nvgpu_can_busy(g) == 0) {
		return 0;
	}

	if (unlikely(!nvgpu_pmu_get_fw_ready(g, pmu))) {
		err = pmu_process_init_msg(pmu, &msg);
		if (err != 0) {
			return err;
		}

		nvgpu_pmu_lsfm_int_wpr_region(g, pmu, pmu->lsfm);

		if (nvgpu_is_enabled(g, NVGPU_PMU_PERFMON)) {
			nvgpu_pmu_perfmon_initialization(g, pmu,
						pmu->pmu_perfmon);
		}

		return 0;
	}

	while (pmu_read_message(pmu, PMU_MESSAGE_QUEUE, &msg, &status)) {

		if (nvgpu_can_busy(g) == 0) {
			return 0;
		}

		nvgpu_pmu_dbg(g, "read msg hdr: ");
		nvgpu_pmu_dbg(g, "unit_id = 0x%08x, size = 0x%08x",
			msg.hdr.unit_id, msg.hdr.size);
		nvgpu_pmu_dbg(g, "ctrl_flags = 0x%08x, seq_id = 0x%08x",
			msg.hdr.ctrl_flags, msg.hdr.seq_id);

		msg.hdr.ctrl_flags &= ~PMU_CMD_FLAGS_PMU_MASK;

		if (msg.hdr.ctrl_flags == PMU_CMD_FLAGS_EVENT) {
			pmu_handle_event(pmu, &msg);
		} else {
			pmu_response_handle(pmu, &msg);
		}
	}

	return 0;
}

static void pmu_rpc_handler(struct gk20a *g, struct pmu_msg *msg,
			    struct nv_pmu_rpc_header rpc,
			    struct rpc_handler_payload *rpc_payload)
{
	struct nvgpu_pmu *pmu = &g->pmu;

	switch (msg->hdr.unit_id) {
	case PMU_UNIT_ACR:
		nvgpu_pmu_lsfm_rpc_handler(g, rpc_payload);
		break;
	case PMU_UNIT_PERFMON_T18X:
	case PMU_UNIT_PERFMON:
		nvgpu_pmu_perfmon_rpc_handler(g, pmu, &rpc, rpc_payload);
		break;
	case PMU_UNIT_VOLT:
		if (pmu->volt_rpc_handler != NULL) {
			pmu->volt_rpc_handler(g, &rpc);
		}
		break;
	case PMU_UNIT_CLK:
		nvgpu_pmu_dbg(g, "reply PMU_UNIT_CLK");
		break;
	case PMU_UNIT_PERF:
		nvgpu_pmu_dbg(g, "reply PMU_UNIT_PERF");
		break;
	case PMU_UNIT_THERM:
		if (pmu->therm_event_handler != NULL) {
			pmu->therm_event_handler(g, pmu, msg, &rpc);
		}
		break;
	default:
		nvgpu_err(g, " Invalid RPC response, stats 0x%x",
			rpc.flcn_status);
		break;
	}
}

void nvgpu_pmu_rpc_handler(struct gk20a *g, struct pmu_msg *msg,
		void *param, u32 status)
{
	struct nv_pmu_rpc_header rpc;
	struct rpc_handler_payload *rpc_payload =
		(struct rpc_handler_payload *)param;

	if (nvgpu_can_busy(g) == 0) {
		return;
	}

	(void) memset(&rpc, 0, sizeof(struct nv_pmu_rpc_header));
	nvgpu_memcpy((u8 *)&rpc, (u8 *)rpc_payload->rpc_buff,
		sizeof(struct nv_pmu_rpc_header));

	if (rpc.flcn_status != 0U) {
		nvgpu_err(g,
			"failed RPC response, unit-id=0x%x, func=0x%x, status=0x%x",
			rpc.unit_id, rpc.function, rpc.flcn_status);
		goto exit;
	}

	pmu_rpc_handler(g, msg, rpc, rpc_payload);

exit:
	rpc_payload->complete = true;

	/* free allocated memory */
	if (rpc_payload->is_mem_free_set) {
		nvgpu_kfree(g, rpc_payload);
	}
}

void pmu_wait_message_cond(struct nvgpu_pmu *pmu, u32 timeout_ms,
			void *var, u8 val)
{
	struct gk20a *g = gk20a_from_pmu(pmu);

	if (nvgpu_pmu_wait_fw_ack_status(g, pmu, timeout_ms, var, val) != 0) {
		nvgpu_err(g, "PMU wait timeout expired.");
	}
}