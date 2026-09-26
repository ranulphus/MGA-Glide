/* ROMDUMP - save the Matrox card's video BIOS (PRD WP-4).
 *
 * Reads the image through the PCI expansion-ROM BAR, not the C000h shadow
 * (which the system BIOS may have modified), writes it to C:\OUT\VBIOS.BIN
 * and reports its size and CRC. The dump stays with the owner of the card:
 * it goes into the local 86Box ROM set, never into this repository.
 *
 * Steps: size the ROM BAR (write all ones, read back), place it at an
 * address the card is not using (just above the framebuffer aperture),
 * enable decoding, copy the image (length from its header, 512-byte units),
 * then restore the BAR. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdio.h>
#include <string.h>

static uint8_t image[128 * 1024];

int main(int argc, char **argv)
{
    uint32_t old_bar, size_mask, size, base, len, i;
    volatile uint8_t *rom;
    FILE *f;
    int ok;

    hx_init(argc, argv, "romdump");
    ok = mga_find(&mga) == 0;
    hx_test("pci", ok, "%s id=%04x rev=%02x", ok ? mga.name : "none", mga.device_id, mga.revision);
    if (!ok)
        hx_done(HX_INIT_FAILED);
    old_bar = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x30);
    mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x30, 0xFFFFF800u);
    size_mask = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x30) & 0xFFFFF800u;
    size = size_mask ? (~size_mask + 1u) : 0;
    if (!size || size > sizeof image) {
        mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x30, old_bar);
        hx_test("rombar", 0, "ROM BAR size %lu unsupported (bar=%08lx)", (unsigned long)size, (unsigned long)old_bar);
        hx_done(HX_FAIL);
    }
    /* Use the BIOS-assigned address if there is one, else one above the
     * framebuffer aperture (aligned to the ROM size). */
    base = old_bar & 0xFFFFF800u;
    if (!base)
        base = (mga.fb_phys + 0x02000000u + size - 1) & ~(size - 1);
    mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x30, base | 1u);
    rom = (volatile uint8_t *)sys_map_phys(base, size);
    if (!rom) {
        mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x30, old_bar);
        hx_test("map", 0, "cannot map %08lx", (unsigned long)base);
        hx_done(HX_FAIL);
    }
    for (i = 0; i < size; i++)
        image[i] = rom[i];
    mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x30, old_bar);
    ok = image[0] == 0x55 && image[1] == 0xAA;
    len = ok ? (uint32_t)image[2] * 512u : 0;
    if (!len || len > size)
        len = size;
    hx_test("signature", ok, "55AA %s, header length %lu of BAR size %lu", ok ? "found" : "missing",
            (unsigned long)len, (unsigned long)size);
    f = fopen("C:\\OUT\\VBIOS.BIN", "wb");
    if (f) {
        fwrite(image, 1, len, f);
        fclose(f);
    }
    hx_stat("romdump device=%04x bytes=%lu crc32=%08lx file=C:\\OUT\\VBIOS.BIN", mga.device_id,
            (unsigned long)len, (unsigned long)hx_crc32(0, image, len));
    hx_done(ok && f ? 0 : HX_FAIL);
    return 0;
}
