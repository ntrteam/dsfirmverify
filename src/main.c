/* dsfirmverify
 * Copyright (C) 2017 angelsl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdbool.h>

#include <nds.h>
#include <ncgc/ntrcard.h>
#include <ncgc/platform/ntr.h>

#define CBC 1
#define ECB 0
#include "sha256.h"
#include "aes/aes.h"
#include "data.h"

#define MIN(v1, v2) ((v1) < (v2) ? (v1) : (v2))
#define MAX(v1, v2) ((v1) > (v2) ? (v1) : (v2))

PrintConsole topScreen;
PrintConsole bottomScreen;

typedef struct {
    uint32_t magic;
    uint32_t priority;
    uint32_t arm11_entry;
    uint32_t arm9_entry;
    char garbage[0x30];
    struct {
        uint32_t offset;
        uint32_t phy_addr;
        uint32_t size;
        uint32_t copy_method;
        char sha256[0x20];
    } sections[4];
    char signature[0x100];
} firm_hdr_t;
_Static_assert(sizeof(firm_hdr_t) == 0x200, "Wrong firm_hdr size");

static uint32_t wait_for_any_key(uint32_t keys) {
	while (true) {
		scanKeys();
        uint32_t down = keysDown();
		if(down & keys) {
			return down;
        }
		swiWaitForVBlank();
	}
}

static void wait_for_keys(uint32_t keys) {
	while (true) {
		scanKeys();
		if(keysDown() == keys) {
			break;
        }
		swiWaitForVBlank();
	}
}

static bool reset_card(void) {
    iprintf("Remove the card FULLY from the\n"
            "slot. Reinsert the card, then\n"
            "press A.\n"
            "(You may hotswap now.)\n");
    wait_for_keys(KEY_A);
    return true;
}

static int32_t read_card(ncgc_ncard_t *const card, uint32_t address, uint32_t size, const char *const secure_area, unsigned char *buf) {
    if (address < 0x4000) {
        return -901;
    }

    if (address < 0x8000) {
        /* address >= 0x4000 => 0x8000 - address <= 0x4000 */
        const uint32_t copy_sz = MIN(0x8000 - address, size);
        memcpy(buf, secure_area + (address - 0x4000), copy_sz);
        address += copy_sz;
        buf += copy_sz;
        size -= copy_sz;
    }

    if (size > 0) {
        return ncgc_nread_data(card, address, buf, size);
    }

    return 0;
}

mbedtls_sha256_context sha256_ctx;
static bool verify_section(int i,
                           ncgc_ncard_t *const card,
                           const firm_hdr_t *const hdr,
                           const char *const secure_area,
                           const void *const key) {
    // can be anything actually, as long as it's a multiple of the AES block size 0x10
    // i choose 0x200 because that's the size of 0xB7 reads
    const uint32_t block_size = 0x200;
    unsigned char iv[16];
    memcpy(iv, &hdr->sections[i], 12);
    memcpy(iv + 12, &hdr->sections[i].size, 4);

    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);

    uint32_t cur_addr = hdr->sections[i].offset + 0x7E00;
    uint32_t size_left = hdr->sections[i].size;

    while (size_left > 0) {
        const uint32_t cur_size = MIN(block_size, size_left);
        unsigned char ct[cur_size], pt[cur_size];
        int32_t r = read_card(card, cur_addr, cur_size, secure_area, ct);
        if (r) {
            iprintf("read_card failed: %ld (%d)\n", r, i);
            return false;
        }
        AES_CBC_decrypt_buffer(pt, ct, cur_size, key, iv);
        mbedtls_sha256_update(&sha256_ctx, pt, cur_size);

        if (cur_size == block_size) {
            // not last block (or size is multiple of block size, whatever)
            memcpy(iv, ct + block_size - 0x10, 0x10);
        }

        cur_addr += cur_size;
        size_left -= cur_size;
    }

    unsigned char hash[0x20];
    mbedtls_sha256_finish(&sha256_ctx, hash);
    // this just zeroes the struct
    // FIRM hashes are top secret!
    // hah, who am I kidding, don't even bother
    // mbedtls_sha256_free(&sha256_ctx);
    if (memcmp(hash, hdr->sections[i].sha256, 0x20)) {
        iprintf("Sect %d invalid hash\n", i);
        return false;
    }

    iprintf("Sect %d OK\n", i);
    return true;
}

static void print_header_hash(const firm_hdr_t *const hdr) {
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, (const unsigned char *) hdr, sizeof(firm_hdr_t));
    unsigned char hash[0x20];
    mbedtls_sha256_finish(&sha256_ctx, hash);
    iprintf("FIRM header SHA256:\n");
    for (unsigned int i = 0; i < sizeof(hash); ++i) {
        iprintf("%02x", hash[i]);
    }
    iprintf("\n");
}

