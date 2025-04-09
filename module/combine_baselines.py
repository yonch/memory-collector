#!/usr/bin/env python3

import argparse
import csv
import os
import sys
from pathlib import Path
from typing import List, Dict, Any

def extract_machine_type(filename: str) -> str:
    """Extract machine type from filename.
    
    Args:
        filename: Input filename (e.g., 'c6i.metal-results.csv')
        
    Returns:
        Machine type (e.g., 'c6i.metal')
    """
    # Remove any directory components and the .csv extension
    basename = Path(filename).stem
    
    # Remove any trailing -results or _results
    for suffix in ['-results', '_results']:
        if basename.endswith(suffix):
            basename = basename[:-len(suffix)]
    
    return basename

def read_baseline_data(csv_file: str) -> List[Dict[str, Any]]:
    """Read baseline experiment data from a CSV file.
    
    Args:
        csv_file: Path to CSV file
        
    Returns:
        List of dictionaries containing baseline data rows
    """
    baseline_data = []
    
    try:
        with open(csv_file, 'r') as f:
            reader = csv.DictReader(f)
            
            # Validate required columns
            required_cols = {'timestamp', 'tick', 'min_delay', 'max_delay', 
                           'mean_delay', 'stddev', 'samples', 'missing', 'experiment'}
            header_set = set(reader.fieldnames or [])
            missing_cols = required_cols - header_set
            
            if missing_cols:
                raise ValueError(f"Missing required columns in {csv_file}: {missing_cols}")
            
            # Extract baseline rows
            for row in reader:
                if row['experiment'].strip() == 'baseline':
                    baseline_data.append(row)
    
    except FileNotFoundError:
        print(f"Error: File not found: {csv_file}", file=sys.stderr)
        sys.exit(1)
    except csv.Error as e:
        print(f"Error reading CSV file {csv_file}: {e}", file=sys.stderr)
        sys.exit(1)
    
    return baseline_data

def main():
    parser = argparse.ArgumentParser(
        description='Combine baseline experiments from multiple benchmark CSVs'
    )
    parser.add_argument(
        'input_files',
        nargs='+',
        help='Input CSV files containing benchmark results'
    )
    parser.add_argument(
        '-o', '--output',
        default='combined_baselines.csv',
        help='Output CSV file (default: combined_baselines.csv)'
    )
    
    args = parser.parse_args()
    
    # Collect all baseline data
    combined_data = []
    
    for input_file in args.input_files:
        try:
            machine_type = extract_machine_type(input_file)
            baseline_data = read_baseline_data(input_file)
            
            # Replace experiment name with machine type
            for row in baseline_data:
                row['experiment'] = machine_type
            
            combined_data.extend(baseline_data)
            
            print(f"Processed {len(baseline_data)} baseline rows from {input_file}")
            
        except Exception as e:
            print(f"Error processing {input_file}: {e}", file=sys.stderr)
            continue
    
    if not combined_data:
        print("Error: No baseline data found in any input files", file=sys.stderr)
        sys.exit(1)
    
    # Write combined data
    try:
        with open(args.output, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=combined_data[0].keys())
            writer.writeheader()
            writer.writerows(combined_data)
        
        print(f"\nSuccessfully wrote {len(combined_data)} rows to {args.output}")
        
    except IOError as e:
        print(f"Error writing output file: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main() 