#!/usr/bin/env python3
"""
append_desktop_name_suffix.py  INPUT OUTPUT SUFFIX

Reads a .desktop file from INPUT, appends SUFFIX to every Name= and Name[*]=
line that belongs to the [Desktop Entry] section (not [Desktop Action *]
sections), and writes the result to OUTPUT.

When SUFFIX is empty the file is copied unchanged.
"""
import sys

if len(sys.argv) != 4:
    print(f"Usage: {sys.argv[0]} INPUT OUTPUT SUFFIX", file=sys.stderr)
    sys.exit(1)

input_path, output_path, suffix = sys.argv[1], sys.argv[2], sys.argv[3]

with open(input_path, encoding='utf-8') as f:
    lines = f.readlines()

in_desktop_entry = False
out_lines = []

for line in lines:
    stripped = line.rstrip('\n')

    # Track which section we are in
    if stripped.startswith('['):
        in_desktop_entry = (stripped == '[Desktop Entry]')
        out_lines.append(line)
        continue

    # Append suffix to Name= and Name[xx]= only inside [Desktop Entry]
    if in_desktop_entry and suffix and (
            stripped.startswith('Name=') or stripped.startswith('Name[')):
        key, _, value = stripped.partition('=')
        out_lines.append(f'{key}={value}{suffix}\n')
    else:
        out_lines.append(line)

with open(output_path, 'w', encoding='utf-8') as f:
    f.writelines(out_lines)
