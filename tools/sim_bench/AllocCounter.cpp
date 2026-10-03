#include "AllocCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::uint64_t> gCount{0};
std::atomic<std::uint64_t> gBytes{0};

void* allocate(std::size_t size) {
    gCount.fetch_add(1, std::memory_order_relaxed);
    gBytes.fetch_add(size, std::memory_order_relaxed);
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

// std::aligned_alloc is missing on MSVC, which has its own pair of functions.
void* allocateAligned(std::size_t size, std::size_t alignment) {
    gCount.fetch_add(1, std::memory_order_relaxed);
    gBytes.fetch_add(size, std::memory_order_relaxed);
#ifdef _WIN32
    void* p = _aligned_malloc(size != 0 ? size : 1, alignment);
#else
    // aligned_alloc wants the size to be a multiple of the alignment.
    const std::size_t rounded = ((size != 0 ? size : 1) + alignment - 1) / alignment * alignment;
    void* p = std::aligned_alloc(alignment, rounded);
#endif
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void release(void* p) noexcept { std::free(p); }

void releaseAligned(void* p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

}  // namespace

namespace simbench {

AllocStats allocStats() { return {gCount.load(std::memory_order_relaxed), gBytes.load(std::memory_order_relaxed)}; }

}  // namespace simbench

// Global replacements (must live outside any namespace).
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t a) { return allocateAligned(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) {
    return allocateAligned(size, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
