# How to cat stuff and re-plot it

## Concatenating files

Getting first chunk of data (minus *n* corrupted lines)

```bash
head -n-1 SW11A-1/accuracy.log > SW11A/accuracy.log
```

Appending other chunks of data (without 1-line header)

```bash
tail -n+2 SW11A-2/accuracy.log >> SW11A/accuracy.log
```

Show the results

```bash
cat SW11A/accuracy.log
```

##Plotting and creating eps-files
**Warning**: previously saved charts will be overwritten.

Note that you ought to replace the `<folderName>` with the correct folder name. In the example below, it is supposed to operate from the folder containing all the results' folders.

```bash
gnuplot -e "fileName='./<folderName>/accuracy.log'; figure='accuracy'" plotCharts.plt
gnuplot -e "fileName='./<folderName>/cross-entropy.log'; figure='cross-entropy'" plotCharts.plt
```
