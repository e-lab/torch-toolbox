# Fresh log file every time; no not tell me if it doesn't exist
rm -f memory.dat

# Output GPU used and total memory, csv and without units [MB]
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits -l 1 | tee -a memory.dat
