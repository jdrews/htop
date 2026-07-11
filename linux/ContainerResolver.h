#ifndef HEADER_ContainerResolver
#define HEADER_ContainerResolver
/*
htop - linux/ContainerResolver.h
(C) 2026 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stdint.h>


#define CONTAINER_ID_LEN 12

void ContainerResolver_init(void);

const char* ContainerResolver_lookup(const char* containerId);

void ContainerResolver_maybeRefresh(uint64_t currentTimeMs);

void ContainerResolver_done(void);

#endif /* HEADER_ContainerResolver */
