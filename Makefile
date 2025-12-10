all:
	nasm -f bin ./boot.asm -o ./build/boot.bin
	dd if=./message.txt >> ./build/boot.bin
	dd if=/dev/zero bs=512 count=1 >> ./build/boot.bin