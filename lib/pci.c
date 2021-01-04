/*
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "cap.h"
#include "common.h"
#include "libvfio-user.h"
#include "pci.h"
#include "private.h"

static inline void
pci_hdr_write_bar(vfu_ctx_t *vfu_ctx, uint16_t bar_index, const char *buf)
{
    uint32_t cfg_addr;
    unsigned long mask;
    vfu_reg_info_t *reg_info = vfu_get_region_info(vfu_ctx);
    vfu_pci_hdr_t *hdr;

    assert(vfu_ctx != NULL);

    if (reg_info[bar_index].size == 0) {
        return;
    }

    hdr = &vfu_pci_get_config_space(vfu_ctx)->hdr;

    cfg_addr = *(uint32_t *) buf;

    vfu_log(vfu_ctx, LOG_DEBUG, "BAR%d addr 0x%x\n", bar_index, cfg_addr);

    if (cfg_addr == 0xffffffff) {
        cfg_addr = ~(reg_info[bar_index].size) + 1;
    }

    if ((reg_info[bar_index].flags & VFU_REGION_FLAG_MEM)) {
        mask = PCI_BASE_ADDRESS_MEM_MASK;
    } else {
        mask = PCI_BASE_ADDRESS_IO_MASK;
    }
    cfg_addr |= (hdr->bars[bar_index].raw & ~mask);

    hdr->bars[bar_index].raw = htole32(cfg_addr);
}

#define BAR_INDEX(offset) ((offset - PCI_BASE_ADDRESS_0) >> 2)

static int
handle_command_write(vfu_ctx_t *ctx, vfu_pci_config_space_t *pci,
                     const char *buf, size_t count)
{
    uint16_t v;

    assert(ctx != NULL);

    if (count != 2) {
        vfu_log(ctx, LOG_ERR, "bad write command size %lu\n", count);
        return -EINVAL;
    }

    assert(pci != NULL);
    assert(buf != NULL);

    v = *(uint16_t*)buf;

    if ((v & PCI_COMMAND_IO) == PCI_COMMAND_IO) {
        if (!pci->hdr.cmd.iose) {
            pci->hdr.cmd.iose = 0x1;
            vfu_log(ctx, LOG_INFO, "I/O space enabled\n");
        }
        v &= ~PCI_COMMAND_IO;
    } else {
        if (pci->hdr.cmd.iose) {
            pci->hdr.cmd.iose = 0x0;
            vfu_log(ctx, LOG_INFO, "I/O space disabled\n");
        }
    }

    if ((v & PCI_COMMAND_MEMORY) == PCI_COMMAND_MEMORY) {
        if (!pci->hdr.cmd.mse) {
            pci->hdr.cmd.mse = 0x1;
            vfu_log(ctx, LOG_INFO, "memory space enabled\n");
        }
        v &= ~PCI_COMMAND_MEMORY;
    } else {
        if (pci->hdr.cmd.mse) {
            pci->hdr.cmd.mse = 0x0;
            vfu_log(ctx, LOG_INFO, "memory space disabled\n");
        }
    }

    if ((v & PCI_COMMAND_MASTER) == PCI_COMMAND_MASTER) {
        if (!pci->hdr.cmd.bme) {
            pci->hdr.cmd.bme = 0x1;
            vfu_log(ctx, LOG_INFO, "bus master enabled\n");
        }
        v &= ~PCI_COMMAND_MASTER;
    } else {
        if (pci->hdr.cmd.bme) {
            pci->hdr.cmd.bme = 0x0;
            vfu_log(ctx, LOG_INFO, "bus master disabled\n");
        }
    }

    if ((v & PCI_COMMAND_SERR) == PCI_COMMAND_SERR) {
        if (!pci->hdr.cmd.see) {
            pci->hdr.cmd.see = 0x1;
            vfu_log(ctx, LOG_INFO, "SERR# enabled\n");
        }
        v &= ~PCI_COMMAND_SERR;
    } else {
        if (pci->hdr.cmd.see) {
            pci->hdr.cmd.see = 0x0;
            vfu_log(ctx, LOG_INFO, "SERR# disabled\n");
        }
    }

    if ((v & PCI_COMMAND_INTX_DISABLE) == PCI_COMMAND_INTX_DISABLE) {
        if (!pci->hdr.cmd.id) {
            pci->hdr.cmd.id = 0x1;
            vfu_log(ctx, LOG_INFO, "INTx emulation disabled\n");
        }
        v &= ~PCI_COMMAND_INTX_DISABLE;
    } else {
        if (pci->hdr.cmd.id) {
            pci->hdr.cmd.id = 0x0;
            vfu_log(ctx, LOG_INFO, "INTx emulation enabled\n");
        }
    }

    if ((v & PCI_COMMAND_INVALIDATE) == PCI_COMMAND_INVALIDATE) {
        if (!pci->hdr.cmd.mwie) {
            pci->hdr.cmd.mwie = 1U;
            vfu_log(ctx, LOG_INFO, "memory write and invalidate enabled\n");
        }
        v &= ~PCI_COMMAND_INVALIDATE;
    } else {
        if (pci->hdr.cmd.mwie) {
            pci->hdr.cmd.mwie = 0;
            vfu_log(ctx, LOG_INFO, "memory write and invalidate disabled");
        }
    }

    if ((v & PCI_COMMAND_VGA_PALETTE) == PCI_COMMAND_VGA_PALETTE) {
        vfu_log(ctx, LOG_INFO, "enabling VGA palette snooping ignored\n");
        v &= ~PCI_COMMAND_VGA_PALETTE;
    }

    if (v != 0) {
        vfu_log(ctx, LOG_ERR, "unconsumed command flags %x\n", v);
        return -EINVAL;
    }

    return 0;
}

static int
handle_erom_write(vfu_ctx_t *ctx, vfu_pci_config_space_t *pci,
                  const char *buf, size_t count)
{
    uint32_t v;

    assert(ctx != NULL);
    assert(pci != NULL);

    if (count != 0x4) {
        vfu_log(ctx, LOG_ERR, "bad EROM count %lu\n", count);
        return -EINVAL;
    }
    v = *(uint32_t*)buf;

    if (v == (uint32_t)PCI_ROM_ADDRESS_MASK) {
        vfu_log(ctx, LOG_INFO, "write mask to EROM ignored\n");
    } else if (v == 0) {
        vfu_log(ctx, LOG_INFO, "cleared EROM\n");
        pci->hdr.erom = 0;
    } else if (v == (uint32_t)~PCI_ROM_ADDRESS_ENABLE) {
        vfu_log(ctx, LOG_INFO, "EROM disable ignored\n");
    } else {
        vfu_log(ctx, LOG_ERR, "bad write to EROM 0x%x bytes\n", v);
        return -EINVAL;
    }
    return 0;
}

static inline int
pci_hdr_write(vfu_ctx_t *vfu_ctx, uint16_t offset,
                  const char *buf, size_t count)
{
    vfu_pci_config_space_t *pci;
    int ret = 0;

    assert(vfu_ctx != NULL);
    assert(buf != NULL);

    pci = vfu_pci_get_config_space(vfu_ctx);

    switch (offset) {
    case PCI_COMMAND:
        ret = handle_command_write(vfu_ctx, pci, buf, count);
        break;
    case PCI_STATUS:
        vfu_log(vfu_ctx, LOG_INFO, "write to status ignored\n");
        break;
    case PCI_INTERRUPT_PIN:
        vfu_log(vfu_ctx, LOG_ERR, "attempt to write read-only field IPIN\n");
        ret = -EINVAL;
        break;
    case PCI_INTERRUPT_LINE:
        pci->hdr.intr.iline = buf[0];
        vfu_log(vfu_ctx, LOG_DEBUG, "ILINE=%0x\n", pci->hdr.intr.iline);
        break;
    case PCI_LATENCY_TIMER:
        pci->hdr.mlt = (uint8_t)buf[0];
        vfu_log(vfu_ctx, LOG_INFO, "set to latency timer to %hhx\n", pci->hdr.mlt);
        break;
    case PCI_BASE_ADDRESS_0:
    case PCI_BASE_ADDRESS_1:
    case PCI_BASE_ADDRESS_2:
    case PCI_BASE_ADDRESS_3:
    case PCI_BASE_ADDRESS_4:
    case PCI_BASE_ADDRESS_5:
        pci_hdr_write_bar(vfu_ctx, BAR_INDEX(offset), buf);
        break;
    case PCI_ROM_ADDRESS:
        ret = handle_erom_write(vfu_ctx, pci, buf, count);
        break;
    default:
        vfu_log(vfu_ctx, LOG_INFO, "PCI config write %#x-%#lx not handled\n",
                offset, offset + count);
        ret = -EINVAL;
    }

#ifdef VFU_VERBOSE_LOGGING
    dump_buffer("PCI header", (char*)pci->hdr.raw, 0xff);
#endif

    return ret;
}

/*
 * @pci_hdr: the PCI header
 * @reg_info: region info
 * @rw: the command
 * @write: whether this is a PCI header write
 * @count: output parameter that receives the number of bytes read/written
 */
