#!/usr/bin/env python3

import sys
import re

def count(char, string):
    """Counts the occurrences of a character in a string."""
    return string.count(char)

def process_file(filename):
    with open(filename) as filehandle:
        contents = filehandle.read()

    out = ""
    state = None
    depth = 0
    brace_depth = 0
    lines = contents.split("\n")

    lines_iter = iter(lines)
    while True:
        line = next(lines_iter, None)
        if line is None:
            break

        line_stripped = line.strip()

        # Look for #ifdef BMCWEB_<something>
        ifdef_match = re.match(r"^#ifdef (BMCWEB_\w+)", line_stripped)
        ifndef_match = re.match(r"^#ifndef (BMCWEB_\w+)", line_stripped)

        if ifdef_match:
            macro = ifdef_match.group(1)
            const_expr = f"if constexpr ({macro}) {{"
            out += const_expr + "\n"
            brace_depth += 1
            state = "inifdef"
            continue

        elif ifndef_match:
            macro = ifndef_match.group(1)
            const_expr = f"if constexpr (!{macro}) {{"
            out += const_expr + "\n"
            brace_depth += 1
            state = "inifndef"
            continue

        brace_depth += count("{", line) - count("}", line)

        if state in ("inifdef", "inifndef"):
            if line_stripped.startswith("#endif"):
                if brace_depth > 0:
                    brace_depth -= 1
                    out += "}\n"  # Close the if constexpr block
                state = None
                continue
            elif line_stripped.startswith("#else"):
                out += "} else {\n"
                continue

        out += line + "\n"

    if out.endswith("\n"):
        out = out[:-1]

    with open(filename, "w") as outputhandle:
        outputhandle.write(out)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ./replace_macros.py <file1.hpp> [<file2.hpp> ...]")
        sys.exit(1)

    for filename in sys.argv[1:]:
        process_file(filename)
