/*
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * Copyright (c) 2003 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * Definitions of functional page table.
 */
#include "mem/page_table.hh"

#include <string>

#include "base/compiler.hh"
#include "base/trace.hh"
#include "debug/MMU.hh"
#include "sim/faults.hh"
#include "sim/serialize.hh"

namespace gem5
{

void
EmulationPageTable::map(Addr vaddr, Addr paddr, int64_t size, uint64_t flags)
{
    bool clobber = flags & Clobber;
    // starting address must be page aligned
    assert(pageOffset(vaddr) == 0);

    // Detect simulated huge page allocation (64KB chunk of 4KB pages)
    bool isHugePage = (size == 64 * 1024);
    if (isHugePage) {
        Addr base64kAlignedVaddr = roundDown(vaddr, 64 * 1024);
        DPRINTF(MMU, "Simulating huge page at: %#x (%d KB)\n", vaddr, size / 1024);

        // Track it
        page64KBStats[base64kAlignedVaddr] = PageAccessStats();
    }

    DPRINTF(MMU, "Allocating Page: %#x-%#x\n", vaddr, vaddr + size);

    while (size > 0) {
        auto it = pTable.find(vaddr);
        if (it != pTable.end()) {
            // already mapped
            panic_if(!clobber,
                     "EmulationPageTable::allocate: addr %#x already mapped",
                     vaddr);
            it->second = Entry(paddr, flags);
        } else {
            pTable.emplace(vaddr, Entry(paddr, flags));
        }

        size -= _pageSize;
        vaddr += _pageSize;
        paddr += _pageSize;
    }
}

void
EmulationPageTable::remap(Addr vaddr, int64_t size, Addr new_vaddr)
{
    assert(pageOffset(vaddr) == 0);
    assert(pageOffset(new_vaddr) == 0);

    DPRINTF(MMU, "moving pages from vaddr %08p to %08p, size = %d\n", vaddr,
            new_vaddr, size);

    while (size > 0) {
        [[maybe_unused]] auto new_it = pTable.find(new_vaddr);
        auto old_it = pTable.find(vaddr);
        assert(old_it != pTable.end() && new_it == pTable.end());

        pTable.emplace(new_vaddr, old_it->second);
        pTable.erase(old_it);
        size -= _pageSize;
        vaddr += _pageSize;
        new_vaddr += _pageSize;
    }
}

void
EmulationPageTable::getMappings(std::vector<std::pair<Addr, Addr>> *addr_maps)
{
    for (auto &iter : pTable)
        addr_maps->push_back(std::make_pair(iter.first, iter.second.paddr));
}

void
EmulationPageTable::unmap(Addr vaddr, int64_t size)
{
    assert(pageOffset(vaddr) == 0);

    DPRINTF(MMU, "Unmapping page: %#x-%#x\n", vaddr, vaddr + size);

    while (size > 0) {
        auto it = pTable.find(vaddr);
        assert(it != pTable.end());
        pTable.erase(it);
        size -= _pageSize;
        vaddr += _pageSize;
    }
}

bool
EmulationPageTable::isUnmapped(Addr vaddr, int64_t size)
{
    // starting address must be page aligned
    assert(pageOffset(vaddr) == 0);

    for (int64_t offset = 0; offset < size; offset += _pageSize)
        if (pTable.find(vaddr + offset) != pTable.end())
            return false;

    return true;
}

const EmulationPageTable::Entry *
EmulationPageTable::lookup(Addr vaddr)
{
    Addr page_addr = pageAlign(vaddr);
    PTableItr iter = pTable.find(page_addr);
    if (iter == pTable.end())
        return nullptr;
    return &(iter->second);
}

void
EmulationPageTable::checkHugePageHotness(Addr vaddr)
{
    Addr base64 = roundDown(vaddr, 64 * 1024);
    Addr base16 = roundDown(vaddr, 16 * 1024);
    
    auto checkAndEvict = [&](Addr base, size_t size,
                             std::unordered_map<Addr, PageAccessStats> &statMap) {
                            
        auto &stats = statMap[base];

        if (abs(stats.upperHalfAccesses) >= 10 ||
            abs(stats.upperUpperQuarterAccesses) >= 10 ||
            abs(stats.lowerLowerQuarterAccesses) >= 10) {

            for (int i = 0; i < size; i += 4 * 1024) {
                Addr page = base + i;
                std::string regionName;
        
                // Identify region type by offset
                int offset = i;
                /* Use the 3 numbers conservatively. So the upper half number is xor with a quadrant number
                */
                if (offset >= size * 3 / 4) {
                    if ((stats.upperHalfAccesses <= -10) != (stats.upperUpperQuarterAccesses <= -10)) { // if true deallocate
                        pTable.erase(page);
                    } else { // if not deallocating need to demote the page e.g. 64kB -> 16kB or 16kb -> 4kB (this means nothing)
                        if (size == 64 * 1024) {
                            Addr new_vaddr = roundDown(page, 16 * 1024);
                            if (page16KBStats.find(new_vaddr) == page16KBStats.end()) {
                                // Track it
                                page64KBStats[new_vaddr] = PageAccessStats();
                            }
                        }
                    }
                } else if (offset >= size * 1 / 2) {
                    if ((stats.upperHalfAccesses <= -10) != (stats.upperUpperQuarterAccesses >= 10)) { // if true deallocate
                        pTable.erase(page);
                    } else { // if not deallocating need to demote the page e.g. 64kB -> 16kB or 16kb -> 4kB (this means nothing)
                        if (size == 64 * 1024) {
                            Addr new_vaddr = roundDown(page, 16 * 1024);
                            if (page16KBStats.find(new_vaddr) == page16KBStats.end()) {
                                // Track it
                                page64KBStats[new_vaddr] = PageAccessStats();
                            }
                        }
                    }
                } else if (offset >= size * 1 / 4) {
                    if ((stats.upperHalfAccesses >= 10) != (stats.lowerLowerQuarterAccesses >= 10)) { // if true deallocate
                        pTable.erase(page);
                    } else { // if not deallocating need to demote the page e.g. 64kB -> 16kB or 16kb -> 4kB (this means nothing)
                        if (size == 64 * 1024) {
                            Addr new_vaddr = roundDown(page, 16 * 1024);
                            if (page16KBStats.find(new_vaddr) == page16KBStats.end()) {
                                // Track it
                                page64KBStats[new_vaddr] = PageAccessStats();
                            }
                        }
                    }
                } else {
                    if ((stats.upperHalfAccesses >= 10) != (stats.lowerLowerQuarterAccesses <= -10)) { // if true deallocate
                        pTable.erase(page);
                    } else { // if not deallocating need to demote the page e.g. 64kB -> 16kB or 16kb -> 4kB (this means nothing)
                        if (size == 64 * 1024) {
                            Addr new_vaddr = roundDown(page, 16 * 1024);
                            if (page16KBStats.find(new_vaddr) == page16KBStats.end()) {
                                // Track it
                                page64KBStats[new_vaddr] = PageAccessStats();
                            }
                        }
                    }
                }
            }
            // Deallocate the Huge Page entry
            statMap.erase(base);
        }

    };
    checkAndEvict(base64, 64 * 1024, page64KBStats);
    checkAndEvict(base16, 16 * 1024, page16KBStats);
}

bool
EmulationPageTable::translate(Addr vaddr, Addr &paddr)
{
    const Entry *entry = lookup(vaddr);
    if (!entry) {
        DPRINTF(MMU, "Couldn't Translate: %#x\n", vaddr);
        return false;
    }
    paddr = pageOffset(vaddr) + entry->paddr;
    DPRINTF(MMU, "Translating: %#x->%#x\n", vaddr, paddr);
    return true;
}

Fault
EmulationPageTable::translate(const RequestPtr &req)
{
    Addr paddr;
    assert(pageAlign(req->getVaddr() + req->getSize() - 1) ==
           pageAlign(req->getVaddr()));
    if (!translate(req->getVaddr(), paddr))
        return Fault(new GenericPageTableFault(req->getVaddr()));
    req->setPaddr(paddr);
    if ((paddr & (_pageSize - 1)) + req->getSize() > _pageSize) {
        panic("Request spans page boundaries!\n");
        return NoFault;
    }

    Addr vaddr = req->getVaddr();
    Addr base64 = roundDown(vaddr, 64 * 1024);
    Addr base16 = roundDown(vaddr, 16 * 1024);

    auto updateStats = [&](Addr base, size_t size, auto &statMap) {
        auto it = statMap.find(base);
        if (it == statMap.end()) return;
    
        auto &stats = it->second;
        stats.totalAccesses++;
    
        uint32_t offset = vaddr - base;
    
        if (offset >= size / 2) {
            stats.upperHalfAccesses++;
            if (offset >= size * 3 / 4) {
                stats.upperUpperQuarterAccesses++;
            } else {
                stats.upperUpperQuarterAccesses--;
            } 
        } else {
            stats.upperHalfAccesses--;
            if (offset < size / 4) {
                stats.lowerLowerQuarterAccesses++;
            } else{
                stats.lowerLowerQuarterAccesses--;
            }
        }
      
    
        if (stats.totalAccesses == 20)
            checkHugePageHotness(vaddr);
    };

    updateStats(base64, 64 * 1024, page64KBStats);
    updateStats(base16, 16 * 1024, page16KBStats);

    return NoFault;
}

void
EmulationPageTable::PageTableTranslationGen::translate(Range &range) const
{
    const Addr page_size = pt->pageSize();

    Addr next = roundUp(range.vaddr, page_size);
    if (next == range.vaddr)
        next += page_size;
    range.size = std::min(range.size, next - range.vaddr);

    if (!pt->translate(range.vaddr, range.paddr))
        range.fault = Fault(new GenericPageTableFault(range.vaddr));
}

void
EmulationPageTable::serialize(CheckpointOut &cp) const
{
    ScopedCheckpointSection sec(cp, "ptable");
    paramOut(cp, "size", pTable.size());

    PTable::size_type count = 0;
    for (auto &pte : pTable) {
        ScopedCheckpointSection sec(cp, csprintf("Entry%d", count++));

        paramOut(cp, "vaddr", pte.first);
        paramOut(cp, "paddr", pte.second.paddr);
        paramOut(cp, "flags", pte.second.flags);
    }
    assert(count == pTable.size());
}

void
EmulationPageTable::unserialize(CheckpointIn &cp)
{
    int count;
    ScopedCheckpointSection sec(cp, "ptable");
    paramIn(cp, "size", count);

    for (int i = 0; i < count; ++i) {
        ScopedCheckpointSection sec(cp, csprintf("Entry%d", i));

        Addr vaddr;
        UNSERIALIZE_SCALAR(vaddr);
        Addr paddr;
        uint64_t flags;
        UNSERIALIZE_SCALAR(paddr);
        UNSERIALIZE_SCALAR(flags);

        pTable.emplace(vaddr, Entry(paddr, flags));
    }
}

const std::string
EmulationPageTable::externalize() const
{
    std::stringstream ss;
    for (PTable::const_iterator it=pTable.begin(); it != pTable.end(); ++it) {
        ss << std::hex << it->first << ":" << it->second.paddr << ";";
    }
    return ss.str();
}

} // namespace gem5
