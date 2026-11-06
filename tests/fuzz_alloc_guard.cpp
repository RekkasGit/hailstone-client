// Replaces the global operator new so a parser that trusts a bogus length field fails the fuzz tests instead of
// exhausting memory (see fuzz_support.h). Only requests above kMaxAllocation made while the guard is armed throw.

#include <atomic>
#include <cstdlib>
#include <new>

#include "fuzz_support.h"

namespace {
std::atomic<bool> g_armed{false};
std::atomic<std::size_t> g_largest{0};

void* guarded_alloc(std::size_t n) {
    if (g_armed.load(std::memory_order_relaxed)) {
        std::size_t seen = g_largest.load(std::memory_order_relaxed);
        while (n > seen && !g_largest.compare_exchange_weak(seen, n, std::memory_order_relaxed)) {}
        if (n > nmsc::fuzz::kMaxAllocation) throw nmsc::fuzz::HugeAllocation();
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
} // namespace

namespace nmsc::fuzz {
void arm_allocation_guard(bool on) { g_armed.store(on, std::memory_order_relaxed); }
void reset_largest_allocation() { g_largest.store(0, std::memory_order_relaxed); }
std::size_t largest_allocation() { return g_largest.load(std::memory_order_relaxed); }
} // namespace nmsc::fuzz

void* operator new(std::size_t n) { return guarded_alloc(n); }
void* operator new[](std::size_t n) { return guarded_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return guarded_alloc(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return guarded_alloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
