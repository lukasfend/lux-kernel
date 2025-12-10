nasm -f bin ./boot.asm -o ./build/boot.bin
qemu-system-x86_64 -hda ./build/boot.bin