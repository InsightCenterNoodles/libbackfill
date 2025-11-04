#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path

# Config
input_dir = Path("assets/materials")
output_dir = Path("assets/generated")
converter_exe = Path(sys.argv[-1])
cpp_output_file = Path("src/generated.cpp")
header_output_file = Path("src/generated.h")

# Ensure output directory exists
output_dir.mkdir(parents=True, exist_ok=True)

embedded_files = []

for mat_file in input_dir.glob("*.mat"):
    print(f"Processing {mat_file}")
    matbin_filename = mat_file.with_suffix(".matbin").name
    matbin_path = output_dir / matbin_filename

    # Run conversion
    subprocess.run([
        str(converter_exe),
        "-o", str(matbin_path),
        "--api", "metal",
        "--api", "vulkan",
        "--api", "opengl",
        str(mat_file)
    ], check=True)

    # Collect for C++ embed
    embedded_files.append(matbin_path)

embedded_symbol_name = []
embedded_function_name = []

for embed in embedded_files:
    varname = str(os.path.relpath(embed, output_dir)).replace(".", "_").replace("-", "_").replace("/", "_")
    embedded_symbol_name.append(varname.upper())
    embedded_function_name.append(varname.lower())


# Write embedded_matbins.h
with open(header_output_file, "w") as hpp:
    hpp.write("#pragma once\n")
    hpp.write("#include <span>\n\n")
    hpp.write("namespace generated {\n")

    for name in embedded_function_name:
        hpp.write(f"std::span<const unsigned char> get_{name}();\n")

    hpp.write("}\n")


# Write embedded_matbins.cpp
with open(cpp_output_file, "w") as cpp:
    cpp.write('#include "generated.h"\n')
    cpp.write("#include <cstddef>\n\n")

    cpp.write("namespace generated {\n")

    embed = "#embed"

    for (file, symbol, func) in zip(embedded_files, embedded_symbol_name, embedded_function_name):

        cpp.write(f'static constexpr unsigned char {symbol}[] = {{ \n{embed} "../{file}"\n}};\n')
        cpp.write(f"std::span<const unsigned char> get_{func}() {{ return {symbol}; }}\n")

    cpp.write("}\n")
