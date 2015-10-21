## Binary Compatibility for Torch on 32bit machine

Including this library to your Torch script gives the ability to read and write
64-bit binary files on 32-bit platforms.


### Install

Trigger the Makefile.

```bash
make
```

or

```bash
make install
```


### Example

Simply include the library in the beginning of your script.

```lua
-- override torch.DiskFile if 32bit machine
local systembit = tonumber(io.popen("getconf LONG_BIT"):read('*a'))
if systembit == 32 then
   require('libbincompat')
end


-- load from disk
local binary_file_from_64bit_machine = 'test.t7'
local model = torch.load(binary_file_from_64bit_machine)


-- save to disk
torch.save('output_binary_in_64bit_format.t7', model)
```
