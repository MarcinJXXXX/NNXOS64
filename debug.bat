qemu-system-x86_64 -no-reboot -no-shutdown -d cpu_reset -m 256 -monitor stdio -drive if=pflash,format=raw,readonly,file="OVMF_X64.fd" -drive file=fat:rw:image

