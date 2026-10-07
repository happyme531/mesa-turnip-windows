#ifndef TU_GSL_H
#define TU_GSL_H

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "freedreno_pm4.h"

struct tu_gsl_api {
   HMODULE module;
   int (*library_open)(uint32_t);
   int (*library_close)(void);
   uint32_t (*device_open)(uint32_t, uint32_t);
   int (*device_close)(uint32_t);
   int (*device_getinfo)(uint32_t, void *);
   uint32_t (*context_create)(uint32_t, uint32_t, uint32_t, void *);
   int (*context_destroy)(uint32_t, uint32_t);
   int (*memory_alloc)(uint64_t, uint64_t, void *);
   int (*memory_free)(void *);
   int (*submit)(uint32_t, uint32_t, const void *, uint32_t, const void *,
                 uint32_t, uint32_t *, uint32_t, const void *, const void *);
   int (*wait)(uint32_t, uint32_t, uint32_t, uint32_t);
};

struct tu_gsl_memory {
   uint64_t desc[18];
   uint32_t metadata[76];
   uint32_t allocation;
};

struct tu_gsl_command {
   const struct tu_gsl_memory *memory;
   uint64_t dwords;
   uint64_t offset;
   uint64_t flags;
};

static inline bool
tu_gsl_load(struct tu_gsl_api *api)
{
   wchar_t path[4096] = {};
   if (!GetEnvironmentVariableW(L"TU_GSL_LIBRARY", path, 4096)) {
      DWORD bytes = sizeof(path);
      if (RegGetValueW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
            L"VulkanDriverName", RRF_RT_REG_SZ, NULL, path, &bytes) != ERROR_SUCCESS)
         return false;
      wchar_t *name = wcsrchr(path, L'\\');
      if (!name || name - path + 16 >= 4096)
         return false;
      wcscpy(name + 1, L"libgsluser.dll");
   }
   api->module = LoadLibraryExW(path, NULL,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
   if (!api->module)
      return false;
#define GSL_LOAD(member, name) \
   api->member = reinterpret_cast<decltype(api->member)>(GetProcAddress(api->module, name)); \
   if (!api->member) { FreeLibrary(api->module); api->module = NULL; return false; }
   GSL_LOAD(library_open, "gsl_library_open")
   GSL_LOAD(library_close, "gsl_library_close")
   GSL_LOAD(device_open, "gsl_device_open")
   GSL_LOAD(device_close, "gsl_device_close")
   GSL_LOAD(device_getinfo, "gsl_device_getinfo")
   GSL_LOAD(context_create, "gsl_context_create")
   GSL_LOAD(context_destroy, "gsl_context_destroy")
   GSL_LOAD(memory_alloc, "gsl_memory_alloc_pure_64")
   GSL_LOAD(memory_free, "gsl_memory_free_pure")
   GSL_LOAD(submit, "gsl_command_issueib_with_alloc_list")
   GSL_LOAD(wait, "gsl_command_waittimestamp")
#undef GSL_LOAD
   return true;
}

static inline int
tu_gsl_alloc(struct tu_gsl_api *api, uint32_t device,
              struct tu_gsl_memory *mem, uint64_t size)
{
   memset(mem, 0, sizeof(*mem));
   if (size > UINT32_MAX || !size)
      return -5;
   mem->metadata[0] = sizeof(mem->metadata);
   mem->metadata[2] = 1;
   mem->metadata[4] = size;
   mem->metadata[6] = size;
   mem->metadata[14] = 1;
   mem->metadata[40] = 3;
   mem->metadata[52] = 7;
   mem->metadata[58] = 1;
   mem->metadata[61] = 1;
   mem->desc[10] = reinterpret_cast<uintptr_t>(mem->metadata);
   mem->desc[11] = 1;
   mem->desc[12] = reinterpret_cast<uintptr_t>(mem->metadata);
   mem->desc[13] = sizeof(mem->metadata) | (uint64_t(device) << 32);
   mem->desc[14] = 1;
   mem->desc[15] = reinterpret_cast<uintptr_t>(&mem->allocation);
   mem->desc[16] = 4;
   return api->memory_alloc(size, 0x88102000ull, mem->desc);
}

