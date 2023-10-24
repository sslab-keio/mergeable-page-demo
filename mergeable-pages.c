#include "mergeable-pages.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char __mem[4096 * 64];
uint64_t hcr3, ncr3, gcr3;
uint8_t cur_asid;
static struct rmpe *rmp;
uint64_t rmp_base, rmp_end;
#define rmp ((struct rmpe *)rmp_base)

// we assume address space is only 4KB*512 and CPU supports only 1-level page
// translation

#define REF_RAW(addr) ((typeof(addr))((uint64_t)addr + (uint64_t)__mem))

#define TRANSLATE_PRIVATE 1UL
#define TRANSLATE_MERGEABLE 2UL
#define TRANSLATE_RMPE_TYPE(_flags) ((enum rmpe_type)((_flags)&3UL))
#define TRANSLATE_WRITE 8UL
#define TRANSLATE_IGNORE_VALIDATED 16UL

#define RMPLE_PRESENT 1UL

void rmpinit(uint64_t _rmp_base, uint64_t _rmp_end) {
    _rmp_base &= PFN_MASK;
    _rmp_end &= PFN_MASK;
    rmp_base = _rmp_base;
    rmp_end = _rmp_end;
}

static void __check_rmp(uint64_t hpa, uint64_t gpa, uint64_t *flags) {
    hpa &= PFN_MASK;
    gpa &= PFN_MASK;

    assert(!(rmp_base <= hpa && hpa < rmp_end));
    uint64_t rmp_covered_end = (rmp_base - rmp_end) / sizeof(struct rmpe) * 4096;
    if (hpa >= rmp_covered_end)
        return;

    struct rmpe *rmpe = REF_RAW(&rmp[hpa >> 12]);
    assert(rmpe->type == TRANSLATE_RMPE_TYPE(*flags));

    if (rmpe->type == RMPE_LEAF)
        assert(cur_asid == 0);

    if (rmpe->type == RMPE_SHARED)
        return;

    if (*flags & TRANSLATE_WRITE)
        assert(rmpe->type != RMPE_LEAF && !(rmpe->type == RMPE_MERGEABLE && rmpe->fixed));
    if (!(*flags & TRANSLATE_IGNORE_VALIDATED))
        assert(rmpe->validated);

    if (rmpe->type == RMPE_MERGEABLE && rmpe->fixed) {
        uint64_t *leaf = (uint64_t *)REF_RAW(rmpe->gpa);
        assert((leaf[cur_asid] & RMPLE_PRESENT) && (leaf[cur_asid] & PFN_MASK) == gpa);
    } else if (rmpe->type == RMPE_MERGEABLE || rmpe->type == RMPE_PRIVATE) {
        assert(rmpe->gpa == gpa && rmpe->asid == cur_asid);
    }
}

static uint64_t __gpa2hpa(uint64_t gpa, uint64_t *flags) {
    assert(ncr3 != -1);

    pte_t npte = *REF_RAW(&((pte_t *)ncr3)[gpa >> 12]);
    assert(npte & PTE_PRESENT);
    if (*flags & TRANSLATE_WRITE)
        assert(npte & PTE_WRITE);

    enum rmpe_type npte_rmpe_type = PTE_RMPE_TYPE(npte);
    enum rmpe_type parent_rmpe_type = TRANSLATE_RMPE_TYPE(*flags);
    assert((parent_rmpe_type == npte_rmpe_type) || !parent_rmpe_type || !npte_rmpe_type);
    if (npte_rmpe_type)
        *flags |= npte_rmpe_type;

    uint64_t hpa = (npte & PFN_MASK) | (gpa & FLAGS_MASK);
    return hpa;
}

static uint64_t __gva2gpa(uint64_t gva, uint64_t *flags) {
    if (gcr3 == -1)
        return gva;

    uint64_t null_flags = 0;
    pte_t *gpt = (pte_t *)__gpa2hpa(gcr3, &null_flags);

    pte_t gpte = *REF_RAW(&gpt[gva >> 12]);
    assert(gpte & PTE_PRESENT);
    if (*flags & TRANSLATE_WRITE)
        assert(gpte & PTE_WRITE);

    enum rmpe_type gpte_rmpe_type = PTE_RMPE_TYPE(gpte);
    enum rmpe_type parent_rmpe_type = TRANSLATE_RMPE_TYPE(*flags);
    assert((parent_rmpe_type == gpte_rmpe_type) || !parent_rmpe_type || !gpte_rmpe_type);
    if (gpte_rmpe_type)
        *flags |= gpte_rmpe_type;

    uint64_t gpa = (gpte & PFN_MASK) | (gva & FLAGS_MASK);
    return gpa;
}

static uint64_t __hva2hpa(uint64_t hva, uint64_t *flags) {
    if (hcr3 == -1)
        return hva;

    pte_t hpte = *REF_RAW(&((pte_t *)hcr3)[hva >> 12]);
    assert(hpte & PTE_PRESENT);
    if (*flags & TRANSLATE_WRITE)
        assert(hpte & PTE_WRITE);

    uint64_t hpa = (hpte & PFN_MASK) | (hva & FLAGS_MASK);
    return hpa;
}

