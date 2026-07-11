/*
htop - ContainerResolver.c
(C) 2026 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "linux/ContainerResolver.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Macros.h"
#include "XUtils.h"


#define CONTAINER_NAME_MAX 256
#define RESOLVER_REFRESH_MS (5 * 1000)
#define RESOLVER_INITIAL_CAPACITY 32

typedef enum { RUNTIME_NONE, RUNTIME_DOCKER, RUNTIME_PODMAN } RuntimeType;

typedef struct {
   char id[CONTAINER_ID_LEN + 1];
   char name[CONTAINER_NAME_MAX];
} ContainerEntry;

static struct {
   ContainerEntry* entries;
   int size;
   int capacity;
   bool available;
   RuntimeType runtime;
   uint64_t lastRefreshMs;
   bool initialized;
   bool warnedOnce;
} resolver = { 0 };


static bool socketExists(const char* path) {
   struct stat st;
   return (stat(path, &st) == 0 && S_ISSOCK(st.st_mode));
}


static RuntimeType detectRuntime(void) {
   if (socketExists("/var/run/docker.sock"))
      return RUNTIME_DOCKER;

   const char* xdgRuntime = getenv("XDG_RUNTIME_DIR");
   if (xdgRuntime) {
      char podmanSock[4096];
      int len = xSnprintf(podmanSock, sizeof(podmanSock), "%s/podman/podman.sock", xdgRuntime);
      if (len > 0 && (size_t)len < sizeof(podmanSock)) {
         if (socketExists(podmanSock))
            return RUNTIME_PODMAN;
      }
   }

   // Fallback: check common Podman socket locations
   if (socketExists("/run/user/1000/podman/podman.sock"))
      return RUNTIME_PODMAN;

   uid_t uid = getuid();
   char fallbackSock[4096];
   xSnprintf(fallbackSock, sizeof(fallbackSock), "/run/user/%d/podman/podman.sock", uid);
   if (socketExists(fallbackSock))
      return RUNTIME_PODMAN;

   return RUNTIME_NONE;
}


static void resolver_clear(void) {
   free(resolver.entries);
   resolver.entries = NULL;
   resolver.size = 0;
   resolver.capacity = 0;
}


static void resolver_refresh(void) {
   RuntimeType runtime = detectRuntime();

   if (runtime == RUNTIME_NONE) {
      resolver.available = false;
      return;
   }

   const char* cmd = NULL;
   switch (runtime) {
   case RUNTIME_DOCKER:
      cmd = "docker ps --format '{{.ID}}\t{{.Names}}' 2>/dev/null";
      break;
   case RUNTIME_PODMAN:
      cmd = "podman ps --format '{{.ID}}\t{{.Names}}' 2>/dev/null";
      break;
   case RUNTIME_NONE:
      return;
   }

   FILE* fp = popen(cmd, "r");
   if (!fp) {
      resolver.available = false;
      return;
   }

   ContainerEntry* newEntries = xCalloc(RESOLVER_INITIAL_CAPACITY, sizeof(ContainerEntry));
   int newSize = 0;
   int newCapacity = RESOLVER_INITIAL_CAPACITY;

   char line[CONTAINER_NAME_MAX + CONTAINER_ID_LEN + 4];
   while (fgets(line, sizeof(line), fp)) {
      // Strip trailing newline
      char* nl = strchr(line, '\n');
      if (nl)
         *nl = '\0';

      // Skip empty lines
      if (line[0] == '\0')
         continue;

      // Split on tab: id\tname
      char* tab = strchr(line, '\t');
      if (!tab)
         continue;

      *tab = '\0';
      const char* id = line;
      const char* name = tab + 1;

      // Skip empty fields
      if (id[0] == '\0' || name[0] == '\0')
         continue;

      // Truncate ID to 12 chars
      size_t idLen = strlen(id);
      if (idLen > CONTAINER_ID_LEN)
         idLen = CONTAINER_ID_LEN;

      if ((size_t)newSize >= (size_t)newCapacity) {
         newCapacity *= 2;
         newEntries = xReallocArray(newEntries, newCapacity, sizeof(ContainerEntry));
      }

      String_safeStrncpy(newEntries[newSize].id, id, CONTAINER_ID_LEN + 1);
      newEntries[newSize].id[idLen] = '\0';
      String_safeStrncpy(newEntries[newSize].name, name, CONTAINER_NAME_MAX);
      newSize++;
   }

   pclose(fp);

   resolver_clear();
   resolver.entries = newEntries;
   resolver.size = newSize;
   resolver.capacity = newCapacity;
   resolver.runtime = runtime;
   resolver.available = true;
}


void ContainerResolver_init(void) {
   if (resolver.initialized)
      return;

   resolver.initialized = true;
   resolver.warnedOnce = false;
   resolver_refresh();
   resolver.lastRefreshMs = 0;
}


const char* ContainerResolver_lookup(const char* containerId) {
   if (!resolver.available || !containerId)
      return NULL;

   for (int i = 0; i < resolver.size; i++) {
      if (strncmp(resolver.entries[i].id, containerId, CONTAINER_ID_LEN + 1) == 0)
         return resolver.entries[i].name;
   }

   return NULL;
}


void ContainerResolver_maybeRefresh(uint64_t currentTimeMs) {
   if (!resolver.initialized)
      return;

   if (resolver.lastRefreshMs == 0 || (currentTimeMs - resolver.lastRefreshMs) >= RESOLVER_REFRESH_MS) {
      resolver_refresh();
      resolver.lastRefreshMs = currentTimeMs;
   }
}


void ContainerResolver_done(void) {
   resolver_clear();
   resolver.initialized = false;
   resolver.available = false;
   resolver.runtime = RUNTIME_NONE;
   resolver.lastRefreshMs = 0;
   resolver.warnedOnce = false;
}