int
pci_hdr_access(vfu_ctx_t *vfu_ctx, uint32_t *count,
               uint64_t *pos, bool is_write, char *buf)
{
    uint32_t _count;
    loff_t _pos;
    int err = 0;

    assert(vfu_ctx != NULL);
    assert(count != NULL);
    assert(pos != NULL);
    assert(buf != NULL);

    _pos = *pos - region_to_offset(VFU_PCI_DEV_CFG_REGION_IDX);
    _count = MIN(*count, PCI_STD_HEADER_SIZEOF - _pos);

    if (is_write) {
        err = pci_hdr_write(vfu_ctx, _pos, buf, _count);
    } else {
        memcpy(buf, vfu_pci_get_config_space(vfu_ctx)->hdr.raw + _pos, _count);
    }
    *pos += _count;
    *count -= _count;
    return err;
}

static uint32_t
pci_config_space_size(vfu_ctx_t *vfu_ctx)
{
    return vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].size;
}

/*
 * Accesses the non-standard part (offset >= 0x40) of the PCI configuration
 * space.
 */
ssize_t
pci_config_space_access(vfu_ctx_t *vfu_ctx, char *buf, size_t count,
                        loff_t pos, bool is_write)
{
    int ret;

    count = MIN(pci_config_space_size(vfu_ctx), count);
    if (is_write) {
        ret = cap_maybe_access(vfu_ctx, vfu_ctx->pci.caps, buf, count, pos);
        if (ret < 0) {
            vfu_log(vfu_ctx, LOG_ERR, "bad access to capabilities %#lx-%#lx\n",
                    pos, pos + count);
            return ret;
        }
    } else {
        /*
         * It is legitimate to read the non-standard part of the PCI config
         * space even if there are no capabilities.
         */
        memcpy(buf, vfu_ctx->pci.config_space->raw + pos, count);
    }
    return count;
}

