#include "MemoryScanner.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>

std::vector<uintptr_t> ScanForDoubleTriples(HANDLE process, double x, double y, double z,
                                            double tolerance, size_t maxResults) {
    std::vector<uintptr_t> hits;
    constexpr size_t kChunk = 4u << 20;        // 4 MB per ReadProcessMemory call
    constexpr size_t kTriple = 3 * sizeof(double);
    std::vector<unsigned char> buf(kChunk + kTriple);

    auto readable = [](const MEMORY_BASIC_INFORMATION& m) {
        if (m.State != MEM_COMMIT || m.Type != MEM_PRIVATE) return false;
        if (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        constexpr DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (m.Protect & ok) != 0;
    };

    uintptr_t addr = 0;
    const uintptr_t kMax = 0x00007FFFFFFFFFFFull;
    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < kMax && VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof mbi) == sizeof mbi) {
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (readable(mbi)) {
            for (uintptr_t off = regionBase; off < regionEnd; off += kChunk) {
                // Read a little past the chunk so a triple straddling the boundary isn't missed.
                size_t want = static_cast<size_t>(std::min<uintptr_t>(kChunk + kTriple, regionEnd - off));
                SIZE_T got = 0;
                if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(off), buf.data(), want, &got) || got < kTriple)
                    continue;  // region may have been freed between query and read
                // Chunk starts are page-aligned, so stepping by 8 keeps 8-byte alignment.
                for (size_t i = 0; i < kChunk && i + kTriple <= got; i += 8) {
                    double d[3];
                    std::memcpy(d, buf.data() + i, kTriple);
                    if (std::fabs(d[0] - x) <= tolerance && std::fabs(d[1] - y) <= tolerance &&
                        std::fabs(d[2] - z) <= tolerance) {
                        hits.push_back(off + i);
                        if (hits.size() >= maxResults) return hits;
                    }
                }
            }
        }
        if (regionEnd <= addr) break;  // overflow guard
        addr = regionEnd;
    }
    return hits;
}
