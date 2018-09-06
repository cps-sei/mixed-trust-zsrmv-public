#!/bin/sh
gawk  -F':' '/HYPTASKBEFOREEXEC/ { getline ; tss = $2 ; ts = strtonum(tss) ; print "0 " ts " 4" } /HYPTASKAFTEREXEC/ { getline; tss = $2 ;  ts = strtonum(tss) ; print "0 " ts " 3"} ' $1
