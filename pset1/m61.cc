#define M61_DISABLE 1
#include "m61.hh"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <algorithm>

// allocation metadata struct
struct allocation_m
{
    size_t sz; // size of allocation
    bool freed; // true if allocation has been freed
    const char* file; // file from which allocation was called
    long line; // line from which allocation was called
};

struct hh_call_meta
{
    const char* file; // file from which allocation was called
    long line; // line from which allocation was called

    bool operator==(const hh_call_meta &b) const
    {
        return file == b.file && line == b.line;
    }
};

struct hh_call_meta_hasher
{
    size_t operator() (const hh_call_meta &key) const
    {
        // ghetto hash function pulled off https://gist.github.com/beyondwdq/7245893
        return ((uintptr_t) key.file << sizeof(uintptr_t) | key.line);
    }
};

struct hh_meta
{
    long long sz; // total size allocated by call
    long count; // number of allocations made by call
};

struct hh_v_item
{
    hh_call_meta call_meta;
    hh_meta meta;
};

// metadata map
// key: pointer to start of allocation
// value: allocation metatdata struct associated with allocation at pointer
std::unordered_map<void *, allocation_m> mmap;

// heavy hitter map
// key: call location metatdata
// value: total size of every call at location
std::unordered_map<hh_call_meta, hh_meta, hh_call_meta_hasher> hhmap;

// initialize l_stats to 0s
// technically could just use {0} but the compiler yells at me
m61_statistics l_stats = {0, 0, 0, 0, 0, 0, 0, 0};

// allocation terminator
const char trm = '\xff';
// limit percentage to print as heavy_hitter
const float limit = 0.05;

/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    if (sz > SIZE_MAX - sizeof(trm))
    {
        l_stats.nfail++;
        l_stats.fail_size += sz;
        return nullptr;
    }
    void *ptr = base_malloc(sz + sizeof(trm));
    if (ptr)
    {
        if (!l_stats.ntotal)
        {
            l_stats.heap_min = (uintptr_t) ptr;
            l_stats.heap_max = (uintptr_t) ptr + sz;
        }
        if ((uintptr_t) ptr + sz > l_stats.heap_max)
        {
            l_stats.heap_max = (uintptr_t) ptr + sz;
        }
        else if ((uintptr_t) ptr < l_stats.heap_min)
        {
            l_stats.heap_min = (uintptr_t) ptr;
        }

        l_stats.ntotal++;
        l_stats.total_size += sz;
        l_stats.nactive++;
        l_stats.active_size += sz;
        mmap[ptr] = {sz, false, file, line};
        ((char *) ptr)[sz] = trm;

        // add to heavy hitters map
        hhmap[(hh_call_meta) {file, line}].sz += sz;
        ++hhmap[(hh_call_meta) {file, line}].count;

        return ptr;
    }
    l_stats.nfail++;
    l_stats.fail_size += sz;
    return nullptr;
}


/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void m61_free(void* ptr, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    if (ptr)
    {
        if ((uintptr_t) ptr < l_stats.heap_min || (uintptr_t) ptr > l_stats.heap_max)
        {
            fprintf(stderr, "MEMORY BUG: %s:%li: invalid free of pointer %p, not in heap\n", file, line, ptr);
            abort();
        }
        else if (!mmap.count(ptr))
        {
            fprintf(stderr, "MEMORY BUG: %s:%li: invalid free of pointer %p, not allocated\n", file, line, ptr);
            for (auto &pair: mmap)
            {
                // check if ptr is between existing ptr and existing ptr + sz
                if ((uintptr_t) pair.first < (uintptr_t) ptr && (uintptr_t) pair.first + mmap[pair.first].sz > (uintptr_t) ptr)
                {
                    fprintf(
                        stderr,
                        "   %s:%li: %p is %li bytes inside a %li byte region allocated here\n",
                        mmap[pair.first].file,
                        mmap[pair.first].line,
                        ptr,
                        (uintptr_t) ptr - (uintptr_t) pair.first,
                        mmap[pair.first].sz
                    );
                }
            }
            abort();
        }
        else if (mmap[ptr].freed)
        {
            fprintf(stderr, "MEMORY BUG: %s:%li: invalid free of pointer %p, double free\n", file, line, ptr);
            abort();
        }
        else if (((char *) ptr)[mmap[ptr].sz] != trm)
        {
            fprintf(stderr, "MEMORY BUG: %s:%li: detected wild write during free of pointer %p\n", file, line, ptr);
            abort();
        }

        base_free(ptr);
        l_stats.nactive--;
        l_stats.active_size -= mmap[ptr].sz;
        mmap[ptr].freed = true;
    }
}


