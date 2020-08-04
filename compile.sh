#Mixed-Trust Kernel Module Scheduler
#Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
#NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
#Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
#[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
#Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
#DM20-0619

cp ~/git/hypscheduler-uberxmhf/uxmhf-rpi3/include/hypmtscheduler.h src/
cp ~/git/hypscheduler-uberxmhf/uxmhf-rpi3/rgapps/linux/ugapp-hypmtscheduler-zsrmv/hypmtscheduler_kmodlib.c src/
sed 's/.[<]hypmtscheduler\..[>]/ \"hypmtscheduler\.h\"/g' src/hypmtscheduler_kmodlib.c > src/hypmtscheduler_kmodlib.c-tmp
mv src/hypmtscheduler_kmodlib.c-tmp src/hypmtscheduler_kmodlib.c
# the __hvc() hypercall function is defined in both the hypmtscheduler_kmodlib.c and mavlinkserhb_kmodlib.c
# At this time I am directly modifying the mavlinkserhb_kmodlib.c to comment out its __hvc() definition but
# we will need to decide how to solve this.
#cp ~/git/hypscheduler-uberxmhf/uxmhf-rpi3/include/mavlinkserhb.h src/
#cp ~/git/hypscheduler-uberxmhf/uxmhf-rpi3/rgapps/linux/ugapp-mavlinkserhb/mavlinkserhb_kmodlib.c src/
#sed '/mavlinkserhb\.h/c\\#include \"mavlinkserhb\.h\"' src/mavlinkserhb_kmodlib.c > src/mavlinkserhb_kmodlib.c-tmp
#mv src/mavlinkserhb_kmodlib.c-tmp src/mavlinkserhb_kmodlib.c
make -C /home/dionisio/git/raspberrypi/linux/ ARCH=arm CROSS_COMPILE=/home/dionisio/git/raspberrypi/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf- M=$(pwd) modules

