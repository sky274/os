pintos-mkdisk swap.dsk --swap-size=4
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p tests/vm/page-linear -a page-linear -- -q
pintos --gdb -- run page-linear