static void print_magic(const firm_hdr_t *const hdr) {
    unsigned int start;
    for (start = sizeof(hdr->garbage); start; --start) {
        if (hdr->garbage[start-1] < 0x20 || hdr->garbage[start-1] > 0x7E) {
            break;
        }
    }
    iprintf("FIRM reserved area magic:\n");
    for (; start < sizeof(hdr->garbage); ++start) {
        iprintf("%c", hdr->garbage[start]);
    }
    iprintf("\n");
}

char secure_area[0x4000];
ncgc_ncard_t card;
void do_check(void) {
    consoleSelect(&bottomScreen);
    consoleClear();
    consoleSelect(&topScreen);
    consoleClear();
    bool dev = false;
    iprintf("Press X for retail FIRM\n"
            "      Y for dev FIRM\n");

    while (true) {
        uint32_t keys = wait_for_any_key(KEY_X | KEY_Y);
        if (keys == KEY_X) {
            dev = false;
            break;
        } else if (keys == KEY_Y) {
            dev = true;
            break;
        } else {
            iprintf("\n"
                    "You had to try, right?\n"
                    "Press one of X or Y.\n");
        }
    }

    const char *const blowfish_key = dev ? blowfish_dev_bin : blowfish_retail_bin;
    const char *const signature = dev ? signature_dev : signature_retail;
    const char *const aes_key = dev ? aeskey_dev : aeskey_retail;

    consoleClear();
    iprintf("Checking for %s FIRM\n", dev ? "dev" : "retail");

    ncgc_nplatform_ntr_init(&card, reset_card);
    int32_t r;

    r = ncgc_ninit(&card, NULL);
    if (r) {
        iprintf("ncgc_ninit failed:\n %ld 0x%08lX\n", r, card.raw_chipid);
        goto fail;
    }

    // the SRL header may not be present in ntrboot carts
    card.hdr.key1_romcnt = card.key1.romcnt = 0x81808F8;
    card.hdr.key2_romcnt = card.key2.romcnt = 0x416657;
    ncgc_nsetup_blowfish_as_is(&card, (const void *) blowfish_key);
    r = ncgc_nbegin_key1(&card);
    if (r) {
        iprintf("ncgc_nbegin_key1 failed:\n %ld 0x%08lX 0x%08lX\n", r, card.raw_chipid, card.key1.chipid);
        goto fail;
    }

    r = ncgc_nread_secure_area(&card, secure_area);
    if (r) {
        iprintf("ncgc_nread_secure_area failed:\n %ld 0x%08lX 0x%08lX\n", r, card.raw_chipid, card.key1.chipid);
        goto fail;
    }

    consoleClear();
    iprintf("Checking for %s FIRM\n", dev ? "dev" : "retail");

    firm_hdr_t *hdr = (firm_hdr_t *) (secure_area + 0x3E00);
    if (memcmp(&hdr->magic, "FIRM", 4)) {
        iprintf("FIRM magic not found\n"
                " 0x%08lX\n", hdr->magic);
        // don't even bother, it's probably garbage
        goto fail;
    } else {
        iprintf("FIRM magic OK\n");
    }

    if (memcmp(hdr->signature, signature, 0x100)) {
        iprintf("Signature isn't sighax\n");
    } else {
        iprintf("Signature OK\n");
    }

    bool ok = true;
    bool need_key2 = false;
    for (int i = 0; i < 4; ++i) {
        if (!hdr->sections[i].size) {
            continue;
        }

        uint32_t rom_addr = 0x7E00 + hdr->sections[i].offset;
        if (rom_addr >= 0x8000 || rom_addr + hdr->sections[i].size > 0x8000) {
            need_key2 = true;
        }

        if (rom_addr < 0x4000 || rom_addr + hdr->sections[i].size <= 0x4000) {
            ok = false;
            iprintf("Sect %d weird address\n"
                    " 0x%08lX 0x%08lX\n",
                    i, hdr->sections[i].offset, hdr->sections[i].size);
        }

        if (hdr->sections[i].copy_method > 2) {
            ok = false;
            iprintf("Sect %d weird load method\n"
                    " %ld\n",
                    i, hdr->sections[i].copy_method);
        }
    }

    if (need_key2) {
        r = ncgc_nbegin_key2(&card);
        if (r) {
            iprintf("ncgc_nbegin_key2 failed:\n"
                    " %ld 0x%08lX 0x%08lX\n", r, card.raw_chipid, card.key2.chipid);
            goto fail;
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (!hdr->sections[i].size) {
            continue;
        }

        if (!verify_section(i, &card, hdr, secure_area, aes_key)) {
            ok = false;
        }
    }

    if (ok) {
        iprintf("All OK.\n");
    } else {
        iprintf("Errors reported.\n");
    }

    consoleSelect(&bottomScreen);
    print_magic(hdr);
    iprintf("\n");
    print_header_hash(hdr);
    consoleSelect(&topScreen);

fail:
    iprintf("Press B to check another cart.\n");
    wait_for_keys(KEY_B);
}

int main(void) {
    videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
	consoleInit(&bottomScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    consoleSelect(&topScreen);

    sysSetBusOwners(true, true);

    while (true) {
        do_check();
    }

    return 0;
}
