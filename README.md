## Build and Flash iocontrol

```
$ west build -b gartennode application/gartennode_zysterne --build-dir build_zysterne -- -DEXTRA_CONF_FILE=prj.conf
```

```
$ imgtool sign --header-size 0x200 --align 8 --version 1.2 --slot-size 0x30000 --key bootloader/mcuboot/gartennode.pem --pad bui
ld/zephyr/zephyr.bin signed_zephyr.bin
```
