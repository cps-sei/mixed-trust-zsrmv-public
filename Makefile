obj-m := zsrmv.o
EXTRA_CFLAGS=-g -DDEBUG
zsrmv-objs := src/zsrmv.o src/hypmtscheduler_kmodlib.o src/timestamp.o
