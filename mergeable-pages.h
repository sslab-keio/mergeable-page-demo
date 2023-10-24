#pragma once

#include <stdbool.h>
#include <stdint.h>

extern uint64_t hcr3, ncr3, gcr3;
extern uint8_t cur_asid;
extern char __mem[4096 * 64];

enum rmpe_type {
    RMPE_SHARED = 0,
    RMPE_PRIVATE = 1,
    RMPE_MERGEABLE = 2,
    RMPE_LEAF = 3,
};
struct rmpe {
    int asid;
    enum rmpe_type type;
    uint64_t gpa; // pointer to RMP leaf when fixed

    bool validated;
    bool fixed;
};
typedef uint64_t pte_t;

#define REF(addr) ((typeof(addr))(translate((uint64_t)addr, false) + (uint64_t)__mem))
#define REF_MUT(addr) ((typeof(addr))(translate((uint64_t)addr, true) + (uint64_t)__mem))
#define WRITE(addr, val) \
    do { \
        *(typeof(val) *)(translate((uint64_t)addr, true) + (uint64_t)__mem) = (val); \
    } while (0)
#define REF_RAW(addr) ((typeof(addr))((uint64_t)addr + (uint64_t)__mem))

#define FLAGS_MASK ((1UL << 12) - 1)
#define PFN_MASK (((1UL << 52) - 1) & ~FLAGS_MASK)
#define PFN_SHIFT 12
#define PTE_PRESENT 1UL
#define PTE_WRITE 2UL
#define PTE_PRIVATE ((uint64_t)RMPE_PRIVATE << 52)
#define PTE_MERGEABLE ((uint64_t)RMPE_MERGEABLE << 52)
#define PTE_RMPE_TYPE(pte) ((enum rmpe_type)(((pte)&UPPER_FLAGS_MASK) >> UPPER_FLAGS_SHIFT))
#define UPPER_FLAGS_MASK (3UL << 52)
#define UPPER_FLAGS_SHIFT 52

void rmpinit(uint64_t _rmp_base, uint64_t _rmp_end);
uint64_t translate(uint64_t addr, bool write);
void pvalidate(uint64_t gva, enum rmpe_type type);
void pfix(uint64_t hpa, uint64_t pleaf);
void punfix(uint64_t hpa);
void pmerge(uint64_t hpa1, uint64_t hpa2);
void punmerge(uint64_t hpa1, uint64_t hpa2, int asid);
void rmpupdate(uint64_t hpa, uint64_t gpa, int asid, enum rmpe_type type);
