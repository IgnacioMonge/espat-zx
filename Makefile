all: ESPATZX.tap

ESPATZX.tap: espatzx_code.c ay_uart.asm font64_data.h
	zcc +zx -vn -startup=0 -clib=new espatzx_code.c ay_uart.asm -o ESPATZX -create-app
clean:
	rm -f *.tap ESPAT* *.bin *.o
