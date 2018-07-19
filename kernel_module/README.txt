README
######

To Build:
   run `make`

To Install:
   run `sudo insmod lkm_example.ko`

To See if it is running:
   run `sudo dmesg` or `lsmod | grep "lkm_example"`

To unload:
   run `sudo rmmod lkm_example`