static uint64_t __translate(uint64_t addr, uint64_t flags) {
    assert((flags & ~(TRANSLATE_WRITE)) == 0UL); // accepts only certain flags

    if (cur_asid == 0) {
        uint64_t hpa = __hva2hpa(addr, &flags);
        __check_rmp(hpa, (uint64_t)-1L, &flags);
        return hpa;
    } else {
        uint64_t gpa = __gva2gpa(addr, &flags);
        uint64_t hpa = __gpa2hpa(gpa, &flags);
        __check_rmp(hpa, gpa, &flags);
        return hpa;
    }
}

uint64_t translate(uint64_t addr, bool write) {
    uint64_t flags = 0UL;
    if (write)
        flags |= TRANSLATE_WRITE;
    return __translate(addr, flags);
}

void pvalidate(uint64_t gva, enum rmpe_type type) {
    assert(cur_asid != 0);
    uint64_t flags = TRANSLATE_IGNORE_VALIDATED;
    uint64_t gpa = __gva2gpa(gva, &flags);
    uint64_t hpa = __gpa2hpa(gva, &flags);
    __check_rmp(hpa, gpa, &flags);
    struct rmpe *rmpe = REF_RAW(&rmp[hpa >> 12]);
    rmpe->validated = true;
}

void pfix(uint64_t hpa, uint64_t pleaf) {
    struct rmpe *rmpe_for_leaf = REF_RAW(&rmp[pleaf >> 12]);
    assert(rmpe_for_leaf->type == RMPE_LEAF);

    struct rmpe *rmpe = REF_RAW(&rmp[hpa >> 12]);
    assert(rmpe->type == RMPE_MERGEABLE && !rmpe->fixed && rmpe->validated);
    rmpe->fixed = true;

    uint64_t *leaf = (uint64_t *)REF_RAW(pleaf);
    memset(leaf, 0, 4096);
    leaf[rmpe->asid] = rmpe->gpa | RMPLE_PRESENT;
    rmpe->gpa = pleaf;

    // then clear TLB
}

void punfix(uint64_t hpa) {
    struct rmpe *rmpe = REF_RAW(&rmp[hpa >> 12]);
    assert(rmpe->fixed && rmpe->validated);
    uint64_t *leaf = (uint64_t *)REF_RAW(rmpe->gpa);
    assert(leaf[rmpe->asid] & RMPLE_PRESENT);
    uint64_t gpa = leaf[rmpe->asid] & PFN_MASK;
    rmpe->fixed = false;
    rmpe->gpa = gpa;
}

// merge hpa2 into hpa1
void pmerge(uint64_t hpa1, uint64_t hpa2) {
    struct rmpe *rmpe1 = REF_RAW(&rmp[hpa1 >> 12]);
    assert(rmpe1->type == RMPE_MERGEABLE && rmpe1->fixed && rmpe1->validated);
    struct rmpe *rmpe2 = REF_RAW(&rmp[hpa2 >> 12]);
    assert(rmpe2->type == RMPE_MERGEABLE && !rmpe2->fixed && rmpe2->validated && rmpe2->asid != 0);

    assert(!memcmp((void *)REF_RAW(hpa1), (void *)REF_RAW(hpa2), 4096));

    uint64_t *leaf = (uint64_t *)REF_RAW(rmpe1->gpa);
    assert(leaf[rmpe2->asid] == 0UL);
    leaf[rmpe2->asid] = rmpe2->gpa | RMPLE_PRESENT;

    memset(rmpe2, 0, sizeof(*rmpe2));
    memset((void *)REF_RAW(hpa2), 0, 4096);
}

// copy merged page at hPA1 to hPA2 with ASID
void punmerge(uint64_t hpa1, uint64_t hpa2, int asid) {
    struct rmpe *rmpe1 = (struct rmpe *)REF_RAW(&rmp[hpa1 >> 12]);
    struct rmpe *rmpe2 = (struct rmpe *)REF_RAW(&rmp[hpa2 >> 12]);
    assert(rmpe1->type == RMPE_MERGEABLE && rmpe1->fixed);
    assert(rmpe2->type == RMPE_SHARED);

    pte_t *leaf = (pte_t *)rmpe1->gpa;
    uint64_t gpa2 = *REF_RAW(&leaf[asid]) & PFN_MASK;
    assert(gpa2);
    rmpe2->asid = asid;
    rmpe2->type = RMPE_MERGEABLE;
    rmpe2->gpa = gpa2;
    rmpe2->validated = true;

    memcpy((void *)REF_RAW(hpa2), (void *)REF_RAW(hpa1), 4096);
}

void rmpupdate(uint64_t hpa, uint64_t gpa, int asid, enum rmpe_type type) {
    struct rmpe *rmpe = REF_RAW(&rmp[hpa >> 12]);
    assert(rmpe->type != RMPE_LEAF);

    if (asid != rmpe->asid || (rmpe->type != RMPE_SHARED && type == RMPE_SHARED))
        memset((void *)REF_RAW(hpa), 0, 4096);

    rmpe->gpa = gpa;
    rmpe->asid = asid;
    rmpe->type = type;
    rmpe->validated = false;
    // then clear all the TLB entries
}
