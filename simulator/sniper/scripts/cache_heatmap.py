"""
cache_heatmap.py

Periodically capture cache snapshots and generate heatmap visualizations.
The heatmaps show block types (colors) and recency (brightness).

Usage: Add to your run command:
  -s cache_heatmap:cache=L2,interval=100000,core=0

Arguments:
  cache    - Cache to monitor: L1D, L1I, L2, L3/LLC (default: L2)
  interval - Snapshot interval in instructions (default: 100000)
  core     - Core ID to monitor (default: 0)
  format   - Output format: ppm, png, or data (default: ppm)
"""

import sys
import os
import sim

try:
    import sim_cache
except ImportError:
    print("[CACHE_HEATMAP] Warning: sim_cache module not available")
    sim_cache = None

class CacheHeatmap:
    def setup(self, args):
        # Parse arguments - format: cache=L2,interval=100000,core=0
        args_dict = {}
        if args:
            # Split by comma, not colon
            for arg in args.split(','):
                if '=' in arg:
                    k, v = arg.split('=', 1)
                    args_dict[k.strip()] = v.strip()
        
        self.cache_name = args_dict.get('cache', 'L2')
        self.interval = int(args_dict.get('interval', 100000))
        self.core_id = int(args_dict.get('core', 0))
        self.format = args_dict.get('format', 'ppm')
        self.done = False  # Guard flag - prevents access after ROI ends
        
        self.snapshot_count = 0
        self.output_dir = os.path.join(sim.config.output_dir, 'cache_heatmaps')
        
        # Create output directory
        try:
            os.makedirs(self.output_dir, exist_ok=True)
        except:
            print("[CACHE_HEATMAP] Warning: Could not create output directory")
            self.output_dir = sim.config.output_dir
        
        print(f"[CACHE_HEATMAP] Monitoring {self.cache_name} on core {self.core_id}")
        print(f"[CACHE_HEATMAP] Snapshot interval: {self.interval} instructions")
        print(f"[CACHE_HEATMAP] Output: {self.output_dir}")
        
        # Register periodic callback
        sim.util.EveryIns(self.interval, self.take_snapshot, roi_only=True)
        
        # Print legend
        self.print_legend()
    
    def print_legend(self):
        """Print color legend for the heatmap"""
        print("\n[CACHE_HEATMAP] Color Legend:")
        print("  Red          - PAGE_TABLE (page walk data)")
        print("  Light Red    - PAGE_TABLE_PASSTHROUGH")
        print("  Orange       - RANGE_TABLE")
        print("  Yellow       - UTOPIA")
        print("  Purple       - SECURITY")
        print("  Magenta      - EXPRESSIVE")
        print("  Cyan         - TLB_ENTRY")
        print("  Light Cyan   - TLB_ENTRY_PASSTHROUGH")
        print("  Blue         - NON_PAGE_TABLE (normal data)")
        print("  Dark Gray    - Invalid/Empty")
        print("  Brightness   - Recency (bright=MRU, dim=LRU)")
        print("")
    
    def take_snapshot(self, icount, icount_delta):
        """Called periodically to take cache snapshots"""
        # Guard: don't access cache if ROI has ended
        if sim_cache is None or self.done:
            return
        
        # Additional safety: wrap in try/except to catch teardown issues
        try:
            self.snapshot_count += 1
            
            # Generate filename
            filename = f"snapshot_{self.snapshot_count:06d}_{icount}"
            
            if self.format == 'ppm':
                # Use C++ function to save PPM directly
                filepath = os.path.join(self.output_dir, filename + '.ppm')
                sim_cache.save_heatmap(self.core_id, self.cache_name, filepath)
            
            elif self.format == 'data':
                # Save raw data as text file
                filepath = os.path.join(self.output_dir, filename + '.txt')
                snapshot = sim_cache.get_snapshot(self.core_id, self.cache_name)
                with open(filepath, 'w') as f:
                    f.write(f"# Cache Snapshot at instruction {icount}\n")
                    f.write(f"# Sets: {snapshot['num_sets']}, Ways: {snapshot['num_ways']}\n")
                    f.write("# Format: set,way,valid,type,recency,reuse\n")
                    for set_idx, ways in enumerate(snapshot['blocks']):
                        for way_idx, block in enumerate(ways):
                            f.write(f"{set_idx},{way_idx},{block['valid']},{block['type']},{block['recency']},{block['reuse']}\n")
            
            # Print summary every 10 snapshots
            if self.snapshot_count % 10 == 0:
                self.print_summary(icount)
        except Exception as e:
            # Catch any errors during teardown and stop gracefully
            print(f"[CACHE_HEATMAP] Error in take_snapshot (simulation may be ending): {e}")
            self.done = True
    
    def print_summary(self, icount):
        """Print a summary of current cache state"""
        if sim_cache is None or self.done:
            return
        
        try:
            counts = sim_cache.get_type_counts(self.core_id, self.cache_name)
            total = counts['valid'] + counts['invalid']
            
            print(f"[CACHE_HEATMAP] Snapshot #{self.snapshot_count} at icount={icount}")
            print(f"  Valid: {counts['valid']}/{total} ({100*counts['valid']/total:.1f}%)")
            
            if counts['valid'] > 0:
                types_found = []
                if counts['NON_PAGE_TABLE'] > 0:
                    types_found.append(f"Data:{counts['NON_PAGE_TABLE']}")
                if counts['PAGE_TABLE'] > 0:
                    types_found.append(f"PT:{counts['PAGE_TABLE']}")
                if counts['SECURITY'] > 0:
                    types_found.append(f"Sec:{counts['SECURITY']}")
                if counts['UTOPIA'] > 0:
                    types_found.append(f"Utopia:{counts['UTOPIA']}")
                if counts['TLB_ENTRY'] > 0:
                    types_found.append(f"TLB:{counts['TLB_ENTRY']}")
                if counts['RANGE_TABLE'] > 0:
                    types_found.append(f"Range:{counts['RANGE_TABLE']}")
                if counts['EXPRESSIVE'] > 0:
                    types_found.append(f"Expr:{counts['EXPRESSIVE']}")
                
                print(f"  Types: {', '.join(types_found)}")
        except Exception as e:
            # Silently ignore errors during teardown
            self.done = True
    
    def hook_roi_end(self):
        """Called when ROI ends - stop taking snapshots and print summary"""
        if self.done:
            return
        self.done = True
        
        print(f"\n[CACHE_HEATMAP] ROI ended. Total snapshots: {self.snapshot_count}")
        print(f"[CACHE_HEATMAP] Output directory: {self.output_dir}")
        
        # Create an animated GIF if imagemagick is available
        if self.format == 'ppm' and self.snapshot_count > 1:
            print("[CACHE_HEATMAP] Tip: Create animation with:")
            print(f"  convert -delay 10 {self.output_dir}/snapshot_*.ppm {self.output_dir}/animation.gif")

sim.util.register(CacheHeatmap())
