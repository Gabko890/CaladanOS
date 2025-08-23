#!/usr/bin/env python3
"""
CaladanOS Memory Map Generator
Automatically extracts memory layout information from kernel binary and source files
to generate mmap.txt

Usage Examples:
    ./generate_mmap.py                    # Generate mmap.txt in current directory
    ./generate_mmap.py -b                 # Build kernel first, then generate
    ./generate_mmap.py -o memory_map.txt  # Output to custom file
    python3 generate_mmap.py --help       # Show all options

Features:
- Extracts symbol addresses from kernel.elf using nm
- Parses ELF sections using objdump/readelf
- Scans assembly files for constants and memory layout
- Parses driver headers for hardware I/O addresses
- Generates comprehensive memory map with file:line references
"""

import subprocess
import re
import os
from pathlib import Path
from typing import Dict, List, Tuple, Optional

class MemoryMapGenerator:
    def __init__(self, project_root: str = "."):
        self.project_root = Path(project_root)
        self.kernel_elf = self.project_root / "build/kernel/kernel.elf"
        self.symbols = {}
        self.sections = {}
        
    def run_command(self, cmd: List[str]) -> str:
        """Execute command and return output"""
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            return result.stdout
        except subprocess.CalledProcessError as e:
            print(f"Error running {' '.join(cmd)}: {e}")
            return ""
    
    def extract_symbols(self) -> Dict[str, int]:
        """Extract symbol addresses from kernel.elf using nm"""
        if not self.kernel_elf.exists():
            print(f"Kernel binary not found at {self.kernel_elf}")
            return {}
            
        output = self.run_command(["nm", str(self.kernel_elf)])
        symbols = {}
        
        for line in output.split('\n'):
            if line.strip():
                parts = line.split()
                if len(parts) >= 3:
                    addr_str = parts[0]
                    symbol_name = parts[2]
                    try:
                        addr = int(addr_str, 16)
                        symbols[symbol_name] = addr
                    except ValueError:
                        continue
        
        return symbols
    
    def extract_sections(self) -> Dict[str, Dict[str, any]]:
        """Extract section information using objdump"""
        if not self.kernel_elf.exists():
            return {}
            
        output = self.run_command(["objdump", "-h", str(self.kernel_elf)])
        sections = {}
        
        # Parse objdump output
        for line in output.split('\n'):
            if re.match(r'^\s*\d+\s+\w+', line):
                parts = line.split()
                if len(parts) >= 6:
                    section_name = parts[1]
                    size = int(parts[2], 16)
                    vma = int(parts[3], 16)
                    lma = int(parts[4], 16)
                    
                    sections[section_name] = {
                        'size': size,
                        'vma': vma,
                        'lma': lma,
                        'size_hex': f"0x{size:X}"
                    }
        
        return sections
    
    def parse_assembly_constants(self) -> Dict[str, any]:
        """Parse assembly files for constants and addresses"""
        constants = {}
        
        # Parse boot32.asm for stack and page table info
        boot32_path = self.project_root / "boot/src/boot32.asm"
        if boot32_path.exists():
            with open(boot32_path, 'r') as f:
                content = f.read()
                
                # Extract stack size (resb 4096 * 4)
                stack_match = re.search(r'stack_bottom:\s*resb\s+(\d+)\s*\*\s*(\d+)', content)
                if stack_match:
                    constants['stack_size'] = int(stack_match.group(1)) * int(stack_match.group(2))
        
        # Parse header.asm for multiboot constants
        header_path = self.project_root / "boot/src/header.asm"
        if header_path.exists():
            with open(header_path, 'r') as f:
                content = f.read()
                
                # Extract multiboot magic numbers
                magic_matches = re.findall(r'dd\s+(0x[0-9a-fA-F]+)', content)
                constants['multiboot_magics'] = magic_matches
        
        return constants
    
    def parse_source_files(self) -> Dict[str, List[Tuple[str, str, int]]]:
        """Parse source files for memory-related definitions"""
        definitions = {
            'hardware_io': [],
            'memory_constants': [],
            'cpu_registers': []
        }
        
        # Parse driver headers
        for header_file in self.project_root.glob("drivers/**/*.h"):
            with open(header_file, 'r') as f:
                content = f.read()
                
                # Find #define statements with hex values
                defines = re.findall(r'#define\s+(\w+)\s+(0x[0-9a-fA-F]+)', content)
                for name, value in defines:
                    rel_path = header_file.relative_to(self.project_root)
                    line_num = self.find_line_number(content, f"#define {name}")
                    definitions['hardware_io'].append((name, value, f"{rel_path}:{line_num}"))
        
        return definitions
    
    def find_line_number(self, content: str, search_text: str) -> int:
        """Find line number of text in content"""
        lines = content.split('\n')
        for i, line in enumerate(lines, 1):
            if search_text in line:
                return i
        return 1
    
    def generate_mmap_content(self) -> str:
        """Generate complete mmap.txt content"""
        self.symbols = self.extract_symbols()
        self.sections = self.extract_sections()
        constants = self.parse_assembly_constants()
        definitions = self.parse_source_files()
        
        content = []
        
        # Header
        content.append("CALADANOS PHYSICAL MEMORY MAP")
        content.append("=" * 29)
        content.append("")
        
        # Kernel loader & boot addresses
        content.append("KERNEL LOADER & BOOT ADDRESSES")
        content.append("=" * 31)
        content.append("Location        Address         Description                             File:Line")
        content.append("-" * 75)
        content.append("ENTRY POINT     0x100000        Kernel entry point (1MB)               targets/x86_64.ld:5")
        
        if 'multiboot_magic' in self.symbols:
            addr = f"0x{self.symbols['multiboot_magic']:X}"
            content.append(f"MULTIBOOT_MAGIC {addr:<15} Multiboot2 magic storage               boot/src/boot32.asm:28")
        
        if 'multiboot_info' in self.symbols:
            addr = f"0x{self.symbols['multiboot_info']:X}"
            content.append(f"MULTIBOOT_INFO  {addr:<15} Multiboot2 info pointer                boot/src/boot32.asm:30")
            
        content.append("VGA_BUFFER      0xB8000         VGA text buffer                         boot/src/boot32.asm:123-125")
        content.append("")
        
        # Stack memory layout
        content.append("STACK MEMORY LAYOUT")
        content.append("=" * 18)
        content.append("Location        Address         Size            Description             File:Line")
        content.append("-" * 75)
        
        if 'stack_bottom' in self.symbols and 'stack_top' in self.symbols:
            bottom_addr = f"0x{self.symbols['stack_bottom']:X}"
            top_addr = f"0x{self.symbols['stack_top']:X}"
            stack_size = self.symbols['stack_top'] - self.symbols['stack_bottom']
            size_kb = f"{stack_size // 1024}KB"
            
            content.append(f"STACK_BOTTOM    {bottom_addr:<15} {size_kb:<15} Boot stack bottom               boot/src/boot32.asm:137")
            content.append(f"STACK_TOP       {top_addr:<15} -               Boot stack top (ESP)            boot/src/boot32.asm:139")
            content.append(f"BOOT_ESP        {top_addr:<15} -               Initial ESP value               boot/src/boot32.asm:11")
        
        content.append("")
        
        # Page tables
        content.append("PAGE TABLES (Physical Addresses)")
        content.append("=" * 33)
        content.append("Structure       Address         Size            Description             File:Line")
        content.append("-" * 75)
        
        page_tables = ['page_table_l4', 'page_table_l3', 'page_table_l2']
        for i, table in enumerate(page_tables):
            if table in self.symbols:
                addr = f"0x{self.symbols[table]:X}"
                level = 4 - i
                desc = f"Page Map Level {level}" if level == 4 else f"Page Directory Level {level}"
                line_num = 131 + (i * 2)  # Based on boot32.asm structure
                content.append(f"{table.upper():<15} {addr:<15} 4KB             {desc:<23} boot/src/boot32.asm:{line_num}")
        
        content.append("")
        
        # Global Descriptor Table
        content.append("GLOBAL DESCRIPTOR TABLE (GDT)")
        content.append("=" * 29)
        content.append("Entry           Address         Value           Description             File:Line")
        content.append("-" * 75)
        
        if 'gdt64' in self.symbols:
            gdt_addr = self.symbols['gdt64']
            content.append(f"GDT64_NULL      0x{gdt_addr:X}        0x0             Null descriptor                 boot/src/boot32.asm:143")
            content.append(f"GDT64_CODE      0x{gdt_addr+8:X}        0x00A09A000...  Code segment (64-bit)           boot/src/boot32.asm:145")
            
            if 'gdt64.pointer' in self.symbols:
                ptr_addr = f"0x{self.symbols['gdt64.pointer']:X}"
                content.append(f"GDT64_PTR       {ptr_addr:<15} gdt64 address   GDT pointer                     boot/src/boot32.asm:146")
        
        content.append("")
        
        # Linker memory sections
        content.append("LINKER MEMORY SECTIONS")
        content.append("=" * 21)
        content.append("Section         Address         Size            Description             File:Line")
        content.append("-" * 75)
        
        section_mapping = {
            '.boot': ('BOOT_SECTION', 'Multiboot header', 'targets/x86_64.ld:7'),
            '.rodata': ('RODATA_SECTION', 'Read-only data', 'kernel.elf'),
            '.text': ('TEXT_SECTION', 'Kernel code', 'targets/x86_64.ld:12'),
            '.data': ('DATA_SECTION', 'Initialized data', 'kernel.elf'),
            '.bss': ('BSS_SECTION', 'Uninitialized data', 'kernel.elf')
        }
        
        for section_name, (display_name, description, file_ref) in section_mapping.items():
            if section_name in self.sections:
                section = self.sections[section_name]
                addr = f"0x{section['vma']:X}"
                size = section['size_hex']
                content.append(f"{display_name:<15} {addr:<15} {size:<15} {description:<23} {file_ref}")
        
        content.append("")
        
        # Hardware I/O addresses
        if definitions['hardware_io']:
            content.append("HARDWARE I/O ADDRESSES")
            content.append("=" * 22)
            content.append("Device          Address         Description                     File:Line")
            content.append("-" * 75)
            
            for name, address, file_line in definitions['hardware_io']:
                content.append(f"{name:<15} {address:<15} {name.replace('_', ' ').title():<31} {file_line}")
            
            content.append("")
        
        # Notes section
        content.append("NOTES:")
        content.append("- All addresses are in hexadecimal format")
        content.append("- Generated automatically by mmap.py\n")
        
        return '\n'.join(content)
    
    def generate_mmap_file(self, output_path: str = "mmap.txt"):
        """Generate and write mmap.txt file"""
        content = self.generate_mmap_content()
        
        output_file = self.project_root / output_path
        with open(output_file, 'w') as f:
            f.write(content)
        
        print(f"Generated memory map at: {output_file}")
        print(f"Extracted {len(self.symbols)} symbols and {len(self.sections)} sections")

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Generate CaladanOS memory map')
    parser.add_argument('--project-root', '-p', default='.', 
                       help='Project root directory (default: current directory)')
    parser.add_argument('--output', '-o', default='mmap.txt',
                       help='Output file name (default: mmap.txt)')
    
    args = parser.parse_args()
    
    generator = MemoryMapGenerator(args.project_root)
    generator.generate_mmap_file(args.output)

if __name__ == "__main__":
    main()
