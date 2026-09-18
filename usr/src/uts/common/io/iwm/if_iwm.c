/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 */

/*
 * Native resource boundaries for the Intel 8260 transport.
 *
 * Hardware definitions come from the pinned OpenBSD iwm donor described in
 * the accompanying headers.  This file contains native illumos integration,
 * not an implementation of OpenBSD kernel interfaces.
 *
 * Attachment is deliberately rejected before PCI access.  There is no
 * hardware start, firmware transfer, interrupt handler or MAC registration.
 * The resource helpers below are compiled for API checking, but have no
 * caller on the module's entry-point paths.  They are not a hardware test.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/firmload.h>
#include <sys/mac_provider.h>
#include "if_iwmvar.h"

/* The only hardware identity admitted by the resource mapping helper. */
static const struct iwm_cfg iwm_8260 = {
	.vendor = 0x8086,
	.device = 0x24f3,
	.subvendor = 0x8086,
	.subdevice = 0x1130,
	.revision = 0x3a,
	.family = IWM_DEVICE_FAMILY_8000,
	.fw_dma_size = IWM_FWDMASEGSZ_8000,
	.nvm_section_size = 32768,
	.fwname = "iwm-8000C-36"
};

#define	IWM_BAR_SIZE	0x2000
#define	IWM_DMA_MAX	0xfffffffffULL	/* 36-bit transport addresses */
#define	IWM_FW_FILE_MAX	(4 * 1024 * 1024)

static void *iwm_state;

