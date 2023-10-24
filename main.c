#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mergeable-pages.h"

int main() {
    rmpinit(0UL, 4096UL);

    cur_asid = 0;
    hcr3 = -1;
    const uint64_t HCR3 = 4096UL * 4;
    const uint64_t NCR3_1 = 4096UL * 5;
    const uint64_t NCR3_2 = 4096UL * 6;
    const uint64_t GCR3 = 4096UL * 0;

    // set up host page table
    pte_t *hpt = (pte_t *)HCR3;
    for (int i = 0; i < 512; i++)
        WRITE(&hpt[i], (uint64_t)i << 12 | PTE_PRESENT | PTE_WRITE);
    hcr3 = HCR3;

    // set up nested page tables
    pte_t *npt1 = (pte_t *)NCR3_1;
    WRITE(&npt1[0], 7 * 4096 | PTE_PRESENT | PTE_PRIVATE | PTE_WRITE);
    WRITE(&npt1[1], 8 * 4096 | PTE_PRESENT | PTE_MERGEABLE | PTE_WRITE);
    WRITE(&npt1[2], 9 * 4096 | PTE_PRESENT | PTE_MERGEABLE);
    WRITE(&npt1[3], 10 * 4096 | PTE_PRESENT | PTE_MERGEABLE);
    rmpupdate(7UL * 4096, 0UL * 4096, 1, RMPE_PRIVATE);
    rmpupdate(8UL * 4096, 1UL * 4096, 1, RMPE_MERGEABLE);
    rmpupdate(9UL * 4096, 2UL * 4096, 1, RMPE_MERGEABLE);
    rmpupdate(10UL * 4096, 3UL * 4096, 1, RMPE_MERGEABLE);

    pte_t *npt2 = (pte_t *)NCR3_2;
    WRITE(&npt2[0], 11 * 4096 | PTE_PRESENT | PTE_PRIVATE | PTE_WRITE);
    WRITE(&npt2[1], 12 * 4096 | PTE_PRESENT | PTE_MERGEABLE | PTE_WRITE);
    WRITE(&npt2[2], 13 * 4096 | PTE_PRESENT | PTE_MERGEABLE);
    WRITE(&npt2[3], 14 * 4096 | PTE_PRESENT | PTE_MERGEABLE);
    rmpupdate(11UL * 4096, 0UL * 4096, 2, RMPE_PRIVATE);
    rmpupdate(12UL * 4096, 1UL * 4096, 2, RMPE_MERGEABLE);
    rmpupdate(13UL * 4096, 2UL * 4096, 2, RMPE_MERGEABLE);
    rmpupdate(14UL * 4096, 3UL * 4096, 2, RMPE_MERGEABLE);

    // switch into VM1
    cur_asid = 1;
    ncr3 = NCR3_1;
    gcr3 = -1;
    // in VM1
    // set up guest page table
    pte_t *gpt1 = (pte_t *)GCR3;
    pvalidate((uint64_t)&gpt1[0], RMPE_PRIVATE);
    WRITE(&gpt1[0], 0UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_PRIVATE);
    WRITE(&gpt1[1], 1UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    WRITE(&gpt1[2], 2UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    WRITE(&gpt1[3], 3UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    pvalidate(1UL * 4096, RMPE_MERGEABLE);
    pvalidate(2UL * 4096, RMPE_MERGEABLE);
    pvalidate(3UL * 4096, RMPE_MERGEABLE);
    gcr3 = GCR3;

    // switch into VM2
    cur_asid = 2;
    ncr3 = NCR3_2;
    gcr3 = -1;
    // in VM2
    // set up guest page table
    pte_t *gpt2 = (pte_t *)GCR3;
    pvalidate((uint64_t)&gpt2[0], RMPE_PRIVATE);
    WRITE(&gpt2[0], 0UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_PRIVATE);
    WRITE(&gpt2[1], 1UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    WRITE(&gpt2[2], 2UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    WRITE(&gpt2[3], 3UL * 4096 | PTE_PRESENT | PTE_WRITE | PTE_MERGEABLE);
    pvalidate(1UL * 4096, RMPE_MERGEABLE);
    pvalidate(2UL * 4096, RMPE_MERGEABLE);
    pvalidate(3UL * 4096, RMPE_MERGEABLE);
    gcr3 = GCR3;

    // switch into VM1
    cur_asid = 1;
    ncr3 = NCR3_1;
    gcr3 = GCR3;
    // in VM1
    printf("%lx\n", translate(0x0000UL, false));

    // switch into VM2
    cur_asid = 2;
    ncr3 = NCR3_2;
    gcr3 = GCR3;
    // in VM2
    printf("%lx\n", translate(0x0000UL, false));

    // Merge VM1's "1"st page and VM2's "1"st page

    // switch into VM1
    cur_asid = 1;
    ncr3 = NCR3_1;
    gcr3 = GCR3;
    // in VM1
    memset((void *)REF_MUT(1UL * 4096), 0, 4096); // at 8*4096 in hPA
    strcpy((void *)REF_MUT(1UL * 4096), "hello");

    // switch into VM2
    cur_asid = 2;
    ncr3 = NCR3_2;
    gcr3 = GCR3;
    // in VM2
    memset((void *)REF_MUT(1UL * 4096), 0, 4096); // at 12*4096 in hPA
    strcpy((void *)REF_MUT(1UL * 4096), "hello");
    // merged page must be the same
    // strcpy((void *)REF_MUT(1UL * 4096), "hell");

    // switch into VMM
    cur_asid = 0;
    hcr3 = HCR3;
    // in VMM
    rmpupdate(1UL * 4096, -1L, 0, RMPE_LEAF);                              // prepare RMP leaf page
    pfix(8UL * 4096, 1UL * 4096);                                          // fix VM1's "1"st page (at 8*4096 in hPA)
    pmerge(8UL * 4096, 12UL * 4096);                                       // merge VM2's "1"st page into VM1's "1"st page
    WRITE(&npt2[1], 8UL * 4096 | PTE_PRESENT | PTE_MERGEABLE | PTE_WRITE); // update corresponding NPT

    // switch into VM1
    cur_asid = 1;
    ncr3 = NCR3_1;
    gcr3 = GCR3;
    // in VM1
    // REF_MUT(1UL * 4096); // this access should be invalid cuz the page is now fixed
    puts((char *)REF(1UL * 4096)); // this is ok

    // switch into VM2
    cur_asid = 2;
    ncr3 = NCR3_2;
    gcr3 = GCR3;
    // in VM2
    // REF_MUT(1UL * 4096); // this access should be invalid cuz the page is now fixed
    puts((char *)REF(1UL * 4096)); // this is ok

    // switch into VMM
    cur_asid = 0;
    hcr3 = HCR3;
    // in VMM
    punmerge(8UL * 4096, 12UL * 4096, 2);                                   // unmerge page at 8*4096 into 12*4096
    punfix(8UL * 4096);                                                     // unfix page at 8*4096
    WRITE(&npt2[1], 12UL * 4096 | PTE_PRESENT | PTE_MERGEABLE | PTE_WRITE); // update corresponding NPT

    // switch into VM1
    cur_asid = 1;
    ncr3 = NCR3_1;
    gcr3 = GCR3;
    // in VM1
    strcpy((void *)REF_MUT(1UL * 4096), "foobar"); // this is ok
    puts((char *)REF(1UL * 4096));

    // switch into VM2
    cur_asid = 2;
    ncr3 = NCR3_2;
    gcr3 = GCR3;
    // in VM2
    strcpy((void *)REF_MUT(1UL * 4096), "deadbeef"); // this is ok
    puts((char *)REF(1UL * 4096));

    return 0;
}