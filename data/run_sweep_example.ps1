.\Diffuri.exe --input data\example_input\lorenz.txt `
              --sweep "rtol=1e-4:1e-14:11" `
              --sweep-out sweep_rtol.csv `
              5.0 20 1e-3
python plot_sweep.py sweep_rtol.csv --out sweep_rtol.png