static const ddi_device_acc_attr_t iwm_reg_attr = {
	DDI_DEVICE_ATTR_V0, DDI_STRUCTURE_LE_ACC, DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

/* Wire fields are explicitly little endian; do not swap payload bytes. */
static const ddi_device_acc_attr_t iwm_dma_attr = {
	DDI_DEVICE_ATTR_V0, DDI_NEVERSWAP_ACC, DDI_STRICTORDER_ACC,
	DDI_DEFAULT_ACC
};

/*
 * Thread context, exclusive lifecycle ownership.  This does not enable bus
 * mastering, alter PCI command bits, or read/write the mapped device BAR.
 * BAR0 is reg tuple 1 on the target; require its observed 8 KiB extent.
 */
int
iwm_pci_map(struct iwm_softc *sc)
{
	const struct iwm_cfg *cfg = &iwm_8260;

	if (sc->pcih != NULL || sc->regh != NULL)
		return (EBUSY);
	if (pci_config_setup(sc->dip, &sc->pcih) != DDI_SUCCESS)
		return (EIO);

	if (pci_config_get16(sc->pcih, PCI_CONF_VENID) != cfg->vendor ||
	    pci_config_get16(sc->pcih, PCI_CONF_DEVID) != cfg->device ||
	    pci_config_get16(sc->pcih, PCI_CONF_SUBVENID) != cfg->subvendor ||
	    pci_config_get16(sc->pcih, PCI_CONF_SUBSYSID) != cfg->subdevice ||
	    pci_config_get8(sc->pcih, PCI_CONF_REVID) != cfg->revision) {
		iwm_pci_unmap(sc);
		return (ENODEV);
	}
	if (ddi_dev_regsize(sc->dip, 1, &sc->regsize) != DDI_SUCCESS ||
	    sc->regsize != IWM_BAR_SIZE ||
	    ddi_regs_map_setup(sc->dip, 1, &sc->regs, 0, sc->regsize,
	    &iwm_reg_attr, &sc->regh) != DDI_SUCCESS) {
		iwm_pci_unmap(sc);
		return (EIO);
	}
	sc->cfg = cfg;
	return (0);
}

void
iwm_pci_unmap(struct iwm_softc *sc)
{
	if (sc->regh != NULL)
		ddi_regs_map_free(&sc->regh);
	sc->regs = NULL;
	sc->regsize = 0;
	if (sc->pcih != NULL)
		pci_config_teardown(&sc->pcih);
	sc->cfg = NULL;
}

/* Direct CSR access only; these do not acquire peripheral/NIC ownership. */
int
iwm_reg_read(struct iwm_softc *sc, uint_t reg, uint32_t *value)
{
	if (sc->regh == NULL || value == NULL || (reg & 3) != 0 ||
	    sc->regsize < sizeof (*value) ||
	    reg > sc->regsize - sizeof (*value))
		return (EINVAL);
	*value = ddi_get32(sc->regh, (uint32_t *)(sc->regs + reg));
	return (0);
}

int
iwm_reg_write(struct iwm_softc *sc, uint_t reg, uint32_t value)
{
	if (sc->regh == NULL || (reg & 3) != 0 ||
	    sc->regsize < sizeof (value) ||
	    reg > sc->regsize - sizeof (value))
		return (EINVAL);
	ddi_put32(sc->regh, (uint32_t *)(sc->regs + reg), value);
	return (0);
}

/*
 * Allocate one contiguous device-visible region.  Alignment is specified by
 * the ring/firmware owner, never inferred from a kernel virtual address.
 * This helper is for consistent control/ring memory, not streaming mblks.
 * No address is published to hardware here.  A later publisher must perform
 * ddi_dma_sync(FORDEV); a consumer must perform ddi_dma_sync(FORCPU).
 */
int
iwm_dma_alloc(struct iwm_softc *sc, struct iwm_dma_info *dma, size_t size,
    uint_t align, uint_t direction)
{
	ddi_dma_attr_t attr = {
		DMA_ATTR_V0, 0, IWM_DMA_MAX, IWM_DMA_MAX, 1, 0x7ff, 1,
		IWM_DMA_MAX, IWM_DMA_MAX, 1, 1, 0
	};
	uint_t count;

	if (dma->dma_hdl != NULL || dma->acc_hdl != NULL || dma->bound)
		return (EBUSY);
	if (size == 0 || size > IWM_DMA_MAX || align == 0 ||
	    (align & (align - 1)) != 0 ||
	    (direction != DDI_DMA_READ && direction != DDI_DMA_WRITE &&
	    direction != DDI_DMA_RDWR))
		return (EINVAL);
	attr.dma_attr_align = align;
	if (ddi_dma_alloc_handle(sc->dip, &attr, DDI_DMA_SLEEP, NULL,
	    &dma->dma_hdl) != DDI_SUCCESS)
		return (ENOMEM);
	if (ddi_dma_mem_alloc(dma->dma_hdl, size, &iwm_dma_attr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, &dma->vaddr,
	    &dma->length, &dma->acc_hdl) != DDI_SUCCESS)
		goto fail;
	if (dma->length < size)
		goto fail;
	if (ddi_dma_addr_bind_handle(dma->dma_hdl, NULL, dma->vaddr, size,
	    direction | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &dma->cookie, &count) != DDI_DMA_MAPPED)
		goto fail;
	dma->bound = B_TRUE;
	if (count != 1 || dma->cookie.dmac_size < size ||
	    dma->cookie.dmac_laddress > IWM_DMA_MAX ||
	    size - 1 > IWM_DMA_MAX - dma->cookie.dmac_laddress ||
	    (dma->cookie.dmac_laddress & (align - 1)) != 0)
		goto fail;
	dma->size = size;
	bzero(dma->vaddr, size);
	return (0);

fail:
	/* If unbinding fails, the object retains ownership for its caller. */
	(void) iwm_dma_free(dma);
	return (EIO);
}

/* Device must be stopped; retain resources rather than free a live mapping. */
int
iwm_dma_free(struct iwm_dma_info *dma)
{
	if (dma->bound) {
		if (ddi_dma_unbind_handle(dma->dma_hdl) != DDI_SUCCESS)
			return (EIO);
		dma->bound = B_FALSE;
	}
	if (dma->acc_hdl != NULL)
		ddi_dma_mem_free(&dma->acc_hdl);
	if (dma->dma_hdl != NULL)
		ddi_dma_free_handle(&dma->dma_hdl);
	bzero(dma, sizeof (*dma));
	return (0);
}

/*
 * Read raw bytes in thread context with exclusive ownership of fw.  Success
 * means file I/O only.  TLV parsing, version/capability validation and device
 * transfer are deliberately absent; this buffer must not be sent to a NIC.
 * The filename resolves to kernel/firmware/iwm/iwm-8000C-36 through firmload.
 */
int
iwm_fw_read(struct iwm_fw_info *fw)
{
	firmware_handle_t handle;
	off_t size;
	int error;

	if (fw->data != NULL)
		return (EBUSY);
	error = firmware_open("iwm", iwm_8260.fwname, &handle);
	if (error != 0)
		return (error);
	size = firmware_get_size(handle);
	if (size < sizeof (struct iwm_tlv_ucode_header) ||
	    size > IWM_FW_FILE_MAX) {
		(void) firmware_close(handle);
		return (EINVAL);
	}
	fw->size = (size_t)size;
	fw->data = kmem_zalloc(fw->size, KM_SLEEP);
	error = firmware_read(handle, 0, fw->data, fw->size);
	(void) firmware_close(handle);
	if (error != 0) {
		iwm_fw_free(fw);
		return (EIO);
	}
	return (0);
}

void
iwm_fw_free(struct iwm_fw_info *fw)
{
	if (fw->data != NULL)
		kmem_free(fw->data, fw->size);
	fw->data = NULL;
	fw->size = 0;
}

static int
iwm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct iwm_softc *sc;
	int instance = ddi_get_instance(dip);

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);
	if (ddi_soft_state_zalloc(iwm_state, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);
	sc = ddi_get_soft_state(iwm_state, instance);
	sc->dip = dip;
	sc->cfg = &iwm_8260;
	ddi_set_driver_private(dip, sc);

	/*
	 * No PCI, BAR, DMA, interrupt, firmware or wireless operation may
	 * precede this rejection.  Removing it requires a real attach/unwind
	 * and quiesce implementation, not a property or a success stub.
	 */
	dev_err(dip, CE_WARN, "!iwm hardware attachment is not implemented");
	ddi_set_driver_private(dip, NULL);
	ddi_soft_state_free(iwm_state, instance);
	return (DDI_FAILURE);
}

