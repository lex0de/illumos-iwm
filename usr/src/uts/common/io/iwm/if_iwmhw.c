/* BEGIN CSTYLED */
/*	$OpenBSD: if_iwm.c,v 1.419 2025/12/01 16:30:46 stsp Exp $	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* END CSTYLED */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 * 8260 INIT-only transport derived from OpenBSD sys/dev/pci/if_iwm.c,
 * 0efabb066d34187a404f31d303b3b97103df1117, BSD licence option.
 * Native DDI ownership, bounded completion waits and no wireless-stack calls.
 */
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/byteorder.h>
#include "if_iwmvar.h"

#define	IWM_RUN_MASK	(IWM_CSR_INT_BIT_FH_TX | IWM_CSR_INT_BIT_FH_RX | \
	IWM_CSR_INT_BIT_SW_RX | IWM_CSR_INT_BIT_RX_PERIODIC | \
	IWM_CSR_INT_BIT_ALIVE | IWM_CSR_INT_BIT_HW_ERR | IWM_CSR_INT_BIT_SW_ERR)
#define	IWM_NVM_READ_OPCODE	0
#define	IWM_NVM_LIMIT	32768
#define	IWM_NVM_CHUNK	2048
#define	IWM_WAIT_US	1000000

enum iwm_fw_state {
	IWM_FW_CLOSED, IWM_FW_LOADED, IWM_FW_PARSED, IWM_CARD_PREPARED,
	IWM_INIT_UPLOAD, IWM_INIT_ALIVE, IWM_NVM_READING, IWM_NVM_PARSED,
	IWM_DEVICE_STOPPING, IWM_DEVICE_STOPPED
};

/* Stable rejection identifiers; values are retained until runtime cleanup. */
enum iwm_proto_reason {
	IWM_PROTO_OK, IWM_PROTO_ALIVE, IWM_PROTO_RX_INDEX,
	IWM_PROTO_RX_SHORT, IWM_PROTO_RX_LENGTH, IWM_PROTO_COMMAND_ID,
	IWM_PROTO_NO_COMMAND, IWM_PROTO_QUEUE, IWM_PROTO_SEQUENCE,
	IWM_PROTO_NVM_SHORT, IWM_PROTO_RESPONSE_SIZE, IWM_PROTO_FH_TX,
	IWM_PROTO_NVM_SECTION, IWM_PROTO_NVM_OFFSET, IWM_PROTO_NVM_STATUS,
	IWM_PROTO_NVM_COUNT, IWM_PROTO_NVM_LENGTH, IWM_PROTO_NVM_ZERO,
	IWM_PROTO_COMMAND_GROUP
};

struct iwm_proto_diag {
	enum iwm_proto_reason reason;
	uint32_t interrupt;
	uint32_t fh;
	uint32_t raw;
	size_t length;
	size_t available;
	size_t payload;
	uint_t rxcur;
	uint_t rxhw;
	uint_t code;
	uint_t sequence;
	uint_t expected_sequence;
	uint_t section;
	uint_t offset;
	uint_t requested;
	uint_t actual_section;
	uint_t actual_offset;
	uint_t count;
	uint_t status;
	boolean_t packet_valid;
	boolean_t nvm_valid;
	boolean_t pending;
};

/* These pure checks preserve the existing transport acceptance conditions. */
static enum iwm_proto_reason
iwm_packet_check(size_t length, size_t available)
{
	if (length < 4)
		return (IWM_PROTO_RX_SHORT);
	if (length > available)
		return (IWM_PROTO_RX_LENGTH);
	return (IWM_PROTO_OK);
}

static enum iwm_proto_reason
iwm_command_check(uint_t code, boolean_t pending, uint_t sequence,
    uint_t expected, size_t length)
{
	if ((code & 0xff) != IWM_NVM_ACCESS_CMD)
		return (IWM_PROTO_COMMAND_ID);
	if ((code >> 8) != 0)
		return (IWM_PROTO_COMMAND_GROUP);
	if (!pending)
		return (IWM_PROTO_NO_COMMAND);
	if ((sequence >> 8) != (expected >> 8))
		return (IWM_PROTO_QUEUE);
	if ((sequence & 0xff) != (expected & 0xff))
		return (IWM_PROTO_SEQUENCE);
	if (length < sizeof (struct iwm_nvm_access_resp))
		return (IWM_PROTO_NVM_SHORT);
	if (length > IWM_NVM_CHUNK + 32)
		return (IWM_PROTO_RESPONSE_SIZE);
	return (IWM_PROTO_OK);
}

static enum iwm_proto_reason
iwm_nvm_check(const struct iwm_proto_diag *d)
{
	if (d->payload < 8)
		return (IWM_PROTO_NVM_SHORT);
	/* A failed firmware read has no success metadata or data body. */
	if (d->status != 0)
		return (IWM_PROTO_NVM_STATUS);
	if (d->actual_section != d->section)
		return (IWM_PROTO_NVM_SECTION);
	if (d->actual_offset != d->offset)
		return (IWM_PROTO_NVM_OFFSET);
	if (d->count == 0)
		return (IWM_PROTO_NVM_ZERO);
	if (d->count > d->requested)
		return (IWM_PROTO_NVM_COUNT);
	if (d->count > d->payload - 8)
		return (IWM_PROTO_NVM_LENGTH);
	return (IWM_PROTO_OK);
}

struct iwm_runtime {
	struct iwm_proto_diag diagnostic;
	struct iwm_proto_diag first_error;
	struct iwm_proto_diag response_diagnostic;
	enum iwm_fw_state state;
	kcondvar_t cv;
	boolean_t touched;
	boolean_t stopped;
	boolean_t stop_failed;
	boolean_t rx_started;
	boolean_t tx_started;
	boolean_t published;
	boolean_t alive;
	boolean_t chunk_done;
	boolean_t command_done;
	boolean_t command_pending;
	int error;
	uint32_t mask;
	uint32_t causes;
	uint_t interrupt_count;
	uint32_t fh_causes;
	uint32_t sched_base;
	uint32_t hw_rev;
	uint32_t alive_data[32];
	size_t alive_len;
	uint_t cmdqid;
	uint_t cmdcur;
	uint_t rxcur;
	uint_t nic_locks;
	uint8_t response[IWM_NVM_CHUNK + 32];
	size_t response_len;
	struct iwm_dma_info transfer;
	struct iwm_dma_info scheduler;
	struct iwm_dma_info tx[IWM_MAX_QUEUES];
	struct iwm_dma_info commands;
	struct iwm_dma_info rx[IWM_RX_RING_COUNT];
	uint8_t rx_pre[IWM_RX_RING_COUNT][sizeof (struct iwm_nvm_access_resp)];
	size_t rx_pre_offset[IWM_RX_RING_COUNT];
	uint32_t rx_generation[IWM_RX_RING_COUNT];
	uint8_t *nvm[IWM_NVM_NUM_OF_SECTIONS];
	size_t nvm_len[IWM_NVM_NUM_OF_SECTIONS];
	uint8_t mac[6];
	uint16_t nvm_version;
	uint32_t radio_cfg;
	uint32_t sku;
	uint8_t tx_ant;
	uint8_t rx_ant;
	uint16_t channels[51];
	uint16_t lar;
};

static uint16_t iwm_u16(const uint8_t *);
static uint32_t iwm_u32(const uint8_t *);