static inline int
tu_gsl_submit_raw(struct tu_gsl_api *api, uint32_t device,
                        uint32_t context, const struct tu_gsl_command *commands,
                        uint32_t count, uint32_t timestamp)
{
   if (count != 1 || !timestamp)
      return -5;
   uint32_t words = 39 + 4 * count;
   uint32_t *metadata = static_cast<uint32_t *>(calloc(words, 4));
   if (!metadata)
      return -4;
   metadata[0] = 0xccaabbee;
   metadata[2] = words * 4;
   metadata[4] = 2;
   metadata[13] = 0xfadcab02;
   metadata[14] = 96 + 16 * count;
   metadata[15] = 0xcccc0001;
   metadata[16] = 88 + 16 * count;
   metadata[25] = count;
   for (uint32_t i = 0; i < count; i++) {
      uint64_t address = commands[i].memory->desc[1] + commands[i].offset;
      metadata[36 + 4 * i] = commands[i].dwords;
      metadata[37 + 4 * i] = address;
      metadata[38 + 4 * i] = address >> 32;
   }
   metadata[37 + 4 * count] = 0xfadcab00;
   metadata[38 + 4 * count] = 8;
   struct {
      void *data;
      uint32_t size;
      uint32_t reserved;
   } private_info = {metadata, words * 4, 0};
   MemoryBarrier();
   int ret = api->submit(device, context, commands, count, commands, 0,
                          &timestamp, 0, NULL, &private_info);
   free(metadata);
   return ret;
}

static inline int
tu_gsl_submit_commands(struct tu_gsl_api *api, uint32_t device,
                        uint32_t context, const struct tu_gsl_command *commands,
                        uint32_t count, uint32_t timestamp)
{
   if (!count || !timestamp || count > (UINT32_MAX - 64) / 16)
      return -5;
   struct tu_gsl_memory root;
   uint64_t size = ((uint64_t(count) * 16 + 32 + 65535) / 65536) * 65536;
   int ret = tu_gsl_alloc(api, device, &root, size);
   if (ret) return ret;
   auto cs = reinterpret_cast<uint32_t *>(root.desc[0]);
   auto marker = reinterpret_cast<volatile uint32_t *>(root.desc[0] + size - 4);
   uint64_t marker_address = root.desc[1] + size - 4;
   *marker = 0;
   for (uint32_t i = 0; i < count; i++) {
      uint64_t address = commands[i].memory->desc[1] + commands[i].offset;
      cs[4 * i] = pm4_pkt7_hdr(CP_INDIRECT_BUFFER, 3);
      cs[4 * i + 1] = address;
      cs[4 * i + 2] = address >> 32;
      cs[4 * i + 3] = commands[i].dwords;
   }
   unsigned pos = 4 * count;
   cs[pos++] = pm4_pkt7_hdr(CP_WAIT_FOR_IDLE, 0);
   cs[pos++] = pm4_pkt7_hdr(CP_MEM_WRITE, 3);
   cs[pos++] = marker_address;
   cs[pos++] = marker_address >> 32;
   cs[pos++] = timestamp;
   cs[pos++] = pm4_pkt7_hdr(CP_WAIT_MEM_WRITES, 0);
   cs[pos++] = pm4_pkt7_hdr(CP_WAIT_FOR_IDLE, 0);
   const struct tu_gsl_command entry = {&root, pos, 0, 0};
   ret = tu_gsl_submit_raw(api, device, context, &entry, 1, timestamp);
   if (!ret) ret = api->wait(device, context, timestamp, 10000);
   MemoryBarrier();
   if (!ret && *marker != timestamp) ret = -13;
   api->memory_free(root.desc);
   return ret;
}

#endif
