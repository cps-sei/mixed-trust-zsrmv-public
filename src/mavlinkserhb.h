/*
Mixed-Trust Kernel Module Scheduler
Copyright 2020 Carnegie Mellon University and Hyoseung Kim.
NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
Released under a BSD (SEI)-style license, please see license.txt or contact permission@sei.cmu.edu for full terms.
[DISTRIBUTION STATEMENT A] This material has been approved for public release and unlimited distribution.  Please see Copyright notice for non-US Government use and distribution.
Carnegie Mellon® is registered in the U.S. Patent and Trademark Office by Carnegie Mellon University.
DM20-0619
*/


/*
	MAVLINK serial heart-beat uberapp

	author: amit vasudevan (amitvasudevan@acm.org)
*/

#ifndef __MAVLINKSERHB_H__
#define __MAVLINKSERHB_H__

#define UAPP_MAVLINKSERHB_UHCALL	0xD0

#define UAPP_MAVLINKSERHB_UHCALL_INITIALIZE				1
#define UAPP_MAVLINKSERHB_UHCALL_SEND					2
#define UAPP_MAVLINKSERHB_UHCALL_CHECKRECV				3
#define UAPP_MAVLINKSERHB_UHCALL_RECV					4
#define UAPP_MAVLINKSERHB_UHCALL_ACTIVATEHBHYPTASK		5
#define UAPP_MAVLINKSERHB_UHCALL_DEACTIVATEHBHYPTASK	6


#ifndef __ASSEMBLY__

typedef struct {
	uint8_t uhcall_fn;
	uint32_t iparam_1;
	uint32_t iparam_2;
	uint32_t iparam_3;
	uint32_t iparam_4;
	uint32_t oparam_1;
	uint32_t oparam_2;
	uint32_t status;
}uapp_mavlinkserhb_param_t;



#endif // __ASSEMBLY__



#endif //__MAVLINKSERHB_H__