static void
iwm_proto_report(struct iwm_softc *sc, const struct iwm_proto_diag *d)
{
	static const char * const names[] = {
		"ok", "alive-layout", "rx-index", "rx-short", "rx-length",
		"command-id", "no-command", "command-queue", "command-index",
		"nvm-short", "response-size", "fh-tx-state", "nvm-section",
		"nvm-offset", "nvm-status", "nvm-count", "nvm-length",
		"nvm-zero", "command-group"
	};

	dev_err(sc->dip, CE_NOTE, "!iwm protocol reason=%s(%u) "
	    "csr=%08x fh=%08x rx=%u/%u raw=%08x length=%lu available=%lu",
	    names[d->reason], d->reason, d->interrupt, d->fh,
	    d->rxcur, d->rxhw, d->raw, (ulong_t)d->length,
	    (ulong_t)d->available);
	dev_err(sc->dip, CE_NOTE, "!iwm protocol packet-valid=%u "
	    "code=%04x sequence=%04x q=%u idx=%u expected-code=%02x "
	    "expected-sequence=%04x pending=%u payload=%lu",
	    d->packet_valid, d->code, d->sequence, d->sequence >> 8,
	    d->sequence & 0xff, IWM_NVM_ACCESS_CMD, d->expected_sequence,
	    d->pending, (ulong_t)d->payload);
	dev_err(sc->dip, CE_NOTE, "!iwm protocol nvm-header-valid=%u "
	    "section=%u/%u offset=%u/%u count=%u/requested=%u status=%u",
	    d->nvm_valid, d->actual_section, d->section,
	    d->actual_offset, d->offset, d->count, d->requested, d->status);
}

static void
iwm_proto_error(struct iwm_softc *sc, enum iwm_proto_reason reason)
{
	struct iwm_runtime *r = sc->run;

	if (r->first_error.reason == IWM_PROTO_OK) {
		r->diagnostic.reason = reason;
		r->first_error = r->diagnostic;
		iwm_proto_report(sc, &r->first_error);
	}
	r->error = EPROTO;
}

static uint32_t
iwm_rd(struct iwm_softc *sc, uint_t reg)
{
	uint32_t value = 0xffffffff;

	(void) iwm_reg_read(sc, reg, &value);
	return (value);
}

static void
iwm_wr(struct iwm_softc *sc, uint_t reg, uint32_t value)
{
	VERIFY0(iwm_reg_write(sc, reg, value));
}

static void
iwm_bits(struct iwm_softc *sc, uint_t reg, uint32_t set, uint32_t clear)
{
	iwm_wr(sc, reg, (iwm_rd(sc, reg) & ~clear) | set);
}

static void
iwm_wr8(struct iwm_softc *sc, uint_t reg, uint8_t value)
{
	ASSERT(reg < sc->regsize);
	ddi_put8(sc->regh, (uint8_t *)(sc->regs + reg), value);
}

static int
iwm_poll(struct iwm_softc *sc, uint_t reg, uint32_t mask,
    uint32_t wanted, uint_t usec)
{
	uint_t n;

	for (n = 0; n <= usec; n += 10) {
		if ((iwm_rd(sc, reg) & mask) == wanted)
			return (0);
		drv_usecwait(10);
	}
	return (ETIMEDOUT);
}