/* No instance can attach.  Detach, suspend and quiesce are unsupported. */
static int
iwm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	_NOTE(ARGUNUSED(dip, cmd));
	return (DDI_FAILURE);
}

static int
iwm_quiesce(dev_info_t *dip)
{
	_NOTE(ARGUNUSED(dip));
	return (DDI_FAILURE);
}

DDI_DEFINE_STREAM_OPS(iwm_devops, nulldev, nulldev, iwm_attach,
    iwm_detach, nodev, NULL, D_MP, NULL, iwm_quiesce);

static struct modldrv iwm_modldrv = {
	&mod_driverops,
	"Intel 8260 transport (attachment disabled)",
	&iwm_devops
};

static struct modlinkage iwm_modlinkage = {
	MODREV_1, { &iwm_modldrv, NULL }
};

int
_init(void)
{
	int error;

	error = ddi_soft_state_init(&iwm_state, sizeof (struct iwm_softc), 1);
	if (error != 0)
		return (error);
	mac_init_ops(&iwm_devops, "iwm");
	error = mod_install(&iwm_modlinkage);
	if (error != 0) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_state);
	}
	return (error);
}

int
_fini(void)
{
	int error = mod_remove(&iwm_modlinkage);

	if (error == 0) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_state);
	}
	return (error);
}

int
_info(struct modinfo *mip)
{
	return (mod_info(&iwm_modlinkage, mip));
}
