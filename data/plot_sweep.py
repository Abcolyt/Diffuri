#
import argparse
import csv
import math
import os
import sys

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("Error: matplotlib is required. Install it via 'pip install matplotlib'", file=sys.stderr)
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Plot Diffuri sweep results.")
    parser.add_argument("csv_path", help="Path to the sweep CSV file")
    parser.add_argument("--out", help="Output PNG path (default: same as CSV with .png extension)")
    parser.add_argument("--show", action="store_true", help="Show plot interactively")
    args = parser.parse_args()

    if not os.path.exists(args.csv_path):
        print(f"Error: File not found: {args.csv_path}", file=sys.stderr)
        sys.exit(1)

    variable = "unknown"
    metric = "unknown"
    ref_ok = "unknown"
    
    x_values = []
    y_values = []
    statuses = []

    with open(args.csv_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            if row[0].startswith('#'):
                if row[0].startswith("# variable:"):
                    variable = row[0].split(":", 1)[1].strip()
                elif row[0].startswith("# metric:"):
                    metric = row[0].split(":", 1)[1].strip()
                elif row[0].startswith("# reference_ok:"):
                    ref_ok = row[0].split(":", 1)[1].strip()
                continue
            
            # Data row
            if row[0] == "value":
                continue # Skip header if not commented
            
            val_str = row[0]
            err_str = row[1]
            status = row[5] if len(row) > 5 else ""
            
            try:
                val = float(val_str)
                if err_str.lower() == "nan":
                    continue # Skip failed runs
                err = float(err_str)
                if err <= 0:
                    err = 1e-16 # Avoid log(0)
                
                x_values.append(val)
                y_values.append(err)
                statuses.append(status)
            except ValueError:
                continue

    if not x_values:
        print("Error: No valid data points found in CSV.", file=sys.stderr)
        sys.exit(1)

    plt.figure(figsize=(10, 6))
    
    # Log scale for X if variable is rtol or atol
    x_log = variable.lower() in ["rtol", "atol"]
    if x_log:
        plt.xscale('log')
    
    plt.yscale('log')
    
    plt.plot(x_values, y_values, marker='o', linestyle='-', color='b', label=f"Error ({metric})")
    
    # Annotate failed points if any (optional, kept simple here)
    
    title = f"Sweep Convergence: {variable} vs {metric}"
    if ref_ok == "false":
        title += " (WARNING: Reference solution failed)"
    
    plt.title(title)
    plt.xlabel(variable)
    plt.ylabel("Error")
    plt.grid(True, which="both", ls="--", alpha=0.6)
    plt.legend()

    out_path = args.out
    if not out_path:
        base, _ = os.path.splitext(args.csv_path)
        out_path = base + ".png"

    # Реперная линия: error = rtol (только если X — rtol/atol)
    if variable.lower() in ("rtol", "atol"):
        xmin, xmax = plt.xlim()
        ref_line = [xmin, xmax]
        plt.plot(ref_line, ref_line, "k--", linewidth=1.0, alpha=0.5,
                 label="error = rtol")
        plt.legend()

    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Plot saved to: {out_path}")

    if args.show:
        plt.show()

if __name__ == "__main__":
    main()