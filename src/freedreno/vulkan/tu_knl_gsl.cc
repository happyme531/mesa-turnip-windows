/* SPDX-License-Identifier: MIT */

#include "util/os_time.h"
#include "vk_sync_timeline.h"

#include "tu_device.h"
#include "tu_gsl.h"
#include "tu_knl.h"
#include "tu_queue.h"

struct gsl_state {
   struct tu_gsl_api api;
};

struct gsl_bo {
   struct tu_bo bo;
   struct tu_gsl_memory memory;
};

struct gsl_sync {
   struct vk_sync vk;
   HANDLE event;
   HANDLE pending;
};

static bool
gsl_pending_sync_enabled()
{
   static const bool enabled = debug_get_bool_option("TU_GSL_PENDING_SYNC", false);
   return enabled;
}

struct gsl_signal_snapshot {
   const struct vk_sync *sync;
   HANDLE event;
   HANDLE pending;
};

struct gsl_signal_snapshots {
   struct vk_device *device = NULL;
   gsl_signal_snapshot *signals = NULL;
   uint32_t count = 0;
   void clear()
   {
      for (uint32_t i = 0; i < count; i++) {
         auto &signal = signals[i];
         if (signal.event)
            CloseHandle(signal.event);
         if (signal.pending)
            CloseHandle(signal.pending);
      }
      free(signals);
      signals = NULL;
      count = 0;
   }
   ~gsl_signal_snapshots()
   {
      clear();
   }
};

static thread_local gsl_signal_snapshots gsl_cpu_signals;
static const struct vk_sync_type *
gsl_sync_type_ptr();

