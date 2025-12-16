#include "fle.hpp"
#include "string_utils.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {

struct LoadedModule {
    std::string name;
    FLEObject obj;
    uint64_t load_base;
    std::map<std::string, uint64_t> section_addrs;
};

// Global list of loaded modules to maintain loading order
// Order: Main Execution -> Dependency 1 -> Dependency 2 ...
std::vector<LoadedModule> loaded_modules;
std::unordered_set<std::string> loaded_module_names;

// Helper to resolve a symbol across all loaded modules
uint64_t resolve_symbol(const std::string& name)
{
    for (const auto& mod : loaded_modules) {
        for (const auto& sym : mod.obj.symbols) {
            // We search for GLOBAL or WEAK symbols that are defined (not UNDEFINED)
            if (sym.name == name && (sym.type == SymbolType::GLOBAL || sym.type == SymbolType::WEAK)) {
                auto it = mod.section_addrs.find(sym.section);
                if (it != mod.section_addrs.end()) {
                    return it->second + sym.offset;
                }
            }
        }
    }
    throw std::runtime_error("Symbol not found: " + name);
}

void load_module_recursive(const std::string& filename)
{
    if (loaded_module_names.count(filename)) {
        return;
    }

    // Load FLE file
    // Handle potential .fle extension issues if filenames in 'needed' lack it
    FLEObject obj;
    try {
        obj = load_fle(filename);
    } catch (...) {
        try {
            obj = load_fle(filename + ".fle");
        } catch (...) {
            throw std::runtime_error("Could not load dependency: " + filename);
        }
    }

    loaded_module_names.insert(filename);

    // Prepare LoadedModule structure
    LoadedModule mod;
    mod.name = filename;
    mod.obj = obj;

    // Determine load base and map memory
    if (obj.type == ".exe") {
        mod.load_base = 0; // Exe has absolute addresses usually
    } else {
        // For shared objects, we need to find a space.
        // Calculate total size required
        uint64_t min_vaddr = UINT64_MAX;
        uint64_t max_end = 0;
        bool has_segments = false;

        for (const auto& phdr : obj.phdrs) {
            if (phdr.size > 0) {
                if (phdr.vaddr < min_vaddr)
                    min_vaddr = phdr.vaddr;
                uint64_t end = phdr.vaddr + phdr.size;
                if (end > max_end)
                    max_end = end;
                has_segments = true;
            }
        }

        if (has_segments) {
            uint64_t total_size = max_end;
            // Reserve memory region
            void* addr = mmap(NULL, total_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (addr == MAP_FAILED) {
                throw std::runtime_error("Failed to reserve memory for shared library");
            }
            mod.load_base = (uint64_t)addr;
        } else {
            mod.load_base = 0;
        }
    }

    // Map segments
    for (const auto& phdr : obj.phdrs) {
        if (phdr.size == 0)
            continue;

        void* target_addr = (void*)(mod.load_base + phdr.vaddr);
        void* map_res = mmap(target_addr, phdr.size,
            PROT_READ | PROT_WRITE, // Always RW initially for copying and relocation
            MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

        if (map_res == MAP_FAILED) {
            throw std::runtime_error("Failed to map segment " + phdr.name);
        }

        // Copy section data
        auto it = obj.sections.find(phdr.name);
        if (it != obj.sections.end()) {
            // Skip BSS copying
            if (phdr.name != ".bss" && !starts_with(phdr.name, ".bss.")) {
                if (it->second.data.size() > phdr.size) {
                    // Should not happen if FLE is valid, but safety check
                    memcpy(target_addr, it->second.data.data(), phdr.size);
                } else {
                    memcpy(target_addr, it->second.data.data(), it->second.data.size());
                }
            }
        } else {
            throw std::runtime_error("Section data not found for segment: " + phdr.name);
        }

        // Record section address
        mod.section_addrs[phdr.name] = (uint64_t)target_addr;
    }

    // Add to specific list location (Global symbol resolution order)
    loaded_modules.push_back(mod);

    // Recursively load dependencies
    for (const auto& dep : obj.needed) {
        load_module_recursive(dep);
    }
}

} // namespace

void FLE_exec(const FLEObject& obj)
{
    if (obj.type != ".exe") {
        throw std::runtime_error("File is not an executable FLE.");
    }

    // Clear globals for fresh execution
    loaded_modules.clear();
    loaded_module_names.clear();

    // 1. Load Main Executable (Manual setup for the main object provided)
    // We treat the passed object as the first module but we need its name.
    // Since we don't have the filename here, we use a placeholder or obj.name if valid.

    // Actually, recursive loader expects loading from disk.
    // But we already have the main object in memory.
    // We should initialize the main module manually.

    LoadedModule main_mod;
    main_mod.name = obj.name.empty() ? "main" : obj.name;
    main_mod.obj = obj;
    main_mod.load_base = 0;

    // Map Main Executable segments
    for (const auto& phdr : obj.phdrs) {
        if (phdr.size == 0)
            continue;

        void* addr = mmap((void*)phdr.vaddr, phdr.size,
            PROT_READ | PROT_WRITE, // RW for relocations
            MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

        if (addr == MAP_FAILED) {
            throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
        }

        auto it = obj.sections.find(phdr.name);
        if (it == obj.sections.end()) {
            throw std::runtime_error("Section not found: " + phdr.name);
        }

        if (phdr.name != ".bss" && !starts_with(phdr.name, ".bss.")) {
            memcpy(addr, it->second.data.data(), phdr.size);
        }

        main_mod.section_addrs[phdr.name] = phdr.vaddr;
    }

    loaded_modules.push_back(main_mod);
    loaded_module_names.insert(main_mod.name);

    // Load dependencies of main
    for (const auto& dep : obj.needed) {
        load_module_recursive(dep);
    }

    // 2. Perform Relocations for ALL modules
    for (auto& mod : loaded_modules) {

        // Helper lambda to apply a single relocation
        auto apply_reloc = [&](const Relocation& reloc, uint64_t section_base_vaddr) {
            uint64_t sym_addr = resolve_symbol(reloc.symbol);
            uint64_t reloc_addr = mod.load_base + section_base_vaddr + reloc.offset;

            // Check if reloc_addr is valid memory?
            // Ideally we should check, but assuming correctness of ELF/FLE

            switch (reloc.type) {
            case RelocationType::R_X86_64_64:
                *(uint64_t*)reloc_addr = sym_addr + reloc.addend;
                break;
            case RelocationType::R_X86_64_32:
                *(uint32_t*)reloc_addr = (uint32_t)(sym_addr + reloc.addend);
                break;
            case RelocationType::R_X86_64_32S:
                *(int32_t*)reloc_addr = (int32_t)(sym_addr + reloc.addend);
                break;
            case RelocationType::R_X86_64_PC32:
                // S + A - P
                *(uint32_t*)reloc_addr = (uint32_t)(sym_addr + reloc.addend - reloc_addr);
                break;
            }
        };

        // A. Dynamic Relocations (Bonus 2 - GOT)
        // These are typically absolute relocations in the GOT section
        // dyn_relocs have offset relative to load_base?
        // In FLE, headers are segments. dyn_relocs usually point to GOT which is in a segment.
        // Assuming reloc.offset is VMA.
        for (const auto& reloc : mod.obj.dyn_relocs) {
            // For dynamic relocs, offset is usually VMA.
            // So address is load_base + offset.
            // pass 0 as section_base since offset includes it.
            apply_reloc(reloc, 0);
        }

        // B. Section Relocations (Bonus 1 - Text Relocations)
        // Iterate over sections to find relocations
        for (const auto& kv : mod.obj.sections) {
            const auto& name = kv.first;
            const auto& section = kv.second;

            // Check if this section is loaded.
            auto addr_it = mod.section_addrs.find(name);
            if (addr_it == mod.section_addrs.end())
                continue;

            // In FLE, section relocs have offset relative to the section start
            // phdr.vaddr corresponds to the section start VMA relative to Load Base (for SO) or Absolute (for EXE)
            // Wait, for .so, phdr.vaddr is offset from base.
            // But we stored the Absolute Runtime Address in section_addrs.
            // But apply_reloc adds load_base + section_base ...

            // Let's adjust logic.
            // For Main (.exe), load_base = 0. section_addr is absolute.
            // For .so, load_base = allocated. section_addr is absolute = load_base + vaddr.

            // Reloc offset is relative to section start.
            // address = section_absolute_start + reloc.offset.
            // We can pass `section_absolute_start - load_base` as 2nd arg?
            // Or just calculate address and adapt lambda.

            uint64_t section_runtime_addr = addr_it->second;

            for (const auto& reloc : section.relocs) {
                uint64_t sym_addr = resolve_symbol(reloc.symbol);
                uint64_t reloc_addr = section_runtime_addr + reloc.offset;

                switch (reloc.type) {
                case RelocationType::R_X86_64_64:
                    *(uint64_t*)reloc_addr = sym_addr + reloc.addend;
                    break;
                case RelocationType::R_X86_64_32:
                    *(uint32_t*)reloc_addr = (uint32_t)(sym_addr + reloc.addend);
                    break;
                case RelocationType::R_X86_64_32S:
                    *(int32_t*)reloc_addr = (int32_t)(sym_addr + reloc.addend);
                    break;
                case RelocationType::R_X86_64_PC32:
                    *(uint32_t*)reloc_addr = (uint32_t)(sym_addr + reloc.addend - reloc_addr);
                    break;
                }
            }
        }
    }

    // 3. Set Permissions (after all relocations are done)
    for (const auto& mod : loaded_modules) {
        for (const auto& phdr : mod.obj.phdrs) {
            if (phdr.size == 0)
                continue;

            // Find runtime address
            uint64_t addr = mod.load_base + phdr.vaddr;

            mprotect((void*)addr, phdr.size,
                (phdr.flags & PHF::R ? PROT_READ : 0)
                    | (phdr.flags & PHF::W ? PROT_WRITE : 0)
                    | (phdr.flags & PHF::X ? PROT_EXEC : 0));
        }
    }

    // 4. Jump to Entry
    using FuncType = int (*)();
    // Entry is VMA. Main EXE base is 0. So entry is absolute.
    FuncType func = reinterpret_cast<FuncType>(obj.entry);
    func();

    // Should not reach here
    assert(false);
}
