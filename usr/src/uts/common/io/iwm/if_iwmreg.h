/* BEGIN CSTYLED */
/*	$OpenBSD: if_iwmreg.h,v 1.70 2024/09/01 03:08:59 jsg Exp $	*/

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 *
 *****************************************************************************/

/* END CSTYLED */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 * Preserve the original donor licence notices above verbatim.
 */

/*
 * OpenBSD sys/dev/pci/if_iwmreg.h at
 * 0efabb066d34187a404f31d303b3b97103df1117, BSD licence option.
 * Transport ABI subset for the 8260.  Local changes are native guards,
 * includes, packing spelling, comment/whitespace style and layout assertions.
 * No wire fields are changed.
 */
#ifndef _IF_IWMREG_H
#define	_IF_IWMREG_H

#include <sys/types.h>
#include <sys/debug.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Direct CSR register offsets; peripheral access needs NIC ownership. */
#define	IWM_CSR_INT		0x008
#define	IWM_CSR_INT_MASK	0x00c
#define	IWM_CSR_FH_INT_STATUS	0x010
#define	IWM_CSR_RESET		0x020
#define	IWM_CSR_GP_CNTRL	0x024
#define	IWM_CSR_HW_REV		0x028
#define	IWM_CSR_INT_PERIODIC_REG	0x005

/* Pinned donor CSR fields; no MAC clock or peripheral access is required. */
#define	IWM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW	0x08000000
#define	IWM_CSR_INT_BIT_FH_RX	(1U << 31)
#define	IWM_CSR_INT_BIT_HW_ERR	(1U << 29)
#define	IWM_CSR_INT_BIT_RX_PERIODIC	(1U << 28)
#define	IWM_CSR_INT_BIT_FH_TX	(1U << 27)
#define	IWM_CSR_INT_BIT_SCD	(1U << 26)
#define	IWM_CSR_INT_BIT_SW_ERR	(1U << 25)
#define	IWM_CSR_INT_BIT_RF_KILL	(1U << 7)
#define	IWM_CSR_INT_BIT_CT_KILL	(1U << 6)
#define	IWM_CSR_INT_BIT_SW_RX	(1U << 3)
#define	IWM_CSR_INT_BIT_WAKEUP	(1U << 1)
#define	IWM_CSR_INT_BIT_ALIVE	(1U << 0)
#define	IWM_CSR_FH_INT_BIT_ERR	(1U << 31)
#define	IWM_CSR_FH_INT_BIT_HI_PRIOR	(1U << 30)
#define	IWM_CSR_FH_INT_BIT_RX_CHNL1	(1U << 17)
#define	IWM_CSR_FH_INT_BIT_RX_CHNL0	(1U << 16)
#define	IWM_CSR_FH_INT_BIT_TX_CHNL1	(1U << 1)
#define	IWM_CSR_FH_INT_BIT_TX_CHNL0	(1U << 0)

struct iwm_ucode_tlv {
	uint32_t type;		/* see above */
	uint32_t length;		/* not including type/length fields */
	uint8_t data[0];
};

struct iwm_ucode_api {
	uint32_t api_index;
	uint32_t api_flags;
} __attribute__((__packed__));

struct iwm_ucode_capa {
	uint32_t api_index;
	uint32_t api_capa;
} __attribute__((__packed__));

#define	IWM_TLV_UCODE_MAGIC	0x0a4c5749

struct iwm_tlv_ucode_header {
	/*
	 * The TLV style ucode header is distinguished from
	 * the v1/v2 style header by first four bytes being
	 * zero, as such is an invalid combination of
	 * major/minor/API/serial versions.
	 */
	uint32_t zero;
	uint32_t magic;
	uint8_t human_readable[64];
	uint32_t ver;		/* major/minor/API/serial */
	uint32_t build;
	uint64_t ignore;
	/*
	 * The data contained herein has a TLV layout,
	 * see above for the TLV header and types.
	 * Note that each TLV is padded to a length
	 * that is a multiple of 4 for alignment.
	 */
	uint8_t data[0];
};

#define	IWM_NUM_OF_TBS	20
#define	IWM_TFD_QUEUE_SIZE_MAX	256
#define	IWM_TFD_QUEUE_SIZE_BC_DUP	64
#define	IWM_TFD_QUEUE_BC_SIZE	(IWM_TFD_QUEUE_SIZE_MAX + \
    IWM_TFD_QUEUE_SIZE_BC_DUP)

#define	IWM_RX_QUEUE_SIZE	256
#define	IWM_RX_QUEUE_MASK	255
#define	IWM_RX_QUEUE_SIZE_LOG	8

/*
 * RX related structures and functions
 */
#define	IWM_RX_FREE_BUFFERS 64
#define	IWM_RX_LOW_WATERMARK 8

/*
 * struct iwm_rb_status - reserve buffer status
 * 	host memory mapped FH registers
 * @closed_rb_num [0:11] - Indicates the index of the RB which was closed
 * @closed_fr_num [0:11] - Indicates the index of the RX Frame which was closed
 * @finished_rb_num [0:11] - Indicates the index of the current RB
 * 	in which the last frame was written to
 * @finished_fr_num [0:11] - Indicates the index of the RX Frame
 * 	which was transferred
 */