/* All peripheral windows and lifecycle state serialize under sc->lock. */
static int
iwm_nic_lock(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;

	ASSERT(MUTEX_HELD(&sc->lock));
	if (r->nic_locks != 0) {
		r->nic_locks++;
		return (0);
	}
	iwm_bits(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ, 0);
	drv_usecwait(2);
	if (iwm_poll(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
	    IWM_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP,
	    IWM_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN, 150000) != 0) {
		iwm_bits(sc, IWM_CSR_GP_CNTRL, 0,
		    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
		return (ETIMEDOUT);
	}
	r->nic_locks = 1;
	return (0);
}

static void
iwm_nic_unlock(struct iwm_softc *sc)
{
	ASSERT(sc->run->nic_locks != 0);
	if (--sc->run->nic_locks == 0)
		iwm_bits(sc, IWM_CSR_GP_CNTRL, 0,
		    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}

static uint32_t
iwm_prph_read(struct iwm_softc *sc, uint32_t addr)
{
	ASSERT(sc->run->nic_locks != 0);
	iwm_wr(sc, IWM_HBUS_TARG_PRPH_RADDR, (addr & 0xfffff) | (3 << 24));
	return (iwm_rd(sc, IWM_HBUS_TARG_PRPH_RDAT));
}

static void
iwm_prph_write(struct iwm_softc *sc, uint32_t addr, uint32_t value)
{
	ASSERT(sc->run->nic_locks != 0);
	iwm_wr(sc, IWM_HBUS_TARG_PRPH_WADDR, (addr & 0xfffff) | (3 << 24));
	iwm_wr(sc, IWM_HBUS_TARG_PRPH_WDAT, value);
}

static void
iwm_prph_bits(struct iwm_softc *sc, uint32_t addr, uint32_t set,
    uint32_t clear)
{
	iwm_prph_write(sc, addr, (iwm_prph_read(sc, addr) & ~clear) | set);
}

static void
iwm_mem_zero(struct iwm_softc *sc, uint32_t addr, size_t bytes)
{
	size_t i;

	ASSERT(sc->run->nic_locks != 0);
	iwm_wr(sc, IWM_HBUS_TARG_MEM_WADDR, addr);
	for (i = 0; i < bytes; i += 4)
		iwm_wr(sc, IWM_HBUS_TARG_MEM_WDAT, 0);
}

static int
iwm_ready(struct iwm_softc *sc)
{
	iwm_bits(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY, 0);
	if (iwm_poll(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY, 50) != 0)
		return (ETIMEDOUT);
	iwm_bits(sc, IWM_CSR_MBOX_SET_REG, IWM_CSR_MBOX_SET_REG_OS_ALIVE, 0);
	return (0);
}

static int
iwm_prepare(struct iwm_softc *sc)
{
	uint_t i, elapsed = 0;

	sc->run->touched = B_TRUE;
	if (iwm_ready(sc) == 0)
		return (0);
	iwm_bits(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED, 0);
	drv_usecwait(1000);
	for (i = 0; i < 10; i++) {
		iwm_bits(sc, IWM_CSR_HW_IF_CONFIG_REG,
		    IWM_CSR_HW_IF_CONFIG_REG_PREPARE, 0);
		do {
			if (iwm_ready(sc) == 0)
				return (0);
			drv_usecwait(200);
			elapsed += 200;
		} while (elapsed < 150000);
		drv_usecwait(25000);
	}
	return (ETIMEDOUT);
}

static int
iwm_apm(struct iwm_softc *sc)
{
	uint16_t link;

	iwm_bits(sc, IWM_CSR_GIO_CHICKEN_BITS,
	    IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX, 0);
	iwm_bits(sc, IWM_CSR_DBG_HPET_MEM_REG, IWM_CSR_DBG_HPET_MEM_REG_VAL, 0);
	iwm_bits(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A, 0);
	link = pci_config_get16(sc->pcih, sc->pcie_cap + PCIE_LINKCTL);
	if (link & PCIE_LINKCTL_ASPM_CTL_L1)
		iwm_bits(sc, IWM_CSR_GIO_REG, IWM_CSR_GIO_REG_VAL_L0S_ENABLED,
		    0);
	else
		iwm_bits(sc, IWM_CSR_GIO_REG, 0,
		    IWM_CSR_GIO_REG_VAL_L0S_ENABLED);
	iwm_bits(sc, IWM_CSR_GP_CNTRL, IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE, 0);
	return (iwm_poll(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000));
}

static int
iwm_start(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint32_t step;
	int error;

	/* Preserve platform bus-master ownership; never turn it on here. */
	if (!(pci_config_get16(sc->pcih, PCI_CONF_COMM) & PCI_COMM_ME))
		return (ENOTSUP);
	if ((error = iwm_prepare(sc)) != 0)
		return (error);
	r->state = IWM_CARD_PREPARED;
	if (iwm_checkpoint(sc, "card-prepared") != 0)
		return (EIO);
	/* 8000 C-step discovery, deliberately omitted from passive attach. */
	r->hw_rev = (sc->hw_rev & 0xfff0) |
	    (IWM_CSR_HW_REV_STEP(sc->hw_rev << 2) << 2);
	iwm_bits(sc, IWM_CSR_GP_CNTRL, IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE, 0);
	drv_usecwait(2);
	if (iwm_poll(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000) != 0 ||
	    iwm_nic_lock(sc) != 0)
		return (ETIMEDOUT);
	iwm_prph_bits(sc, IWM_WFPM_CTRL_REG, IWM_ENABLE_WFPM, 0);
	step = iwm_prph_read(sc, IWM_AUX_MISC_REG);
	if (step == 0xffffffff || step == 0xa5a5a5a0) {
		iwm_nic_unlock(sc);
		return (EIO);
	}
	dev_err(sc->dip, CE_NOTE, "!iwm 8000 AUX=%08x", step);
	step = (step >> IWM_HW_STEP_LOCATION_BITS) & 0xf;
	if (step == 3)
		r->hw_rev = (r->hw_rev & 0xfffffff3) |
		    (IWM_SILICON_C_STEP << 2);
	iwm_nic_unlock(sc);
	dev_err(sc->dip, CE_NOTE, "!iwm adjusted HW_REV=%08x", r->hw_rev);
	iwm_wr(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	drv_usecwait(5000);
	if ((error = iwm_apm(sc)) != 0)
		return (error);
	return (iwm_checkpoint(sc, "reset-apm"));
}

static int
iwm_sync(struct iwm_dma_info *dma, uint_t direction)
{
	return (ddi_dma_sync(dma->dma_hdl, 0, dma->size, direction) ==
	    DDI_SUCCESS ? 0 : EIO);
}

static uint64_t
iwm_dma_addr(struct iwm_dma_info *dma)
{
	ASSERT(dma->bound && dma->cookie.dmac_laddress <= 0xfffffffffULL);
	return (dma->cookie.dmac_laddress);
}

/* No writes to unused queue producer pointers occur anywhere in this file. */
static int
iwm_queues_check(struct iwm_softc *sc, const char *boundary)
{
	struct iwm_runtime *r = sc->run;
	uint_t q, i;
	uint32_t rd, wr, status, base;
	int error = 0;

	if (iwm_nic_lock(sc) != 0)
		return (EBUSY);
	for (q = 0; q < IWM_MAX_QUEUES; q++) {
		base = iwm_rd(sc, IWM_FH_MEM_CBBC_QUEUE(q));
		if (base != iwm_dma_addr(&r->tx[q]) >> 8)
			error = EIO;
		rd = iwm_prph_read(sc, IWM_SCD_QUEUE_RDPTR(q));
		wr = iwm_prph_read(sc, IWM_SCD_QUEUE_WRPTR(q));
		status = iwm_prph_read(sc, IWM_SCD_QUEUE_STATUS_BITS(q));
		dev_err(sc->dip, CE_NOTE, "!iwm %s q%u rd=%08x wr=%08x "
		    "status=%08x", boundary, q, rd, wr, status);
		if (q == r->cmdqid)
			continue;
		if (rd != 0 || wr != 0 ||
		    (status & (1 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE)))
			error = EIO;
		if (iwm_sync(&r->tx[q], DDI_DMA_SYNC_FORCPU) != 0)
			error = EIO;
		for (i = 0; i < r->tx[q].size; i++) {
			if (r->tx[q].vaddr[i] != 0) {
				error = EIO;
				break;
			}
		}
		if (iwm_sync(&r->tx[q], DDI_DMA_SYNC_FORDEV) != 0)
			error = EIO;
	}
	iwm_nic_unlock(sc);
	if (error != 0) {
		dev_err(sc->dip, CE_WARN, "!iwm unused queue invariant failed");
		r->error = error;
		r->mask = 0;
		iwm_wr(sc, IWM_CSR_INT_MASK, 0);
	}
	return (error);
}

static int
iwm_transport_init(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint32_t phy = sc->fw.phy_config, mask, value;
	uint_t q;

	if (iwm_apm(sc) != 0 || iwm_nic_lock(sc) != 0)
		return (ETIMEDOUT);
	value = IWM_CSR_HW_REV_STEP(r->hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	value |= IWM_CSR_HW_REV_DASH(r->hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;
	value |= ((phy & IWM_FW_PHY_CFG_RADIO_TYPE) >>
	    IWM_FW_PHY_CFG_RADIO_TYPE_POS) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	value |= ((phy & IWM_FW_PHY_CFG_RADIO_STEP) >>
	    IWM_FW_PHY_CFG_RADIO_STEP_POS) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	value |= ((phy & IWM_FW_PHY_CFG_RADIO_DASH) >>
	    IWM_FW_PHY_CFG_RADIO_DASH_POS) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;
	mask = IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI;
	iwm_bits(sc, IWM_CSR_HW_IF_CONFIG_REG, value, mask);
	iwm_wr(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	if (iwm_poll(sc, IWM_FH_MEM_RSSR_RX_STATUS_REG,
	    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
	    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 10000) != 0) {
		iwm_nic_unlock(sc);
		return (ETIMEDOUT);
	}
	iwm_wr(sc, IWM_FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
	iwm_wr(sc, IWM_FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
	iwm_wr(sc, IWM_FH_RSCSR_CHNL0_RDPTR, 0);
	iwm_wr(sc, IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);
	iwm_wr(sc, IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG,
	    iwm_dma_addr(&sc->dma[2]) >> 8);
	iwm_wr(sc, IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG,
	    iwm_dma_addr(&sc->dma[3]) >> 4);
	r->rx_started = B_TRUE;
	iwm_wr(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG,
	    IWM_FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL |
	    IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY |
	    IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL |
	    (IWM_RX_RB_TIMEOUT << IWM_FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
	    IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K |
	    IWM_RX_QUEUE_SIZE_LOG << IWM_FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS);
	iwm_wr8(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_TIMEOUT_DEF);
	iwm_wr(sc, IWM_FH_RSCSR_CHNL0_WPTR, 8);
	iwm_prph_write(sc, IWM_SCD_TXFACT, 0);
	iwm_wr(sc, IWM_FH_KW_MEM_ADDR_REG, iwm_dma_addr(&sc->dma[0]) >> 4);
	for (q = 0; q < IWM_MAX_QUEUES; q++) {
		iwm_wr(sc, IWM_FH_MEM_CBBC_QUEUE(q),
		    iwm_dma_addr(&r->tx[q]) >> 8);
		if (iwm_checkpoint(sc, "TX-base-programmed") != 0) {
			iwm_nic_unlock(sc);
			return (EIO);
		}
	}
	r->published = B_TRUE;
	iwm_prph_bits(sc, IWM_SCD_GP_CTRL,
	    IWM_SCD_GP_CTRL_AUTO_ACTIVE_MODE |
	    IWM_SCD_GP_CTRL_ENABLE_31_QUEUES, 0);
	iwm_nic_unlock(sc);
	iwm_bits(sc, IWM_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff, 0);
	if (iwm_checkpoint(sc, "transport-programmed") != 0)
		return (EIO);
	return (iwm_queues_check(sc, "pre-INIT"));
}

static int
iwm_wait(struct iwm_softc *sc, boolean_t *done)
{
	struct iwm_runtime *r = sc->run;
	clock_t deadline = ddi_get_lbolt() + drv_usectohz(IWM_WAIT_US);

	ASSERT(MUTEX_HELD(&sc->lock));
	while (!*done && r->error == 0) {
		if (cv_timedwait(&r->cv, &sc->lock, deadline) < 0) {
			r->error = ETIMEDOUT;
			break;
		}
	}
	return (r->error);
}

static int
iwm_upload(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	struct iwm_fw_image *im = &sc->fw.image[IWM_FW_INIT];
	uint_t i, cpu = 0, bits = 1;
	size_t pos, length;
	uint32_t offset, status;
	uint64_t addr = iwm_dma_addr(&r->transfer);
	int error;

	r->state = IWM_INIT_UPLOAD;
	r->alive = B_FALSE;
	iwm_wr(sc, IWM_CSR_INT, 0xffffffff);
	iwm_wr(sc, IWM_CSR_UCODE_DRV_GP1_CLR,
	    IWM_CSR_UCODE_SW_BIT_RFKILL |
	    IWM_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
	iwm_wr(sc, IWM_CSR_INT, 0xffffffff);
	r->mask = IWM_CSR_INT_BIT_FH_TX | IWM_CSR_INT_BIT_HW_ERR |
	    IWM_CSR_INT_BIT_SW_ERR;
	iwm_wr(sc, IWM_CSR_INT_MASK, r->mask);
	iwm_wr(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	iwm_wr(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	if (iwm_nic_lock(sc) != 0)
		return (EBUSY);
	iwm_prph_write(sc, IWM_RELEASE_CPU_RESET, IWM_RELEASE_CPU_RESET_BIT);
	iwm_nic_unlock(sc);
	if (iwm_checkpoint(sc, "CPU-release") != 0)
		return (EIO);
	for (i = 0; i < im->count; i++) {
		struct iwm_fw_section *s = &im->section[i];

		if (s->offset == IWM_FW_CPU_SEPARATOR ||
		    s->offset == IWM_FW_PAGING_SEPARATOR) {
			if (iwm_nic_lock(sc) != 0)
				return (EBUSY);
			iwm_wr(sc, IWM_FH_UCODE_LOAD_STATUS,
			    cpu == 0 ? 0xffff : 0xffffffff);
			iwm_nic_unlock(sc);
			if (s->offset == IWM_FW_PAGING_SEPARATOR)
				break;
			cpu = 1;
			bits = 1;
			continue;
		}
		dev_err(sc->dip, CE_NOTE, "!iwm INIT section %u CPU%u "
		    "offset=%08x bytes=%lu", i, cpu + 1, s->offset,
		    (ulong_t)s->length);
		for (pos = 0; pos < s->length; pos += length) {
			length = MIN(s->length - pos, IWM_FH_MEM_TB_MAX_LENGTH);
			offset = s->offset + pos;
			bcopy(s->data + pos, r->transfer.vaddr, length);
			if (iwm_sync(&r->transfer, DDI_DMA_SYNC_FORDEV) != 0 ||
			    iwm_nic_lock(sc) != 0)
				return (EIO);
			if (offset >= IWM_FW_MEM_EXTENDED_START &&
			    offset <= IWM_FW_MEM_EXTENDED_END)
				iwm_prph_bits(sc, IWM_LMPM_CHICK,
				    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE, 0);
			r->chunk_done = B_FALSE;
			r->tx_started = B_TRUE;
			iwm_wr(sc,
			    IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
			    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
			iwm_wr(sc,
			    IWM_FH_SRVC_CHNL_SRAM_ADDR_REG(IWM_FH_SRVC_CHNL),
			    offset);
			iwm_wr(sc, IWM_FH_TFDIB_CTRL0_REG(IWM_FH_SRVC_CHNL),
			    (uint32_t)addr);
			iwm_wr(sc, IWM_FH_TFDIB_CTRL1_REG(IWM_FH_SRVC_CHNL),
			    ((addr >> 32) <<
			    IWM_FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) |
			    length);
			iwm_wr(sc,
			    IWM_FH_TCSR_CHNL_TX_BUF_STS_REG(IWM_FH_SRVC_CHNL),
			    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
			    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
			    IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);
			iwm_wr(sc,
			    IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
			    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
			    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE |
			    IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);
			iwm_nic_unlock(sc);
			error = iwm_wait(sc, &r->chunk_done);
			if (error != 0)
				return (error);
			if (iwm_nic_lock(sc) != 0)
				return (EBUSY);
			if (offset >= IWM_FW_MEM_EXTENDED_START &&
			    offset <= IWM_FW_MEM_EXTENDED_END)
				iwm_prph_bits(sc, IWM_LMPM_CHICK, 0,
				    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);
			iwm_nic_unlock(sc);
		}
		if (iwm_nic_lock(sc) != 0)
			return (EBUSY);
		status = iwm_rd(sc, IWM_FH_UCODE_LOAD_STATUS);
		iwm_wr(sc, IWM_FH_UCODE_LOAD_STATUS,
		    status | (bits << (cpu * 16)));
		bits = (bits << 1) | 1;
		iwm_nic_unlock(sc);
		if (iwm_checkpoint(sc, "INIT-section") != 0)
			return (EIO);
	}
	r->mask = IWM_RUN_MASK;
	iwm_wr(sc, IWM_CSR_INT_MASK, r->mask);
	dev_err(sc->dip, CE_NOTE, "!iwm INIT upload complete; ALIVE wait %u us",
	    IWM_WAIT_US);
	if ((error = iwm_wait(sc, &r->alive)) != 0)
		return (error);
	r->state = IWM_INIT_ALIVE;
	if (iwm_checkpoint(sc, "INIT-ALIVE") != 0)
		return (EIO);
	return (iwm_queues_check(sc, "after-ALIVE"));
}

static int
iwm_post_alive(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint_t q = r->cmdqid, ch;
	uint32_t base;

	if (iwm_nic_lock(sc) != 0)
		return (EBUSY);
	base = iwm_prph_read(sc, IWM_SCD_SRAM_BASE_ADDR);
	if (base != r->sched_base || base == 0 || (base & 3) != 0 ||
	    base > 0xffffffffU - IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND) {
		iwm_nic_unlock(sc);
		return (EIO);
	}
	/* Keep direct CSR MSI handling; no ICT architecture change. */
	iwm_mem_zero(sc, base + IWM_SCD_CONTEXT_MEM_LOWER_BOUND,
	    IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND -
	    IWM_SCD_CONTEXT_MEM_LOWER_BOUND);
	iwm_prph_write(sc, IWM_SCD_DRAM_BASE_ADDR,
	    iwm_dma_addr(&r->scheduler) >> 10);
	iwm_prph_write(sc, IWM_SCD_CHAINEXT_EN, 0);
	/* The only scheduler queue explicitly enabled is the command queue. */
	iwm_wr(sc, IWM_HBUS_TARG_WRPTR, q << 8);
	iwm_prph_write(sc, IWM_SCD_QUEUE_STATUS_BITS(q),
	    1 << IWM_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN);
	iwm_prph_bits(sc, IWM_SCD_AGGR_SEL, 0, 1U << q);
	iwm_prph_write(sc, IWM_SCD_QUEUE_RDPTR(q), 0);
	iwm_mem_zero(sc, base + IWM_SCD_CONTEXT_QUEUE_OFFSET(q), 4);
	iwm_wr(sc, IWM_HBUS_TARG_MEM_WADDR,
	    base + IWM_SCD_CONTEXT_QUEUE_OFFSET(q) + 4);
	iwm_wr(sc, IWM_HBUS_TARG_MEM_WDAT,
	    (IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) |
	    (IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS));
	iwm_prph_write(sc, IWM_SCD_QUEUE_STATUS_BITS(q),
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
	    (IWM_TX_FIFO_CMD << IWM_SCD_QUEUE_STTS_REG_POS_TXF) |
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_WSL) | IWM_SCD_QUEUE_STTS_REG_MSK);
	iwm_prph_bits(sc, IWM_SCD_EN_CTRL, 1U << q, 0);
	iwm_prph_write(sc, IWM_SCD_TXFACT, 0xff);
	for (ch = 0; ch < IWM_FH_TCSR_CHNL_NUM; ch++)
		iwm_wr(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(ch),
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);
	iwm_bits(sc, IWM_FH_TX_CHICKEN_BITS_REG,
	    IWM_FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN, 0);
	iwm_nic_unlock(sc);
	return (iwm_checkpoint(sc, "post-ALIVE"));
}

static uint16_t
iwm_u16(const uint8_t *p)
{
	return ((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t
iwm_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void
iwm_notification(struct iwm_softc *sc, const uint8_t *p, size_t length,
    const uint8_t *pre, size_t payload_offset, size_t pre_offset,
    uint32_t generation)
{
	struct iwm_runtime *r = sc->run;
	uint_t code = p[0] | (uint_t)p[1] << 8;
	uint_t idx = p[2], qid = p[3];
	const uint8_t *data = p + 4;
	size_t n = length - 4;
	uint_t i;
	enum iwm_proto_reason reason;
	struct iwm_proto_diag *d = &r->diagnostic;

	d->packet_valid = B_TRUE;
	d->code = code;
	d->sequence = qid << 8 | idx;
	d->expected_sequence = r->cmdqid << 8 | r->cmdcur;
	d->pending = r->command_pending;
	d->payload = n;
	/* Decode only a complete fixed header. */
	if (code == IWM_NVM_ACCESS_CMD &&
	    n >= sizeof (struct iwm_nvm_access_resp)) {
		dev_err(sc->dip, CE_NOTE, "!iwm nvm-wire rx=%u generation=%u "
		    "offset=%lu "
		    "header=%02x %02x %02x %02x payload=%02x %02x %02x %02x "
		    "%02x %02x %02x %02x", r->rxcur, generation,
		    (ulong_t)payload_offset, p[0], p[1], p[2], p[3], data[0],
		    data[1], data[2], data[3], data[4], data[5], data[6],
		    data[7]);
		dev_err(sc->dip, CE_NOTE, "!iwm nvm-pre rx=%u generation=%u "
		    "offset=%lu bytes=%02x %02x %02x %02x %02x %02x %02x %02x",
		    r->rxcur, generation, (ulong_t)pre_offset, pre[0], pre[1],
		    pre[2], pre[3], pre[4], pre[5], pre[6], pre[7]);
		d->nvm_valid = B_TRUE;
		d->actual_offset = iwm_u16(data);
		d->count = iwm_u16(data + 2);
		d->actual_section = iwm_u16(data + 4);
		d->status = iwm_u16(data + 6);
	}
	if (r->command_pending)
		iwm_proto_report(sc, d);

	if (code == IWM_ALIVE) {
		if (r->alive || r->state != IWM_INIT_UPLOAD ||
		    (n != sizeof (struct iwm_alive_resp_v1) &&
		    n != sizeof (struct iwm_alive_resp_v2) &&
		    n != sizeof (struct iwm_alive_resp_v3))) {
			iwm_proto_error(sc, IWM_PROTO_ALIVE);
			return;
		}
		r->alive_len = n;
		bcopy(data, r->alive_data, n);
		for (i = 0; i < n; i += 4)
			dev_err(sc->dip, CE_NOTE, "!iwm ALIVE word%u=%08x",
			    i / 4, iwm_u32(data + i));
		if (iwm_u16(data) != IWM_ALIVE_STATUS_OK) {
			r->error = EIO;
			return;
		}
		/* All three donor ALIVE layouts place SCD at byte 40. */
		r->sched_base = iwm_u32(data + 40);
		r->alive = B_TRUE;
		return;
	}
	if (code == IWM_MFUART_LOAD_NOTIFICATION) {
		dev_err(sc->dip, CE_NOTE, "!iwm MFUART notification bytes=%lu",
		    (ulong_t)n);
		return;
	}
	reason = iwm_command_check(code, r->command_pending,
	    qid << 8 | idx, r->cmdqid << 8 | r->cmdcur, n);
	if (reason != IWM_PROTO_OK) {
		iwm_proto_error(sc, reason);
		return;
	}
	bcopy(data, r->response, n);
	r->response_len = n;
	r->response_diagnostic = *d;
	r->command_done = B_TRUE;
}

static void
iwm_notifications(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint_t hw, count = 0;
	size_t off, length, advance;
	uint8_t *p;
	uint32_t raw;
	enum iwm_proto_reason reason;
	struct iwm_proto_diag *d = &r->diagnostic;

	d->packet_valid = B_FALSE;
	d->nvm_valid = B_FALSE;
	d->raw = 0;
	d->length = d->available = d->payload = 0;
	d->code = d->sequence = 0;
	d->actual_section = d->actual_offset = d->count = d->status = 0;
	d->rxcur = r->rxcur;
	if (iwm_sync(&sc->dma[3], DDI_DMA_SYNC_FORCPU) != 0) {
		r->error = EIO;
		return;
	}
	hw = iwm_u16((uint8_t *)sc->dma[3].vaddr) & 0xfff;
	d->rxhw = hw;
	if (hw >= IWM_RX_RING_COUNT) {
		iwm_proto_error(sc, IWM_PROTO_RX_INDEX);
		return;
	}
	while (r->rxcur != hw && count++ < IWM_RX_RING_COUNT && r->error == 0) {
		struct iwm_dma_info *dma = &r->rx[r->rxcur];
		size_t payload_offset, pre_offset, capture_offset = 0;
		boolean_t capture_valid = B_FALSE;
		uint32_t generation;

		if (iwm_sync(dma, DDI_DMA_SYNC_FORCPU) != 0) {
			r->error = EIO;
			return;
		}
		p = (uint8_t *)dma->vaddr;
		for (off = 0; off + 8 <= IWM_RBUF_SIZE; off += advance) {
			raw = iwm_u32(p + off);
			if (raw == IWM_FH_RSCSR_FRAME_INVALID ||
			    iwm_u32(p + off + 4) == 0)
				break;
			length = raw & IWM_FH_RSCSR_FRAME_SIZE_MSK;
			d->packet_valid = B_FALSE;
			d->nvm_valid = B_FALSE;
			d->code = d->sequence = 0;
			d->payload = 0;
			d->actual_section = d->actual_offset = 0;
			d->count = d->status = 0;
			d->rxcur = r->rxcur;
			d->raw = raw;
			d->length = length;
			d->available = IWM_RBUF_SIZE - off - 4;
			reason = iwm_packet_check(length, d->available);
			if (reason != IWM_PROTO_OK) {
				iwm_proto_error(sc, reason);
				break;
			}
			pre_offset = r->rx_pre_offset[r->rxcur];
			generation = r->rx_generation[r->rxcur];
			payload_offset = off + 8;
			capture_offset = payload_offset;
			capture_valid = B_TRUE;
			iwm_notification(sc, p + off + 4, length,
			    r->rx_pre[r->rxcur], payload_offset, pre_offset,
			    generation);
			advance = P2ROUNDUP(length + 4,
			    IWM_FH_RSCSR_FRAME_ALIGN);
			if (r->error != 0 || advance > IWM_RBUF_SIZE - off)
				break;
		}
		if (capture_valid && capture_offset <= IWM_RBUF_SIZE -
		    sizeof (struct iwm_nvm_access_resp)) {
			r->rx_pre_offset[r->rxcur] = capture_offset;
			bcopy(p + capture_offset, r->rx_pre[r->rxcur],
			    sizeof (struct iwm_nvm_access_resp));
		}
		bzero(p, IWM_RBUF_SIZE);
		r->rx_generation[r->rxcur]++;
		if (iwm_sync(dma, DDI_DMA_SYNC_FORDEV) != 0)
			r->error = EIO;
		r->rxcur = (r->rxcur + 1) % IWM_RX_RING_COUNT;
	}
	if (iwm_sync(&sc->dma[3], DDI_DMA_SYNC_FORDEV) != 0)
		r->error = EIO;
	if (r->error == 0)
		iwm_wr(sc, IWM_FH_RSCSR_CHNL0_WPTR,
		    ((hw == 0 ? IWM_RX_RING_COUNT : hw) - 1) & ~7);
}

/* Called by the single MSI handler with sc->lock held; never sleeps. */
uint_t
iwm_active_intr(struct iwm_softc *sc, uint32_t causes, uint32_t fh)
{
	struct iwm_runtime *r = sc->run;
	uint32_t rx = IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX |
	    IWM_CSR_INT_BIT_RX_PERIODIC;

	ASSERT(MUTEX_HELD(&sc->lock));
	if (causes == 0 && fh == 0) {
		iwm_wr(sc, IWM_CSR_INT_MASK, r->mask);
		return (DDI_INTR_UNCLAIMED);
	}
	r->causes |= causes;
	r->fh_causes |= fh;
	r->diagnostic.interrupt = causes;
	r->diagnostic.fh = fh;
	if (++r->interrupt_count <= 1024)
		dev_err(sc->dip, CE_NOTE,
		    "!iwm firmware interrupt csr=%08x fh=%08x", causes, fh);
	else
		r->error = EOVERFLOW;
	iwm_wr(sc, IWM_CSR_INT, causes);
	iwm_wr(sc, IWM_CSR_FH_INT_STATUS, fh);
	if (causes == 0xffffffff || (causes & ~IWM_RUN_MASK) != 0 ||
	    (fh & ~(IWM_CSR_FH_INT_TX_MASK | IWM_CSR_FH_INT_RX_MASK)) != 0 ||
	    (causes & (IWM_CSR_INT_BIT_HW_ERR | IWM_CSR_INT_BIT_SW_ERR))) {
		r->error = EIO;
	} else {
		if (causes & IWM_CSR_INT_BIT_FH_TX) {
			if (r->state != IWM_INIT_UPLOAD || r->chunk_done)
				iwm_proto_error(sc, IWM_PROTO_FH_TX);
			else
				r->chunk_done = B_TRUE;
		}
		if (causes & rx) {
			iwm_wr8(sc, IWM_CSR_INT_PERIODIC_REG,
			    IWM_CSR_INT_PERIODIC_DIS);
			if (causes & (IWM_CSR_INT_BIT_FH_RX |
			    IWM_CSR_INT_BIT_SW_RX))
				iwm_wr8(sc, IWM_CSR_INT_PERIODIC_REG,
				    IWM_CSR_INT_PERIODIC_ENA);
			iwm_notifications(sc);
		}
	}
	if (r->error != 0)
		r->mask = 0;
	iwm_wr(sc, IWM_CSR_INT_MASK, r->mask);
	cv_broadcast(&r->cv);
	return (DDI_INTR_CLAIMED);
}

static int
iwm_nvm_chunk(struct iwm_softc *sc, uint_t section, uint_t offset,
    uint_t requested, uint_t *received)
{
	struct iwm_runtime *r = sc->run;
	struct iwm_device_cmd *cmd = (void *)r->commands.vaddr;
	struct iwm_tfd *desc = (void *)r->tx[r->cmdqid].vaddr;
	struct iwm_nvm_access_cmd *nvm;
	enum iwm_proto_reason reason;
	uint64_t addr;
	uint32_t low;
	uint_t n;
	uint16_t bytes;
	struct iwm_agn_scd_bc_tbl *bc = (void *)r->scheduler.vaddr;
	int error;

	if (section >= IWM_NVM_NUM_OF_SECTIONS || offset >= IWM_NVM_LIMIT ||
	    requested > IWM_NVM_CHUNK || requested > IWM_NVM_LIMIT - offset ||
	    r->state != IWM_NVM_READING || r->error != 0)
		return (EINVAL);
	cmd += r->cmdcur;
	desc += r->cmdcur;
	bzero(cmd, sizeof (*cmd));
	bzero(desc, sizeof (*desc));
	cmd->hdr.code = IWM_NVM_ACCESS_CMD;
	cmd->hdr.qid = r->cmdqid;
	cmd->hdr.idx = r->cmdcur;
	nvm = (void *)cmd->data;
	nvm->op_code = IWM_NVM_READ_OPCODE;
	nvm->type = LE_16(section);
	/* Preserve the offset instead of the donor's unconditional zero. */
	nvm->offset = LE_16(offset);
	nvm->length = LE_16(requested);
	dev_err(sc->dip, CE_NOTE, "!iwm nvm-request section=%u bytes=%02x "
	    "%02x %02x %02x %02x %02x %02x %02x", section,
	    ((uint8_t *)nvm)[0], ((uint8_t *)nvm)[1], ((uint8_t *)nvm)[2],
	    ((uint8_t *)nvm)[3], ((uint8_t *)nvm)[4], ((uint8_t *)nvm)[5],
	    ((uint8_t *)nvm)[6], ((uint8_t *)nvm)[7]);
	addr = iwm_dma_addr(&r->commands) + r->cmdcur * sizeof (*cmd);
	low = LE_32((uint32_t)addr);
	bcopy(&low, &desc->tbs[0].lo, sizeof (low));
	desc->tbs[0].hi_n_len = LE_16((addr >> 32) |
	    ((sizeof (cmd->hdr) + sizeof (*nvm)) << 4));
	desc->num_tbs = 1;
	/* Donor command accounting includes CRC and delimiter only. */
	bytes = IWM_TX_CRC_SIZE + IWM_TX_DELIMITER_SIZE;
	if (sc->fw.flags & IWM_UCODE_TLV_FLAGS_DW_BC_TABLE)
		bytes /= 4;
	bc[r->cmdqid].tfd_offset[r->cmdcur] = LE_16(bytes);
	if (r->cmdcur < IWM_TFD_QUEUE_SIZE_BC_DUP)
		bc[r->cmdqid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + r->cmdcur] =
		    LE_16(bytes);
	if (iwm_sync(&r->scheduler, DDI_DMA_SYNC_FORDEV) != 0)
		return (EIO);
	if (iwm_sync(&r->commands, DDI_DMA_SYNC_FORDEV) != 0 ||
	    iwm_sync(&r->tx[r->cmdqid], DDI_DMA_SYNC_FORDEV) != 0)
		return (EIO);
	r->command_done = B_FALSE;
	r->command_pending = B_TRUE;
	r->response_len = 0;
	r->diagnostic.section = section;
	r->diagnostic.offset = offset;
	r->diagnostic.requested = requested;
	r->diagnostic.expected_sequence = r->cmdqid << 8 | r->cmdcur;
	r->diagnostic.pending = B_TRUE;
	dev_err(sc->dip, CE_NOTE, "!iwm NVM request section=%u offset=%u "
	    "length=%u opcode=%02x group=0 version=0 target=%u operation=%u "
	    "q=%u idx=%u sequence=%04x producer=%u", section, offset,
	    requested, cmd->hdr.code, nvm->target, nvm->op_code,
	    r->cmdqid, r->cmdcur, r->diagnostic.expected_sequence,
	    (r->cmdcur + 1) % IWM_TX_RING_COUNT);
	/* No external input or packet can select a queue. */
	iwm_wr(sc, IWM_HBUS_TARG_WRPTR,
	    r->cmdqid << 8 | ((r->cmdcur + 1) % IWM_TX_RING_COUNT));
	error = iwm_wait(sc, &r->command_done);
	r->command_pending = B_FALSE;
	if (error != 0)
		return (error);
	r->cmdcur = (r->cmdcur + 1) % IWM_TX_RING_COUNT;
	r->diagnostic = r->response_diagnostic;
	r->diagnostic.actual_offset = iwm_u16(r->response);
	r->diagnostic.count = iwm_u16(r->response + 2);
	r->diagnostic.actual_section = iwm_u16(r->response + 4);
	r->diagnostic.status = iwm_u16(r->response + 6);
	r->diagnostic.payload = r->response_len;
	r->diagnostic.nvm_valid = B_TRUE;
	reason = iwm_nvm_check(&r->diagnostic);
	if (reason == IWM_PROTO_NVM_STATUS) {
		/* Failed sections are absent; fields are not success data. */
		r->diagnostic.reason = reason;
		iwm_proto_report(sc, &r->diagnostic);
		dev_err(sc->dip, CE_NOTE, "!iwm NVM section%u read failed "
		    "status=%u; section remains absent", section,
		    r->diagnostic.status);
		return (ENOENT);
	}
	if (reason != IWM_PROTO_OK) {
		iwm_proto_error(sc, reason);
		return (EPROTO);
	}
	n = r->diagnostic.count;
	bcopy(r->response + 8, r->nvm[section] + offset, n);
	*received = n;
	return (iwm_checkpoint(sc, "NVM-chunk"));
}

static boolean_t
iwm_mac_valid(const uint8_t *mac)
{
	uint_t i;
	uint8_t any = 0;
	static const uint8_t reserved[6] = { 2, 0xcc, 0xaa, 0xff, 0xee, 0 };

	for (i = 0; i < 6; i++)
		any |= mac[i];
	return (any != 0 && !(mac[0] & 1) && bcmp(mac, reserved, 6) != 0);
}

static boolean_t
iwm_nvm_sections_valid(const struct iwm_runtime *r)
{
	return (r->nvm_len[1] >= 8 && r->nvm_len[3] >= 102 &&
	    r->nvm_len[12] >= 8 &&
	    (r->nvm_len[10] >= 8 || r->nvm_len[11] >= 8));
}

static int
iwm_nvm_parse(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint_t i, laroff;
	uint32_t a, b;
	static const uint8_t channels[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
		96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
		149, 153, 157, 161, 165, 169, 173, 177, 181
	};

	if (!iwm_nvm_sections_valid(r)) {
		dev_err(sc->dip, CE_WARN, "!iwm NVM mandatory sections missing "
		    "sw=%lu regulatory=%lu phy-sku=%lu hw-8000=%lu "
		    "mac-override=%lu", (ulong_t)r->nvm_len[1],
		    (ulong_t)r->nvm_len[3], (ulong_t)r->nvm_len[12],
		    (ulong_t)r->nvm_len[10], (ulong_t)r->nvm_len[11]);
		return (EINVAL);
	}
	r->nvm_version = iwm_u16(r->nvm[1]);
	laroff = r->nvm_version < 0xe39 ? IWM_NVM_LAR_OFFSET_8000_OLD :
	    IWM_NVM_LAR_OFFSET_8000;
	if (laroff * 2 + 2 > r->nvm_len[3])
		return (EINVAL);
	r->lar = iwm_u16(r->nvm[3] + laroff * 2);
	r->radio_cfg = iwm_u32(r->nvm[12]);
	r->sku = iwm_u32(r->nvm[12] + 4);
	r->tx_ant = IWM_NVM_RF_CFG_TX_ANT_MSK_8000(r->radio_cfg);
	r->rx_ant = IWM_NVM_RF_CFG_RX_ANT_MSK_8000(r->radio_cfg);
	if (r->nvm_len[11] >= 8)
		bcopy(r->nvm[11] + 2, r->mac, 6);
	if (!iwm_mac_valid(r->mac)) {
		if (r->nvm_len[10] == 0 || iwm_nic_lock(sc) != 0)
			return (EINVAL);
		a = iwm_prph_read(sc, IWM_WFMP_MAC_ADDR_0);
		b = iwm_prph_read(sc, IWM_WFMP_MAC_ADDR_1);
		iwm_nic_unlock(sc);
		r->mac[0] = a >> 24;
		r->mac[1] = a >> 16;
		r->mac[2] = a >> 8;
		r->mac[3] = a;
		r->mac[4] = b >> 8;
		r->mac[5] = b;
	}
	if (!iwm_mac_valid(r->mac) || r->tx_ant == 0 || r->rx_ant == 0)
		return (EINVAL);
	dev_err(sc->dip, CE_NOTE, "!iwm NVM version=%04x radio=%08x sku=%08x "
	    "TXant=%x RXant=%x bands24=%u bands5=%u LAR=%04x hwaddrs=%u",
	    r->nvm_version, r->radio_cfg, r->sku, r->tx_ant, r->rx_ant,
	    !!(r->sku & IWM_NVM_SKU_CAP_BAND_24GHZ),
	    !!(r->sku & IWM_NVM_SKU_CAP_BAND_52GHZ), r->lar,
	    iwm_u16(r->nvm[1] + 6));
	dev_err(sc->dip, CE_NOTE, "!iwm NVM MAC %02x:%02x:%02x:%02x:%02x:%02x",
	    r->mac[0], r->mac[1], r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
	for (i = 0; i < sizeof (channels); i++) {
		r->channels[i] = iwm_u16(r->nvm[3] + 2 * i);
		dev_err(sc->dip, CE_NOTE, "!iwm NVM channel%u flags=%04x "
		    "observation only", channels[i], r->channels[i]);
	}
	r->state = IWM_NVM_PARSED;
	return (iwm_checkpoint(sc, "NVM-parsed"));
}

static int
iwm_nvm(struct iwm_softc *sc)
{
	static const uint_t sections[] = { 0, 1, 3, 4, 5, 8, 10, 11, 12 };
	struct iwm_runtime *r = sc->run;
	uint_t i, s, offset, n, want;
	int error;

	r->state = IWM_NVM_READING;
	for (i = 0; i < sizeof (sections) / sizeof (sections[0]); i++) {
		s = sections[i];
		r->nvm_len[s] = 0;
		for (offset = 0; offset < IWM_NVM_LIMIT; offset += n) {
			want = MIN(IWM_NVM_CHUNK, IWM_NVM_LIMIT - offset);
			error = iwm_nvm_chunk(sc, s, offset, want, &n);
			if (error == ENOENT)
					/* Firmware rejected this section. */
					break;
			if (error != 0)
				return (error);
			r->nvm_len[s] = offset + n;
			if (n < want)
				break;
		}
		dev_err(sc->dip, CE_NOTE, "!iwm NVM section%u length=%lu",
		    s, (ulong_t)r->nvm_len[s]);
		if (iwm_checkpoint(sc, "NVM-section") != 0 ||
		    iwm_queues_check(sc, "NVM-section") != 0)
			return (EIO);
	}
	if ((error = iwm_nvm_parse(sc)) != 0)
		return (error);
	return (iwm_queues_check(sc, "after-NVM"));
}

/*
 * Stop every enabled channel before any published DMA mapping can be freed.
 * Timeout is a failure, never permission to release memory still owned by NIC.
 */
static int
iwm_device_stop(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint_t ch;
	uint32_t mask;
	int error = 0;

	if (r->stop_failed)
		return (EIO);
	if (!r->touched || r->stopped)
		return (0);
	if (r->published && iwm_queues_check(sc, "pre-stop") != 0)
		r->error = EIO;
	r->state = IWM_DEVICE_STOPPING;
	r->mask = 0;
	iwm_wr(sc, IWM_CSR_INT_MASK, 0);
	(void) iwm_rd(sc, IWM_CSR_INT_MASK);
	if (r->tx_started || r->rx_started) {
		if (iwm_nic_lock(sc) != 0) {
			error = EIO;
		} else {
			iwm_prph_write(sc, IWM_SCD_TXFACT, 0);
			for (ch = 0; ch < IWM_FH_TCSR_CHNL_NUM; ch++)
				iwm_wr(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(ch),
				    0);
			for (ch = 0; ch < IWM_FH_TCSR_CHNL_NUM; ch++) {
				mask =
				    IWM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch);
				if (iwm_poll(sc, IWM_FH_TSSR_TX_STATUS_REG,
				    mask, mask, 4000) != 0)
					error = ETIMEDOUT;
			}
			iwm_wr(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
			if (iwm_poll(sc, IWM_FH_MEM_RSSR_RX_STATUS_REG,
			    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
			    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 10000) != 0)
				error = ETIMEDOUT;
			iwm_nic_unlock(sc);
		}
	}
	iwm_bits(sc, IWM_CSR_GP_CNTRL, 0,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	r->nic_locks = 0;
	iwm_bits(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED, 0);
	iwm_bits(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_PREPARE |
	    IWM_CSR_HW_IF_CONFIG_REG_ENABLE_PME, 0);
	drv_usecwait(1000);
	iwm_bits(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG, 0,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	drv_usecwait(5000);
	iwm_bits(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_STOP_MASTER, 0);
	if (iwm_poll(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100) != 0)
		error = ETIMEDOUT;
	iwm_bits(sc, IWM_CSR_GP_CNTRL, 0, IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	iwm_wr(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	r->command_pending = B_FALSE;
	r->alive = B_FALSE;
	drv_usecwait(5000);
	iwm_wr(sc, IWM_CSR_INT_MASK, 0);
	iwm_wr8(sc, IWM_CSR_INT_PERIODIC_REG, IWM_CSR_INT_PERIODIC_DIS);
	iwm_wr(sc, IWM_CSR_INT, 0xffffffff);
	iwm_wr(sc, IWM_CSR_FH_INT_STATUS, 0xffffffff);
	dev_err(sc->dip, CE_NOTE, "!iwm stop error=%d HW_REV=%08x "
	    "GP=%08x RESET=%08x MASK=%08x causes=%08x FH=%08x",
	    error, iwm_rd(sc, IWM_CSR_HW_REV), iwm_rd(sc, IWM_CSR_GP_CNTRL),
	    iwm_rd(sc, IWM_CSR_RESET), iwm_rd(sc, IWM_CSR_INT_MASK),
	    r->causes, r->fh_causes);
	if (error != 0) {
		r->stop_failed = B_TRUE;
		return (error);
	}
	r->stopped = B_TRUE;
	r->state = IWM_DEVICE_STOPPED;
	return (0);
}

/*
 * Quiesce cannot take locks or tear down mappings. Normally the INIT session
 * has already stopped before attach completes. For an interrupted session,
 * request master stop and reset, without treating a timeout as success.
 */
int
iwm_run_quiesce(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	int error;

	if (r == NULL || !r->touched || r->stopped)
		return (0);
	iwm_wr(sc, IWM_CSR_INT_MASK, 0);
	iwm_bits(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_STOP_MASTER, 0);
	error = iwm_poll(sc, IWM_CSR_RESET,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	iwm_wr(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	(void) iwm_rd(sc, IWM_CSR_RESET);
	return (error);
}

/* Attach/detach thread: runtime resources precede passive cleanup. */
int
iwm_run_free(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	int i, error;

	if (r == NULL)
		return (0);
	mutex_enter(&sc->lock);
	error = iwm_device_stop(sc);
	mutex_exit(&sc->lock);
	if (iwm_intr_disable(sc) != 0 || error != 0)
		return (EIO);
	for (i = IWM_NVM_NUM_OF_SECTIONS - 1; i >= 0; i--) {
		if (r->nvm[i] != NULL) {
			kmem_free(r->nvm[i], IWM_NVM_LIMIT);
			r->nvm[i] = NULL;
		}
	}
	for (i = IWM_RX_RING_COUNT - 1; i >= 0; i--) {
		if (iwm_dma_free(&r->rx[i]) != 0)
			return (EIO);
	}
	if (iwm_dma_free(&r->commands) != 0)
		return (EIO);
	for (i = IWM_MAX_QUEUES - 1; i >= 0; i--) {
		if (iwm_dma_free(&r->tx[i]) != 0)
			return (EIO);
	}
	if (iwm_dma_free(&r->scheduler) != 0 || iwm_dma_free(&r->transfer) != 0)
		return (EIO);
	iwm_fw_free(&sc->fw);
	cv_destroy(&r->cv);
	kmem_free(r, sizeof (*r));
	sc->run = NULL;
	return (0);
}

static int
iwm_run_alloc(struct iwm_softc *sc)
{
	struct iwm_runtime *r = sc->run;
	uint32_t *desc = (void *)sc->dma[2].vaddr;
	uint_t i;

	if (iwm_dma_alloc(sc, &r->transfer, sc->cfg->fw_dma_size,
	    16, DDI_DMA_WRITE) != 0 ||
	    iwm_dma_alloc(sc, &r->scheduler,
	    IWM_MAX_QUEUES * sizeof (struct iwm_agn_scd_bc_tbl),
	    1024, DDI_DMA_RDWR) != 0)
		return (ENOMEM);
	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		if (iwm_dma_alloc(sc, &r->tx[i],
		    IWM_TX_RING_COUNT * sizeof (struct iwm_tfd),
		    256, DDI_DMA_RDWR) != 0)
			return (ENOMEM);
	}
	/* Only command storage exists. All other TX queues have empty TFDs. */
	if (iwm_dma_alloc(sc, &r->commands,
	    IWM_TX_RING_COUNT * sizeof (struct iwm_device_cmd),
	    4, DDI_DMA_WRITE) != 0)
		return (ENOMEM);
	for (i = 0; i < IWM_RX_RING_COUNT; i++) {
		if (iwm_dma_alloc(sc, &r->rx[i], IWM_RBUF_SIZE,
		    256, DDI_DMA_READ) != 0)
			return (ENOMEM);
		r->rx_pre_offset[i] = 8;
		desc[i] = LE_32((uint32_t)(iwm_dma_addr(&r->rx[i]) >> 8));
	}
	/* End the allocator's host-side test sync before device publication. */
	if (iwm_sync(&sc->dma[0], DDI_DMA_SYNC_FORDEV) != 0 ||
	    iwm_sync(&sc->dma[2], DDI_DMA_SYNC_FORDEV) != 0 ||
	    iwm_sync(&sc->dma[3], DDI_DMA_SYNC_FORDEV) != 0 ||
	    iwm_sync(&r->scheduler, DDI_DMA_SYNC_FORDEV) != 0)
		return (EIO);
	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		if (iwm_sync(&r->tx[i], DDI_DMA_SYNC_FORDEV) != 0)
			return (EIO);
	}
	for (i = 0; i < IWM_RX_RING_COUNT; i++) {
		if (iwm_sync(&r->rx[i], DDI_DMA_SYNC_FORDEV) != 0)
			return (EIO);
	}
	for (i = 0; i < IWM_NVM_NUM_OF_SECTIONS; i++)
		r->nvm[i] = kmem_zalloc(IWM_NVM_LIMIT, KM_SLEEP);
	return (iwm_checkpoint(sc, "runtime-allocated"));
}

/*
 * Explicit development opt-in performs exactly one INIT/NVM cycle. No retry,
 * REGULAR image, calibration/radio command, MAC or packet submission exists.
 */
int
iwm_init_nvm(struct iwm_softc *sc)
{
	struct iwm_runtime *r;
	int error, stop_error, host_error;
	uint_t i;

	ASSERT(sc->run == NULL);
	r = kmem_zalloc(sizeof (*r), KM_SLEEP);
	cv_init(&r->cv, NULL, CV_DRIVER, NULL);
	sc->run = r;
	if ((error = iwm_fw_read(sc)) != 0)
		return (error);
	r->state = IWM_FW_LOADED;
	if ((error = iwm_fw_parse(&sc->fw)) != 0)
		return (error);
	r->state = IWM_FW_PARSED;
	if (iwm_checkpoint(sc, "firmware-parsed") != 0)
		return (EIO);
	r->cmdqid = (sc->fw.capa[IWM_UCODE_TLV_CAPA_DQA_SUPPORT / 32] &
	    (1U << (IWM_UCODE_TLV_CAPA_DQA_SUPPORT % 32))) ?
	    IWM_DQA_CMD_QUEUE : IWM_CMD_QUEUE;
	dev_err(sc->dip, CE_NOTE, "!iwm firmware %u.%08x.%u INIT only "
	    "command-q=%u PHY=%08x timeout=%u us", sc->fw.version[0],
	    sc->fw.version[1], sc->fw.version[2], r->cmdqid,
	    sc->fw.phy_config, IWM_WAIT_US);
	for (i = 0; i < 4; i++)
		dev_err(sc->dip, CE_NOTE, "!iwm API[%u]=%08x CAPA[%u]=%08x",
		    i, sc->fw.api[i], i, sc->fw.capa[i]);
	if ((error = iwm_run_alloc(sc)) != 0)
		return (error);
	if (sc->intr_cap & DDI_INTR_FLAG_BLOCK)
		error = ddi_intr_block_enable(&sc->intr, 1);
	else
		error = ddi_intr_enable(sc->intr);
	if (error != DDI_SUCCESS)
		return (EIO);
	sc->intr_enabled = B_TRUE;
	mutex_enter(&sc->lock);
	error = r->error;
	if (error == 0)
		error = iwm_start(sc);
	if (error == 0)
		error = iwm_transport_init(sc);
	if (error == 0)
		error = iwm_upload(sc);
	if (error == 0)
		error = iwm_post_alive(sc);
	if (error == 0)
		error = iwm_nvm(sc);
	if (error != 0)
		dev_err(sc->dip, CE_WARN,
		    "!iwm INIT/NVM failed error=%d state=%u "
		    "INT=%08x FH=%08x RESET=%08x", error, r->state,
		    iwm_rd(sc, IWM_CSR_INT), iwm_rd(sc, IWM_CSR_FH_INT_STATUS),
		    iwm_rd(sc, IWM_CSR_RESET));
	if (r->first_error.reason != IWM_PROTO_OK)
		iwm_proto_report(sc, &r->first_error);
	stop_error = iwm_device_stop(sc);
	dev_err(sc->dip, CE_NOTE,
	    "!iwm protocol original-error=%d reason=%u shutdown-error=%d",
	    error, r->first_error.reason, stop_error);
	mutex_exit(&sc->lock);
	host_error = iwm_intr_disable(sc);
	if (stop_error != 0 || host_error != 0)
		return (EIO);
	/* Include errors from the final callback, now drained by DDI. */
	if (error == 0)
		error = r->error;
	if (error == 0)
		dev_err(sc->dip, CE_NOTE,
		    "!iwm INIT/NVM cycle complete and stopped; "
		    "no radio or MAC; checkpoints=%d", sc->attach_step);
	return (error);
}
