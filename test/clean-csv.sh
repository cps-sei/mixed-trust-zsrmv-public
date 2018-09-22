#!/bin/sh
sort -t" " -nk2 $1 | gawk '{ rid=$1 ; ts=$2 ; et=$3 ; if (et == "58") {et="4" } else {if (et == "59") {et = "3"}} if (et != "50" && et != "51" && et != "52" && et != "53" && et != "54" && et != "55" ) { print rid " " ts " " et} }' -