/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void* m61_calloc(size_t nmemb, size_t sz, const char* file, long line) {
    size_t toverflow = nmemb * sz;
    if (nmemb != 0 && toverflow / nmemb != sz)
    {
        l_stats.nfail++;
        return nullptr;
    }
    void* ptr = m61_malloc(nmemb * sz, file, line);
    if (ptr) {
        memset(ptr, 0, nmemb * sz);
    }
    return ptr;
}

/// m61_realloc(ptr, sz, file, line)
///    Reallocate the dynamic memory pointed to by `ptr` to hold at least
///    `sz` bytes, returning a pointer to the new block. If `ptr` is
///    `nullptr`, behaves like `m61_malloc(sz, file, line)`. If `sz` is 0,
///    behaves like `m61_free(ptr, file, line)`. The allocation request
///    was at location `file`:`line`.

void* m61_realloc(void* ptr, size_t sz, const char* file, long line)
{
    if (sz == 0)
    {
        m61_free(ptr, file, line);
        return nullptr;
    }
    else
    {
        char *new_ptr = (char *) m61_malloc(sz, file, line);
        if (ptr)
        {
            for (size_t i = 0; i < sz; ++i)
            {
                new_ptr[i] = ((char *) ptr)[i];
            }
        }

        m61_free(ptr, file, line);
        return (void *) new_ptr;
    }
}


/// m61_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_get_statistics(m61_statistics* stats) {
    // Stub: set all statistics to enormous numbers
    // memset(stats, 255, sizeof(m61_statistics));
    *stats = l_stats;
}


/// m61_print_statistics()
///    Print the current memory statistics.

void m61_print_statistics() {
    m61_statistics stats;
    m61_get_statistics(&stats);

    printf(
        "alloc count: active %10llu   total %10llu   fail %10llu\n",
        stats.nactive, stats.ntotal, stats.nfail
    );
    printf(
        "alloc size:  active %10llu   total %10llu   fail %10llu\n",
        stats.active_size, stats.total_size, stats.fail_size
    );
}


/// m61_print_leak_report()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_print_leak_report() {
    for (auto &pair: mmap)
    {
        if (!pair.second.freed)
        {
            fprintf(
                stdout,
                "LEAK CHECK: %s:%li: allocated object %p with size %li\n",
                pair.second.file,
                pair.second.line,
                pair.first,
                pair.second.sz
            );
        }
    }
}


/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
    std::vector<hh_v_item> hh_v;
    for (auto &pair: hhmap)
    {
        hh_v.push_back((hh_v_item) {pair.first, pair.second});
    }


    // heavy hitters
    std::sort(hh_v.begin(), hh_v.end(), [](const hh_v_item &a, hh_v_item &b) {
                                            return a.meta.sz > b.meta.sz;
                                        });
    auto it_hhv = hh_v.begin();
    while (it_hhv != hh_v.end())
    {
        float percent = (float) it_hhv->meta.sz / l_stats.total_size;
        if (percent < limit)
        {
            break;
        }

        fprintf(
            stdout,
            "HEAVY HITTER: %s:%li: %lli bytes (~%.1f%%)\n",
            it_hhv->call_meta.file,
            it_hhv->call_meta.line,
            it_hhv->meta.sz,
            percent * 100
        );

        it_hhv++;
    }


    // frequent allocations
    std::sort(hh_v.begin(), hh_v.end(), [](const hh_v_item &a, hh_v_item &b) {
                                            return a.meta.count > b.meta.count;
                                        });
    it_hhv = hh_v.begin();
    while (it_hhv != hh_v.end())
    {
        float percent = (float) it_hhv->meta.count / l_stats.ntotal;
        if (percent < limit)
        {
            break;
        }

        fprintf(
            stdout,
            "FREQUENTLY ALLOCATED: %s:%li: %li count (~%.1f%%)\n",
            it_hhv->call_meta.file,
            it_hhv->call_meta.line,
            it_hhv->meta.count,
            percent * 100
        );

        it_hhv++;
    }
}