int
vfu_pci_init(vfu_ctx_t *vfu_ctx, vfu_pci_type_t pci_type,
             int hdr_type, int revision UNUSED)
{
    vfu_pci_config_space_t *config_space;
    size_t size;

    assert(vfu_ctx != NULL);

    switch (pci_type) {
    case VFU_PCI_TYPE_CONVENTIONAL:
    case VFU_PCI_TYPE_PCI_X_1:
        size = PCI_CFG_SPACE_SIZE;
        break;
    case VFU_PCI_TYPE_PCI_X_2:
    case VFU_PCI_TYPE_EXPRESS:
        size = PCI_CFG_SPACE_EXP_SIZE;
        break;
    default:
        vfu_log(vfu_ctx, LOG_ERR, "invalid PCI type %u", pci_type);
        return ERROR(EINVAL);
    }

    if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
        vfu_log(vfu_ctx, LOG_ERR, "invalid PCI header type %d", hdr_type);
        return ERROR(EINVAL);
    }

    /*
     * TODO there no real reason why we shouldn't allow this, we should just
     * clean up and redo it.
     */
    if (vfu_ctx->pci.config_space != NULL) {
        vfu_log(vfu_ctx, LOG_ERR,
                "PCI configuration space header already setup");
        return ERROR(EEXIST);
    }

    // Allocate a buffer for the config space.
    config_space = calloc(1, size);
    if (config_space == NULL) {
        return ERROR(ENOMEM);
    }

    vfu_ctx->pci.type = pci_type;
    vfu_ctx->pci.config_space = config_space;
    vfu_ctx->reg_info[VFU_PCI_DEV_CFG_REGION_IDX].size = size;

    return 0;
}

void
vfu_pci_set_id(vfu_ctx_t *vfu_ctx, uint16_t vid, uint16_t did,
               uint16_t ssvid, uint16_t ssid)
{
    vfu_ctx->pci.config_space->hdr.id.vid = vid;
    vfu_ctx->pci.config_space->hdr.id.did = did;
    vfu_ctx->pci.config_space->hdr.ss.vid = ssvid;
    vfu_ctx->pci.config_space->hdr.ss.sid = ssid;
}

void
vfu_pci_set_class(vfu_ctx_t *vfu_ctx, uint8_t base, uint8_t sub, uint8_t pi)
{
    vfu_ctx->pci.config_space->hdr.cc.bcc = base;
    vfu_ctx->pci.config_space->hdr.cc.scc = sub;
    vfu_ctx->pci.config_space->hdr.cc.pi = pi;
}

int
vfu_pci_setup_caps(vfu_ctx_t *vfu_ctx, vfu_cap_t **caps, int nr_caps)
{
    int ret;

    assert(vfu_ctx != NULL);

    if (vfu_ctx->pci.caps != NULL) {
        vfu_log(vfu_ctx, LOG_ERR, "capabilities are already setup");
        return ERROR(EEXIST);
    }

    if (caps == NULL || nr_caps == 0) {
        vfu_log(vfu_ctx, LOG_ERR, "Invalid args passed");
        return ERROR(EINVAL);
    }

    vfu_ctx->pci.caps = caps_create(vfu_ctx, caps, nr_caps, &ret);
    if (vfu_ctx->pci.caps == NULL) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to create PCI capabilities: %s",
               strerror(ret));
        return ERROR(ret);
    }

    return 0;
}

inline vfu_pci_config_space_t *
vfu_pci_get_config_space(vfu_ctx_t *vfu_ctx)
{
    assert(vfu_ctx != NULL);
    return vfu_ctx->pci.config_space;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