VkResult
tu_gsl_prepare_cpu_signals(struct vk_device *device, const struct vk_sync_signal *signals, uint32_t count)
{
   if (!gsl_pending_sync_enabled())
      return VK_SUCCESS;
   gsl_cpu_signals.clear();
   gsl_cpu_signals.device = device;
   gsl_cpu_signals.signals = static_cast<gsl_signal_snapshot *>(calloc(count, sizeof(gsl_signal_snapshot)));
   if (count && !gsl_cpu_signals.signals)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   for (uint32_t i = 0; i < count; i++) {
      if (signals[i].sync->type != gsl_sync_type_ptr()) {
         gsl_cpu_signals.clear();
         return vk_error(device, VK_ERROR_UNKNOWN);
      }
      auto sync = reinterpret_cast<const gsl_sync *>(signals[i].sync);
      HANDLE event = NULL, pending = NULL;
      HANDLE process = GetCurrentProcess();
      if (!DuplicateHandle(process, sync->event, process, &event, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
          !DuplicateHandle(process, sync->pending, process, &pending, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
         if (event)
            CloseHandle(event);
         if (pending)
            CloseHandle(pending);
         gsl_cpu_signals.clear();
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      gsl_cpu_signals.signals[i] = { signals[i].sync, event, pending };
      gsl_cpu_signals.count++;
   }
   return VK_SUCCESS;
}

static void
gsl_mark_cpu_signals_pending()
{
   for (uint32_t i = 0; i < gsl_cpu_signals.count; i++) {
      auto &signal = gsl_cpu_signals.signals[i];
      if (signal.pending && !SetEvent(signal.pending))
         vk_device_set_lost(gsl_cpu_signals.device, "GSL pending event signal failed");
   }
}

struct gsl_submit {
   struct util_dynarray entries;
};

static struct tu_gsl_api *
gsl_api(struct tu_physical_device *physical)
{
   return &static_cast<gsl_state *>(physical->gsl)->api;
}

static VkResult
gsl_sync_init(struct vk_device *device, struct vk_sync *sync, uint64_t initial)
{
   auto s = reinterpret_cast<gsl_sync *>(sync);
   s->event = CreateEventW(NULL, TRUE, initial != 0, NULL);
   if (!s->event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   s->pending = gsl_pending_sync_enabled() ? CreateEventW(NULL, TRUE, initial != 0, NULL) : NULL;
   if (gsl_pending_sync_enabled() && !s->pending) {
      CloseHandle(s->event);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   return VK_SUCCESS;
}

static void
gsl_sync_finish(struct vk_device *, struct vk_sync *sync)
{
   CloseHandle(reinterpret_cast<gsl_sync *>(sync)->event);
   if (reinterpret_cast<gsl_sync *>(sync)->pending)
      CloseHandle(reinterpret_cast<gsl_sync *>(sync)->pending);
}

static VkResult
gsl_sync_signal(struct vk_device *device, struct vk_sync *sync, uint64_t)
{
   auto s = reinterpret_cast<gsl_sync *>(sync);
   for (uint32_t i = 0; i < gsl_cpu_signals.count; i++) {
      auto &signal = gsl_cpu_signals.signals[i];
      if (signal.sync != sync || !signal.event)
         continue;
      BOOL result = SetEvent(signal.pending) && SetEvent(signal.event);
      CloseHandle(signal.event);
      CloseHandle(signal.pending);
      signal.event = signal.pending = NULL;
      return result ? VK_SUCCESS : vk_error(device, VK_ERROR_UNKNOWN);
   }
   HANDLE event = s->event;
   HANDLE pending = s->pending;
   if (pending && !SetEvent(pending))
      return vk_error(device, VK_ERROR_UNKNOWN);
   return SetEvent(event) ? VK_SUCCESS : vk_error(device, VK_ERROR_UNKNOWN);
}

static VkResult
gsl_sync_reset(struct vk_device *device, struct vk_sync *sync)
{
   auto s = reinterpret_cast<gsl_sync *>(sync);
   if (s->pending && !ResetEvent(s->pending))
      return vk_error(device, VK_ERROR_UNKNOWN);
   return ResetEvent(s->event) ? VK_SUCCESS : vk_error(device, VK_ERROR_UNKNOWN);
}

static VkResult
gsl_sync_move(struct vk_device *device, struct vk_sync *dst, struct vk_sync *src)
{
   auto d = reinterpret_cast<gsl_sync *>(dst);
   auto s = reinterpret_cast<gsl_sync *>(src);
   HANDLE event = d->event;
   d->event = s->event;
   s->event = event;
   event = d->pending;
   d->pending = s->pending;
   s->pending = event;
   return gsl_sync_reset(device, src);
}

static VkResult
gsl_sync_wait(struct vk_device *device, struct vk_sync *sync, uint64_t, enum vk_sync_wait_flags flags, uint64_t deadline)
{
   auto s = reinterpret_cast<gsl_sync *>(sync);
   HANDLE event = (flags & VK_SYNC_WAIT_PENDING) && s->pending ? s->pending : s->event;
   for (;;) {
      if (vk_device_is_lost(device))
         return VK_ERROR_DEVICE_LOST;
      DWORD timeout = 100;
      if (deadline != UINT64_MAX) {
         uint64_t now = os_time_get_nano();
         uint64_t remaining = deadline > now ? deadline - now : 0;
         timeout = MIN2(DIV_ROUND_UP(remaining, 1000000ull), uint64_t(100));
      }
      DWORD result = WaitForSingleObject(event, timeout);
      if (vk_device_is_lost(device))
         return VK_ERROR_DEVICE_LOST;
      if (result == WAIT_OBJECT_0)
         return VK_SUCCESS;
      if (result != WAIT_TIMEOUT)
         return vk_error(device, VK_ERROR_UNKNOWN);
      if (deadline != UINT64_MAX && os_time_get_nano() >= deadline)
         return VK_TIMEOUT;
   }
}

static const struct vk_sync_type gsl_sync_type = {
   .size = sizeof(struct gsl_sync),
   .features = static_cast<vk_sync_features>(
      VK_SYNC_FEATURE_BINARY | VK_SYNC_FEATURE_GPU_WAIT | VK_SYNC_FEATURE_GPU_MULTI_WAIT | VK_SYNC_FEATURE_CPU_WAIT |
      VK_SYNC_FEATURE_CPU_RESET | VK_SYNC_FEATURE_CPU_SIGNAL | VK_SYNC_FEATURE_WAIT_PENDING),
   .init = gsl_sync_init,
   .finish = gsl_sync_finish,
   .signal = gsl_sync_signal,
   .reset = gsl_sync_reset,
   .move = gsl_sync_move,
   .wait = gsl_sync_wait,
};

static const struct vk_sync_type *
gsl_sync_type_ptr()
{
   return &gsl_sync_type;
}

static VkResult
gsl_device_init(struct tu_device *dev)
{
   dev->fd = -1;
   if (debug_get_bool_option("TU_GSL_ASYNC_SUBMIT", true)) {
      vk_device_enable_threaded_submit(&dev->vk);
      dev->vk.submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED;
   }
   return VK_SUCCESS;
}

static void
gsl_device_finish(struct tu_device *)
{
}
static int
gsl_device_timestamp(struct tu_device *, uint64_t *)
{
   return -1;
}
static int
gsl_suspend_count(struct tu_device *, uint64_t *count)
{
   *count = 0;
   return 0;
}
static VkResult
gsl_device_status(struct tu_device *dev)
{
   return vk_device_is_lost(&dev->vk) ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static int
gsl_queue_new(struct tu_device *dev, struct tu_queue *queue)
{
   uint32_t extra[] = { GetCurrentThreadId(), 0x4000, 8, 0 };
   uint32_t context =
      gsl_api(dev->physical_device)->context_create(dev->physical_device->gsl_device, 8, 0x80008852u, extra);
   if (!context || context == UINT32_MAX)
      return -1;
   queue->msm_queue_id = context;
   queue->fence = 0;
   return 0;
}

static void
gsl_queue_close(struct tu_device *dev, struct tu_queue *queue)
{
   gsl_api(dev->physical_device)->context_destroy(dev->physical_device->gsl_device, queue->msm_queue_id);
}

static VkResult
gsl_bo_init(struct tu_device *dev,
            struct vk_object_base *base,
            struct tu_bo **out,
            uint64_t size,
            uint64_t align,
            uint64_t client_iova,
            VkMemoryPropertyFlags properties,
            enum tu_bo_alloc_flags flags,
            struct tu_sparse_vma *lazy,
            const char *name)
{
   if (client_iova || lazy || align > 65536)
      return vk_error(dev, VK_ERROR_FEATURE_NOT_PRESENT);
   size = align64(size, 65536);
   auto bo = static_cast<gsl_bo *>(calloc(1, sizeof(gsl_bo)));
   if (!bo)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   int ret = tu_gsl_alloc(gsl_api(dev->physical_device), dev->physical_device->gsl_device, &bo->memory, size);
   if (ret) {
      free(bo);
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY, "GSL allocation failed: %d", ret);
   }
   bo->bo.gem_handle = bo->memory.allocation;
   bo->bo.size = bo->memory.desc[2];
   bo->bo.iova = bo->memory.desc[1];
   bo->bo.map = reinterpret_cast<void *>(bo->memory.desc[0]);
   bo->bo.refcnt = 1;
   bo->bo.base = base;
   bo->bo.never_unmap = true;
   bo->bo.name = tu_debug_bos_add(dev, bo->bo.size, name);
   tu_dump_bo_init(dev, &bo->bo);
   *out = &bo->bo;
   return VK_SUCCESS;
}

static void
gsl_bo_finish(struct tu_device *dev, struct tu_bo *base)
{
   if (!p_atomic_dec_zero(&base->refcnt))
      return;
   auto bo = reinterpret_cast<gsl_bo *>(base);
   tu_debug_bos_del(dev, base);
   tu_dump_bo_del(dev, base);
   gsl_api(dev->physical_device)->memory_free(bo->memory.desc);
   free(bo);
}

static VkResult
gsl_bo_map(struct tu_device *dev, struct tu_bo *base, void *placed)
{
   auto bo = reinterpret_cast<gsl_bo *>(base);
   void *map = reinterpret_cast<void *>(bo->memory.desc[0]);
   if (placed && placed != map)
      return vk_error(dev, VK_ERROR_MEMORY_MAP_FAILED);
   base->map = map;
   return VK_SUCCESS;
}

static VkResult
gsl_import(struct tu_device *dev, struct tu_bo **, uint64_t, uint64_t, enum tu_bo_alloc_flags, int)
{
   return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
}
static int
gsl_export(struct tu_device *, struct tu_bo *)
{
   return -1;
}
static void
gsl_allow_dump(struct tu_device *, struct tu_bo *)
{
}

static void *
gsl_submit_create(struct tu_device *)
{
   return calloc(1, sizeof(struct gsl_submit));
}

static void
gsl_submit_finish(struct tu_device *, void *data)
{
   auto submit = static_cast<gsl_submit *>(data);
   util_dynarray_fini(&submit->entries);
   free(submit);
}

static void
gsl_submit_entries(struct tu_device *, void *data, struct tu_cs_entry *entries, unsigned count)
{
   auto submit = static_cast<gsl_submit *>(data);
   util_dynarray_append_array(&submit->entries, struct tu_cs_entry, entries, count);
}

static VkResult
gsl_queue_submit(struct tu_queue *queue,
                 void *data,
                 struct vk_sync_wait *waits,
                 uint32_t wait_count,
                 struct vk_sync_signal *signals,
                 uint32_t signal_count,
                 struct tu_u_trace_submission_data *)
{
   auto dev = queue->device;
   VkResult result = vk_sync_wait_many(&dev->vk, wait_count, waits, VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
   if (result != VK_SUCCESS)
      return result;
   auto submit = static_cast<gsl_submit *>(data);
   uint32_t count = util_dynarray_num_elements(&submit->entries, struct tu_cs_entry);
   if (count) {
      auto entries = static_cast<tu_cs_entry *>(util_dynarray_begin(&submit->entries));
      bool debug = os_get_option("TU_GSL_DEBUG") != NULL;
      auto commands = static_cast<tu_gsl_command *>(calloc(count, sizeof(tu_gsl_command)));
      if (!commands)
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
      for (unsigned i = 0; i < count; i++) {
         auto bo = reinterpret_cast<const gsl_bo *>(entries[i].bo);
         commands[i] = { &bo->memory, entries[i].size / 4, entries[i].offset, 0 };
         if (debug) {
            auto words = reinterpret_cast<const uint32_t *>(static_cast<const char *>(bo->bo.map) + entries[i].offset);
            fprintf(stderr, "GSL IB %u: iova=0x%llx, offset=%u, dwords=%u\n", i,
                    static_cast<unsigned long long>(bo->bo.iova), entries[i].offset, entries[i].size / 4);
            for (unsigned j = 0; j < entries[i].size / 4; j++)
               fprintf(stderr, "%08x%c", words[j], (j % 8 == 7) ? '\n' : ' ');
            fprintf(stderr, "\n");
         }
      }
      uint32_t timestamp = uint32_t(queue->fence) + 1;
      auto api = gsl_api(dev->physical_device);
      int ret = tu_gsl_submit_commands(api, dev->physical_device->gsl_device, queue->msm_queue_id, commands, count,
                                       timestamp, gsl_pending_sync_enabled() ? gsl_mark_cpu_signals_pending : NULL);
      free(commands);
      if (ret)
         return vk_device_set_lost(&dev->vk, "GSL submission failed: %d", ret);
      queue->fence = timestamp;
   }
   return vk_sync_signal_many(&dev->vk, signal_count, signals);
}

static VkResult
gsl_queue_wait(struct tu_queue *queue, uint32_t timestamp, uint64_t timeout_ns)
{
   if (!timestamp)
      return VK_SUCCESS;
   auto physical = queue->device->physical_device;
   uint32_t timeout =
      timeout_ns == UINT64_MAX ? UINT32_MAX : MIN2(DIV_ROUND_UP(timeout_ns, 1000000ull), uint64_t(UINT32_MAX - 1));
   int ret = gsl_api(physical)->wait(physical->gsl_device, queue->msm_queue_id, timestamp, timeout);
   if (!ret)
      return VK_SUCCESS;
   return vk_device_set_lost(&queue->device->vk, "GSL fence wait failed: %d", ret);
}

static const struct tu_knl gsl_knl = {
   .name = "gsl",
   .device_init = gsl_device_init,
   .device_finish = gsl_device_finish,
   .device_get_gpu_timestamp = gsl_device_timestamp,
   .device_get_suspend_count = gsl_suspend_count,
   .device_check_status = gsl_device_status,
   .submitqueue_new = gsl_queue_new,
   .submitqueue_close = gsl_queue_close,
   .bo_init = gsl_bo_init,
   .bo_init_dmabuf = gsl_import,
   .bo_export_dmabuf = gsl_export,
   .bo_map = gsl_bo_map,
   .bo_allow_dump = gsl_allow_dump,
   .bo_finish = gsl_bo_finish,
   .submit_create = gsl_submit_create,
   .submit_finish = gsl_submit_finish,
   .submit_add_entries = gsl_submit_entries,
   .queue_submit = gsl_queue_submit,
   .queue_wait_fence = gsl_queue_wait,
};

VkResult
tu_knl_gsl_load(struct tu_instance *instance)
{
   auto state = static_cast<gsl_state *>(calloc(1, sizeof(gsl_state)));
   if (!state)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   if (!tu_gsl_load(&state->api)) {
      free(state);
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER, "GSL library not available");
   }
   if (state->api.library_open(0x800)) {
      FreeLibrary(state->api.module);
      free(state);
      return vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "GSL library initialization failed");
   }
   uint32_t handle = state->api.device_open(1, 0);
   uint32_t info[8] = {};
   if (!handle || handle == UINT32_MAX || state->api.device_getinfo(handle, info)) {
      if (handle && handle != UINT32_MAX)
         state->api.device_close(handle);
      state->api.library_close();
      FreeLibrary(state->api.module);
      free(state);
      return vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "GSL device initialization failed");
   }
   auto physical = static_cast<tu_physical_device *>(
      vk_zalloc(&instance->vk.alloc, sizeof(tu_physical_device), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE));
   if (!physical) {
      state->api.device_close(handle);
      state->api.library_close();
      FreeLibrary(state->api.module);
      free(state);
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   physical->instance = instance;
   physical->gsl = state;
   physical->gsl_device = handle;
   physical->local_fd = physical->master_fd = physical->kgsl_dma_fd = -1;
   physical->dev_id.chip_id = info[1];
   physical->gmem_size = info[7];
   physical->uche_trap_base = 0x1fffffffff000ull;
   physical->ubwc_config.bank_swizzle_levels = ~0u;
   physical->ubwc_config.macrotile_mode = FDL_MACROTILE_INVALID;
   physical->submitqueue_priority_count = 1;
   physical->timeline_type = vk_sync_timeline_get_type(&gsl_sync_type);
   physical->sync_types[0] = &gsl_sync_type;
   physical->sync_types[1] = &physical->timeline_type.sync;
   physical->heap.size = tu_get_system_heap_size(physical);
   physical->heap.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
   instance->knl = &gsl_knl;
   VkResult result = tu_physical_device_init(physical, instance);
   if (result != VK_SUCCESS) {
      tu_knl_gsl_finish(physical);
      vk_free(&instance->vk.alloc, physical);
      return result;
   }
   unsigned dst = 0;
   for (auto reg : physical->dev_info.magic_raw) {
      if (reg.reg != REG_A6XX_TPL1_DBG_ECO_CNTL1 && reg.reg != REG_A7XX_RB_UNKNOWN_8E79)
         physical->dev_info.magic_raw[dst++] = reg;
   }
   while (dst < ARRAY_SIZE(physical->dev_info.magic_raw))
      physical->dev_info.magic_raw[dst++] = {};
   list_addtail(&physical->vk.link, &instance->vk.physical_devices.list);
   return VK_SUCCESS;
}

void
tu_knl_gsl_finish(struct tu_physical_device *physical)
{
   auto state = static_cast<gsl_state *>(physical->gsl);
   state->api.device_close(physical->gsl_device);
   state->api.library_close();
   FreeLibrary(state->api.module);
   free(state);
   physical->gsl = NULL;
}
