import sys
import re

def parse_mapping_file(filename):
    """Parses a vp_map.txt file and returns a dictionary {va: pa}."""
    mappings = {}
    va_pa_pattern = re.compile(r"^VA:\s+(0x[0-9a-fA-F]+)\s+->\s+PA:\s+(0x[0-9a-fA-F]+)")
    counter = 0
    try:
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                match = va_pa_pattern.match(line)
                if match:
                    va_str, pa_str = match.groups()
                    try:
                        va = counter
                        pa = int(pa_str, 16)
                        # Optional: Exclude mappings where PA might be 0 if PFN was 0 (permissions issue)
                        # page_size = 4096 # Assuming page size if needed for filtering
                        # if pa % page_size == va % page_size: # Basic check if offset matches
                        mappings[va] = pa
                    except ValueError:
                        print(f"Warning: Could not parse hex values in line: {line}", file=sys.stderr)
                    counter += 1
                        
    except FileNotFoundError:
        print(f"Error: File not found: {filename}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"Error reading file {filename}: {e}", file=sys.stderr)
        return None
        
    return mappings

def compare_mappings(map1, map2):
    """Counts overlaps where the same VA maps to the same PA in both maps."""
    overlap_count = 0
    if map1 is None or map2 is None:
        return 0, 0 # Cannot compare if a map failed to load
        
    total_in_map1 = len(map1)
    if total_in_map1 == 0:
        return 0, 0

    for va, pa1 in map1.items():
        if va in map2 and map2[va] == pa1:
            overlap_count += 1
            
    return overlap_count, total_in_map1

if __name__ == "__main__":
    if len(sys.argv) != 5:
        print("Usage: python compare_mappings.py <file1.txt> <file2.txt> <file3.txt> <file4.txt>")
        sys.exit(1)

    filenames = sys.argv[1:5]
    maps = []
    
    print("Parsing mapping files...")
    for filename in filenames:
        print(f"  Parsing {filename}...")
        parsed_map = parse_mapping_file(filename)
        if parsed_map is None:
            print(f"  Failed to parse {filename}. Exiting.")
            sys.exit(1)
        maps.append(parsed_map)
        print(f"  Found {len(parsed_map)} valid mappings in {filename}.")
        
    print("\nCalculating Overlap Ratios...")
    print("Ratio = (VAs mapping to same PA in both files) / (Total VAs in File A)")
    print("---------------------------------------------------------------------")

    num_files = len(maps)
    for i in range(num_files):
        for j in range(i + 1, num_files):
            file_a_name = filenames[i]
            file_b_name = filenames[j]
            map_a = maps[i]
            map_b = maps[j]
            
            # Compare A vs B
            overlap_ab, total_a = compare_mappings(map_a, map_b)
            ratio_ab = (float(overlap_ab) / total_a) if total_a > 0 else 0.0
            print(f"Overlap ({file_a_name} vs {file_b_name}): {overlap_ab}/{total_a} = {ratio_ab:.4f}")

            # Compare B vs A (denominator changes)
            overlap_ba, total_b = compare_mappings(map_b, map_a)
            ratio_ba = (float(overlap_ba) / total_b) if total_b > 0 else 0.0
            print(f"Overlap ({file_b_name} vs {file_a_name}): {overlap_ba}/{total_b} = {ratio_ba:.4f}")
            print("---")

    print("\nAnalysis complete.")