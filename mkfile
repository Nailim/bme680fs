</$objtype/mkfile

ALL=bme680fs

all:V:	$ALL

bme680fs:		bme680fs.$O
	$LD $LDFLAGS -o bme680fs bme680fs.$O

bme680fs.$O:	bme680fs.c
	$CC $CFLAGS bme680fs.c

clean:V:
	rm -f *.$O bme680fs

