#Mixed-Trust Kernel Module Scheduler
#Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
#NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
#Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
#[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
#Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
#DM20-0619

ifeq ($(ARCH),arm)
	CROSS_COMPILE=/home/dionisio/git/raspberrypi/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-
	CC=$(CROSS_COMPILE)gcc
	AR=$(CROSS_COMPILE)ar
	LD=$(CROSS_COMPILE)ld
	AS=$(CROSS_COMPILE)as
else
	CC=gcc
	AR=ar
	LD=ld
	AS=as
endif

all:	libzsv.a #gen-speed-params

clean:
	rm -f libzsv.a libzsv.o gen-speed-params *~

libzsv.o:	libzsv.c 
	$(CC) -fPIC -c libzsv.c -o libzsv.o -I..

libzsv.a:	libzsv.o
	$(AR) -rcs libzsv.a libzsv.o

gen-speed-params:	gen-speed-params.c
	$(CC) -o gen-speed-params gen-speed-params.c -L. -lzsv -lrt
