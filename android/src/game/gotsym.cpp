/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "gotsym.hpp"

#include <elf.h>
#include <cstring>

#if defined(__arm__)
#include <link.h>
#endif

namespace mla { namespace gtasa {

static const uint32_t kRelAbs32   = 2;      // R_ARM_ABS32
static const uint32_t kRelGlobDat = 21;     // R_ARM_GLOB_DAT

#if defined(__arm__)
namespace {
struct Search { const char* suffix; ImageInfo* out; bool found; };

int OnImage(struct dl_phdr_info* info, size_t, void* data)
{
    Search* s = (Search*)data;
    if(!info->dlpi_name) return 0;

    size_t n = std::strlen(info->dlpi_name), m = std::strlen(s->suffix);
    if(n < m || std::strcmp(info->dlpi_name + n - m, s->suffix) != 0) return 0;

    s->out->bias  = (uintptr_t)info->dlpi_addr;
    s->out->phdr  = info->dlpi_phdr;
    s->out->phnum = info->dlpi_phnum;
    s->found = true;
    return 1;                       // stop
}
}
#endif

bool FindLoadedImage(const char* nameSuffix, ImageInfo* out)
{
#if defined(__arm__)
    Search s = { nameSuffix, out, false };
    dl_iterate_phdr(&OnImage, &s);
    return s.found;
#else
    (void)nameSuffix; (void)out;
    return false;
#endif
}

bool ImageFromMemory(uintptr_t base, ImageInfo* out)
{
    if(!base || !out) return false;

    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)base;
    if(std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32 || eh->e_phoff == 0)
        return false;

    const Elf32_Phdr* ph = (const Elf32_Phdr*)(base + eh->e_phoff);
    out->bias = base;
    for(unsigned i = 0; i < eh->e_phnum; ++i)
        if(ph[i].p_type == PT_LOAD) { out->bias = base - ph[i].p_vaddr; break; }
    out->phdr  = ph;
    out->phnum = eh->e_phnum;
    return true;
}

uintptr_t FindGotSlot(const ImageInfo& image, const char* symbol)
{
    if(!image.phdr || !image.phnum || !symbol) return 0;

    const Elf32_Phdr* ph = (const Elf32_Phdr*)image.phdr;
    const Elf32_Dyn* dyn = nullptr;
    for(unsigned i = 0; i < image.phnum; ++i)
        if(ph[i].p_type == PT_DYNAMIC) dyn = (const Elf32_Dyn*)(image.bias + ph[i].p_vaddr);
    if(!dyn) return 0;

    // The values of the dynamic section are addresses relative to the image, unless the loader already made them absolute
    auto fix = [&](uintptr_t p) -> uintptr_t { return (image.bias && p >= image.bias)? p : image.bias + p; };

    uintptr_t symtab = 0, strtab = 0, rel = 0, jmprel = 0;
    size_t relsz = 0, pltrelsz = 0;
    for(const Elf32_Dyn* d = dyn; d->d_tag != DT_NULL; ++d)
    {
        switch(d->d_tag)
        {
            case DT_SYMTAB:   symtab   = fix(d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab   = fix(d->d_un.d_ptr); break;
            case DT_REL:      rel      = fix(d->d_un.d_ptr); break;
            case DT_RELSZ:    relsz    = d->d_un.d_val;      break;
            case DT_JMPREL:   jmprel   = fix(d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val;      break;
        }
    }
    if(!symtab || !strtab) return 0;

    struct Table { uintptr_t addr; size_t size; } tables[2] = { { rel, relsz }, { jmprel, pltrelsz } };
    for(auto& t : tables)
    {
        if(!t.addr) continue;
        const Elf32_Rel* r = (const Elf32_Rel*)t.addr;
        for(size_t i = 0; i < t.size / sizeof(Elf32_Rel); ++i)
        {
            uint32_t type = ELF32_R_TYPE(r[i].r_info);
            uint32_t sym  = ELF32_R_SYM(r[i].r_info);
            if(sym == 0 || (type != kRelGlobDat && type != kRelAbs32)) continue;

            const Elf32_Sym* s = (const Elf32_Sym*)symtab + sym;
            const char* name = (const char*)strtab + s->st_name;
            if(std::strcmp(name, symbol) == 0)
                return image.bias + r[i].r_offset;
        }
    }
    return 0;
}

uintptr_t ResolveDataSymbol(const ImageInfo& image, const char* symbol, uintptr_t exported, bool* relocated)
{
    if(relocated) *relocated = false;

    uintptr_t slot = FindGotSlot(image, symbol);
    if(!slot) return exported;

    uintptr_t current = (uintptr_t)(*(volatile const uint32_t*)slot);       // 32 bits ELF: the slot is 4 bytes wide
    if(current == 0) return exported;

    if(relocated) *relocated = (current != exported);
    return current;
}

}} // namespace mla::gtasa
