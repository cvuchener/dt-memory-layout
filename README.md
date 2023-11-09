Memory layout generator for Dwarf-Therapist
===========================================

Generate ini memory layouts for [Dwarf-Therapist](https://github.com/Dwarf-Therapist/Dwarf-Therapist) from [df-structures](https://github.com/DFHack/df-structures) XML.

Building requires a C++20 compiler, CMake, and [dfs](https://github.com/cvuchener/dfs).

Usage:

    dt-memory-layout /path/to/df-structures "version name" /path/to/memory_layout.xml

The memory layout ini is printed on the standard output.

XML files describing the memory layout to generate are provided in the `ini` directory.

This program is distributed under GPLv3.

