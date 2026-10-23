#pragma once
#include "phys_local.h"
#include <tl/tl_system.h>
#include <platform/arch_size.h>
#include <cassert>
#include <cstdint>

// The slot free list head is one 64-bit word swapped with a 64-bit CAS: a pointer plus an ABA tag bumped on every
// push and pop. On 32-bit it is the retail {low dword = pointer, high dword = tag} pair. On 64-bit the pointer keeps
// the low 48 bits and the tag the high 16 (the word overlays m_ptr, m_tag is unused): user-space addresses use at
// most 47 bits on Windows x64 and 39 bits on the Switch (Horizon AArch64), so no pointer bit is lost, and a 16-bit
// tag still changes between the load and the CAS of a preempted thread unless 65536 pushes or pops ran in between.
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
static inline __int64 phys_tagged_pack(void *ptr, unsigned int tag)
{
    assert(((uintptr_t)ptr >> 48) == 0);
    return (__int64)(((unsigned __int64)tag << 48) | (uintptr_t)ptr);
}
static inline char *phys_tagged_ptr(__int64 word)
{
    return (char *)(uintptr_t)((unsigned __int64)word & 0x0000FFFFFFFFFFFFull);
}
static inline unsigned int phys_tagged_tag(__int64 word)
{
    return (unsigned int)((unsigned __int64)word >> 48);
}
#else
static inline __int64 phys_tagged_pack(void *ptr, unsigned int tag)
{
    return ((__int64)tag << 32) | (unsigned int)(uintptr_t)ptr;
}
static inline char *phys_tagged_ptr(__int64 word)
{
    return (char *)(uintptr_t)(unsigned int)word;
}
static inline unsigned int phys_tagged_tag(__int64 word)
{
    return (unsigned int)((unsigned __int64)word >> 32);
}
#endif

struct tagged_void_pointer_t // sizeof=0x8
{                                       // XREF: phys_slot_pool/r
    void *m_ptr;
    unsigned int m_tag;

    tagged_void_pointer_t() noexcept
        : m_ptr(nullptr), m_tag(0) {
    }

    tagged_void_pointer_t(void *ptr, unsigned int tag) noexcept
        : m_ptr(ptr), m_tag(tag) {
    }
};

struct alignas(8) phys_slot_pool // sizeof=0x18
{                                       // XREF: phys_memory_manager/r
    struct extra_info // sizeof=0x8
    {
        phys_slot_pool *m_slot_pool_owner;
        void *m_allocation_owner;
    };
    volatile tagged_void_pointer_t m_first_free_slot;
    unsigned int m_map_key;
    phys_slot_pool *m_hash_next;
    volatile unsigned int m_total_slot_count;
    volatile unsigned int m_allocated_slot_count;

    void init(unsigned int slot_size, unsigned int slot_alignment);
    static inline unsigned int encode_size_alignment(unsigned int size, unsigned int alignment)
    {
        if (size > 0xFFFF && _tlAssert("source/phys_mem_new.cpp", 38, "size <= 0xFFFF", ""))
            __debugbreak();
        if (alignment > 0xFFFF && _tlAssert("source/phys_mem_new.cpp", 39, "alignment <= 0xFFFF", ""))
            __debugbreak();
        return size | (alignment << 16);
    }

    void  extra_info_init(char *slot);
    void  extra_info_allocate(char *slot);
    void  extra_info_free(unsigned __int8 *slot);
    void  validate_slot(char *slot);
    void  free_slot(unsigned __int8 *slot);
    char *allocate_slot();
};
static_assert(sizeof(phys_slot_pool) == BO1_ARCH_SIZE(0x18, 40));

struct phys_memory_manager // sizeof=0x3D0
{
    char *m_buffer_start;
    char *m_buffer_end;
    char *m_buffer_cur;
    int m_list_slot_pool_count;
    minspec_read_write_mutex m_slot_pool_map_mutex;
    minspec_hash_table<phys_slot_pool,64> m_slot_pool_map;
    minspec_mutex m_slot_pool_allocate_mutex;
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    alignas(8) phys_slot_pool m_list_preallocated_slot_pools[28];
    int m_list_preallocated_slot_pools_count;
    // padding byte
    // padding byte
    // padding byte
    // padding byte

    phys_memory_manager(char *memory_buffer, int memory_buffer_size);

    uintptr_t allocate(unsigned int size, unsigned int alignment);
    phys_slot_pool *allocate_slot_pool();
    phys_slot_pool *get_slot_pool(unsigned int slot_size, unsigned int slot_alignment);
};

static_assert(sizeof(phys_memory_manager) == BO1_ARCH_SIZE(0x3D0, 1696));

void __cdecl phys_memory_manager_term();
void __cdecl ppu_pmm_get_linear_buffer(char ***linear_buffer_cur, char **linear_buffer_end);
void __cdecl transient_allocator_update_largest_size();

void *__cdecl PMM_PERM_ALLOCATE(unsigned int size, unsigned int alignment);
char *__cdecl PMM_ALLOC(unsigned int size, unsigned int alignment);
void __cdecl PMM_FREE(unsigned __int8 *ptr, unsigned int size, unsigned int alignment);
char *__cdecl PSP_ALLOC(phys_slot_pool *slot_pool);
void __cdecl PSP_FREE(phys_slot_pool *slot_pool, unsigned __int8 *slot);
phys_slot_pool *__cdecl GET_PHYS_SLOT_POOL(unsigned int size, unsigned int alignment);
void __cdecl PMM_VALIDATE(char *ptr, unsigned int size, unsigned int alignment);

void __cdecl phys_memory_manager_init(void *const memory_buffer, int memory_buffer_size);