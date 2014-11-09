# Replot charts
####
# gnuplot -e "fileName='./HW2C/accuracy.log'; figure='accuracy'" plotCharts.plt
# gnuplot -e "fileName='./HW2C/cross-entropy.log'; figure='cross-entropy'" plotCharts.plt
####

# Define a decent style for plotting (from Torch)
blue_050 = "#1D4599"
green_050 = "#11AD34"
set style line 1  linecolor rgbcolor blue_050  linewidth 2 pt 7
set style line 2  linecolor rgbcolor green_050 linewidth 2 pt 5 linetype 3
set style increment user

# Switch on the grid
set grid
# Plotting on screen
#plot '< tail -n+2 ./HW2C/accuracy.log' u 1 w lines title 'Training accuracy', '' u 2 w lines title 'Testing accuracy'
plot '< tail -n+2 '.fileName u 1 w lines title 'Training '.figure, '' u 2 w lines title 'Testing '.figure
# Printing to eps
set term postscript eps enhanced color
set o fileName.'.eps'
replot
set term wxt

# Pause 1 second
pause -1
