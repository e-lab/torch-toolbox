# Reads csv
set datafile separator ','

# Define a decent style for plotting (from Torch)
blue_050 = "#1D4599"
green_050 = "#11AD34"
set style line 1  linecolor rgbcolor blue_050  linewidth 2 pt 7
set style line 2  linecolor rgbcolor green_050 linewidth 2 pt 5
set style increment user

# Switch on the grid
set grid
# Set the window's title
set term wxt title 'GPU RAM usage' noraise
# Plot last 61 rows of ./memory.dat
plot [][0:3250] '< tail -121 ./memory.dat' u 1 w linespoints title 'Used memory [MB]', '' u 2 w linespoints title 'Total memory [MB]'
#plot [][0:3250] './memory.dat' u 1 w linespoints title 'Used memory [MB]', '' u 2 w linespoints title 'Total memory [MB]'
# Pause 1 second
pause 1
# Re-run
reread
