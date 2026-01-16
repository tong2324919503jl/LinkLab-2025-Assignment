#include "fle.hpp"
#include <iomanip>
#include <iostream>

void FLE_nm(const FLEObject& obj)
{
    // TODO: 实现符号表显示工具  
    for (const auto& sym : obj.symbols) {
        if (sym.section.empty()) continue;

        char type_char = '?';
        bool is_text = sym.section == ".text" || 
                       (sym.section.size() >= 6 && sym.section.substr(0, 6) == ".text.");
        bool is_data = sym.section == ".data" ||
                       (sym.section.size() >= 6 && sym.section.substr(0, 6) == ".data.");
        bool is_bss  = sym.section == ".bss";
        bool is_rodata = sym.section == ".rodata" ||
                         (sym.section.size() >= 8 && sym.section.substr(0, 8) == ".rodata.");

        if (sym.type == SymbolType::WEAK) {
            if (is_text) {
                type_char = 'W';
            } else if (is_data || is_bss || is_rodata) {
                type_char = 'V';
            }
        } else {
            if (is_text) {
                type_char = (sym.type == SymbolType::GLOBAL) ? 'T' : 't';
            } else if (is_data) {
                type_char = (sym.type == SymbolType::GLOBAL) ? 'D' : 'd';
            } else if (is_bss) {
                type_char = (sym.type == SymbolType::GLOBAL) ? 'B' : 'b';
            } else if (is_rodata) {
                type_char = (sym.type == SymbolType::GLOBAL) ? 'R' : 'r';
            }
        }

        if (type_char == '?') continue;

        std::cout << std::setfill('0') << std::setw(16) << std::hex << sym.offset;
        std::cout << " " << type_char << " " << sym.name << "\n";
    }
    throw std::runtime_error("Not implemented");
}