struct iwm_rb_status {
	uint16_t closed_rb_num;
	uint16_t closed_fr_num;
	uint16_t finished_rb_num;
	uint16_t finished_fr_nam;
	uint32_t unused;
} __attribute__((__packed__));
/*
 * struct iwm_tfd_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @lo: low [31:0] portion of the dma address of TX buffer
 * 	every even is unaligned on 16 bit boundary
 * @hi_n_len 0-3 [35:32] portion of dma
 *	     4-15 length of the tx buffer
 */
struct iwm_tfd_tb {
	uint32_t lo;
	uint16_t hi_n_len;
} __attribute__((__packed__));

/*
 * struct iwm_tfd
 *
 * Transmit Frame Descriptor (TFD)
 *
 * @ __reserved1[3] reserved
 * @ num_tbs 0-4 number of active tbs
 *	     5   reserved
 *	     6-7 padding (not used)
 * @ tbs[20]	transmit frame buffer descriptors
 * @ __pad 	padding
 *
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs x 128 bytes-per-TFD = 32 KBytes
 *
 * Driver must indicate the physical address of the base of each
 * circular buffer via the IWM_FH_MEM_CBBC_QUEUE registers.
 *
 * Each TFD contains pointer/size information for up to 20 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */
struct iwm_tfd {
	uint8_t __reserved1[3];
	uint8_t num_tbs;
	struct iwm_tfd_tb tbs[IWM_NUM_OF_TBS];
	uint32_t __pad;
} __attribute__((__packed__));

/* Keep Warm Size */
#define	IWM_KW_SIZE 0x1000	/* 4k */

/* Fixed (non-configurable) rx data from phy */

/*
 * struct iwm_agn_schedq_bc_tbl scheduler byte count table
 *	base physical address provided by IWM_SCD_DRAM_BASE_ADDR
 * @tfd_offset  0-12 - tx command byte count
 *	       12-16 - station index
 */
struct iwm_agn_scd_bc_tbl {
	uint16_t tfd_offset[IWM_TFD_QUEUE_BC_SIZE];
} __attribute__((__packed__));
struct iwm_cmd_header {
	uint8_t code;
	uint8_t flags;
	uint8_t idx;
	uint8_t qid;
} __attribute__((__packed__));

struct iwm_cmd_header_wide {
	uint8_t opcode;
	uint8_t group_id;
	uint8_t idx;
	uint8_t qid;
	uint16_t length;
	uint8_t reserved;
	uint8_t version;
} __attribute__((__packed__));

#define	IWM_POWER_SCHEME_CAM	1
#define	IWM_POWER_SCHEME_BPS	2
#define	IWM_POWER_SCHEME_LP	3

#define	IWM_DEF_CMD_PAYLOAD_SIZE 320
#define	IWM_MAX_CMD_PAYLOAD_SIZE ((4096 - 4) - sizeof (struct iwm_cmd_header))
#define	IWM_CMD_FAILED_MSK 0x40

/*
 * struct iwm_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
 */
struct iwm_device_cmd {
	union {
		struct {
			struct iwm_cmd_header hdr;
			uint8_t data[IWM_DEF_CMD_PAYLOAD_SIZE];
		};
		struct {
			struct iwm_cmd_header_wide hdr_wide;
			uint8_t data_wide[IWM_DEF_CMD_PAYLOAD_SIZE -
					sizeof (struct iwm_cmd_header_wide) +
					sizeof (struct iwm_cmd_header)];
		};
	};
} __attribute__((__packed__));

struct iwm_rx_packet {
	/*
	 * The first 4 bytes of the RX frame header contain both the RX frame
	 * size and some flags.
	 * Bit fields:
	 * 31:    flag flush RB request
	 * 30:    flag ignore TC (terminal counter) request
	 * 29:    flag fast IRQ request
	 * 28-26: Reserved
	 * 25:    Offload enabled
	 * 24:    RPF enabled
	 * 23:    RSS enabled
	 * 22:    Checksum enabled
	 * 21-16: RX queue
	 * 15-14: Reserved
	 * 13-00: RX frame size
	 */
	uint32_t len_n_flags;
	struct iwm_cmd_header hdr;
	uint8_t data[];
} __attribute__((__packed__));

CTASSERT(sizeof (struct iwm_ucode_tlv) == 8);
CTASSERT(sizeof (struct iwm_tlv_ucode_header) == 88);
CTASSERT(sizeof (struct iwm_rb_status) == 12);
CTASSERT(sizeof (struct iwm_tfd_tb) == 6);
CTASSERT(sizeof (struct iwm_tfd) == 128);
CTASSERT(sizeof (struct iwm_cmd_header) == 4);
CTASSERT(sizeof (struct iwm_cmd_header_wide) == 8);
CTASSERT(sizeof (struct iwm_device_cmd) == 324);
CTASSERT(sizeof (struct iwm_rx_packet) == 8);

#ifdef __cplusplus
}
#endif

#endif /* _IF_IWMREG_H */
