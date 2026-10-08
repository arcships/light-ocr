#include "inference/amdnpu/runtime_api.h"

#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#include "iree/base/api.h"
#include "iree/async/frontier_tracker.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/hal/api.h"
#include "iree/hal/buffer_view_util.h"
#include "iree/hal/device_group.h"
#include "iree/hal/drivers/local_sync/registration/driver_module.h"
#include "iree/io/file_handle.h"
#include "iree/io/formats/parser_registry.h"
#include "iree/io/parameter_index.h"
#include "iree/io/parameter_index_provider.h"
#include "iree/modules/hal/module.h"
#include "iree/modules/io/parameters/module.h"
#include "iree/vm/api.h"
#include "iree/vm/bytecode/module.h"
#include "iree-amd-aie/driver/amdxdna/registration/driver_module.h"

namespace {
constexpr size_t kClasses = 18710;
iree_allocator_t allocator() { return iree_allocator_system(); }

void check(iree_status_t status) {
  if (iree_status_is_ok(status)) return;
  char message[2048] = {};
  iree_host_size_t length = 0;
  iree_status_format(status, sizeof(message), message, &length);
  iree_status_ignore(status);
  throw std::runtime_error(message[0] ? message : "IREE AMD operation failed");
}

template <typename F>
int boundary(char* error, size_t capacity, F&& work) noexcept {
  if (error && capacity) error[0] = 0;
  try { work(); return 0; }
  catch (const std::exception& e) {
    if (error && capacity) {
      std::strncpy(error, e.what(), capacity - 1);
      error[capacity - 1] = 0;
    }
  } catch (...) {
    if (error && capacity) {
      std::strncpy(error, "Unknown IREE AMD runtime failure", capacity - 1);
      error[capacity - 1] = 0;
    }
  }
  return 1;
}

struct Engine {
  std::vector<uint8_t> weights;
  iree_vm_instance_t* instance = nullptr;
  iree_hal_driver_registry_t* registry = nullptr;
  iree_hal_device_t* cpu = nullptr;
  iree_hal_device_t* npu = nullptr;
  iree_async_proactor_pool_t* proactor_pool = nullptr;
  iree_async_frontier_tracker_t* frontier_tracker = nullptr;
  iree_hal_device_group_t* group = nullptr;
  iree_vm_module_t* hal = nullptr;
  iree_vm_module_t* parameters = nullptr;
  ~Engine() {
    iree_vm_module_release(parameters);
    iree_vm_module_release(hal);
    iree_hal_device_group_release(group);
    iree_hal_device_release(npu);
    iree_hal_device_release(cpu);
    iree_async_frontier_tracker_release(frontier_tracker);
    iree_async_proactor_pool_release(proactor_pool);
    iree_hal_driver_registry_free(registry);
    iree_vm_instance_release(instance);
  }
};

struct Session {
  Engine* engine = nullptr;
  uint32_t width = 0;
  std::vector<uint8_t> bytecode;
  iree_vm_module_t* model = nullptr;
  iree_vm_context_t* context = nullptr;
  iree_vm_function_t function = {};
  ~Session() {
    iree_vm_context_release(context);
    iree_vm_module_release(model);
  }
};

iree_status_t choose_allocator(
    void* user, iree_host_size_t count,
    const iree_hal_device_queue_affinity_pair_t* pairs,
    iree_hal_memory_type_t, iree_hal_buffer_usage_t,
    iree_hal_module_device_allocator_select_flags_t,
    iree_host_size_t* selection) {
  // CPU kernels can map AMD's host-only BOs. AMD dispatches cannot consume
  // local CPU allocations: shared resources must originate on the NPU.
  auto* engine = static_cast<Engine*>(user);
  *selection = 0;
  for (iree_host_size_t i = 0; i < count; ++i)
    if (pairs[i].device == engine->npu) { *selection = i; break; }
  return iree_ok_status();
}

void create_device(Engine& engine, const char* name, iree_hal_device_t** output) {
  iree_hal_driver_t* driver = nullptr;
  check(iree_hal_driver_registry_try_create(engine.registry,
        iree_make_cstring_view(name), allocator(), &driver));
  auto params = iree_hal_device_create_params_default();
  params.proactor_pool = engine.proactor_pool;
  auto status = iree_hal_driver_create_default_device(driver, &params, allocator(), output);
  iree_hal_driver_release(driver);
  check(status);
}

int create(const uint8_t* data, size_t size, void** output, char* error, size_t capacity) {
  if (output) *output = nullptr;
  return boundary(error, capacity, [&] {
    if (!data || !size || !output) throw std::invalid_argument("Missing parameter archive");
    auto engine = std::make_unique<Engine>();
    engine->weights.assign(data, data + size);
    check(iree_vm_instance_create(IREE_VM_TYPE_CAPACITY_DEFAULT, allocator(), &engine->instance));
    check(iree_hal_module_register_all_types(engine->instance));
    check(iree_hal_driver_registry_allocate(allocator(), &engine->registry));
    check(iree_hal_local_sync_driver_module_register(engine->registry));
    check(iree_hal_amdxdna_driver_module_register(engine->registry));
    check(iree_async_proactor_pool_create(1, nullptr,
          iree_async_proactor_pool_options_default(), allocator(), &engine->proactor_pool));
    check(iree_async_frontier_tracker_create(iree_async_frontier_tracker_options_default(),
          allocator(), &engine->frontier_tracker));
    create_device(*engine, "local-sync", &engine->cpu);
    create_device(*engine, "amdxdna", &engine->npu);
    iree_hal_device_group_builder_t builder;
    iree_hal_device_group_builder_initialize(&builder, engine->frontier_tracker);
    check(iree_hal_device_group_builder_add_device(&builder, engine->cpu));
    check(iree_hal_device_group_builder_add_device(&builder, engine->npu));
    check(iree_hal_device_group_builder_finalize(&builder, allocator(), &engine->group));
    auto policy = iree_hal_module_device_policy_default();
    policy.allocator_select = {choose_allocator, engine.get()};
    check(iree_hal_module_create(engine->instance, policy, engine->group,
          IREE_HAL_MODULE_FLAG_SYNCHRONOUS, iree_hal_module_debug_sink_null(),
          allocator(), &engine->hal));
    iree_io_file_handle_t* file = nullptr;
    iree_io_parameter_index_t* index = nullptr;
    iree_io_parameter_provider_t* provider = nullptr;
    // Locals are released on both success and failure. The parameter module
    // retains its provider/index/file, whose byte span is owned by Engine.
    auto status = iree_io_file_handle_wrap_host_allocation(
        IREE_IO_FILE_ACCESS_READ, iree_make_byte_span(engine->weights.data(), size),
        iree_io_file_handle_release_callback_null(), allocator(), &file);
    if (iree_status_is_ok(status)) status = iree_io_parameter_index_create(allocator(), &index);
    if (iree_status_is_ok(status)) status = iree_io_parse_file_index(IREE_SV("irpa"), file, index, allocator());
    if (iree_status_is_ok(status)) status = iree_io_parameter_index_provider_create(
        IREE_SV("recognition"), index,
        IREE_IO_PARAMETER_INDEX_PROVIDER_DEFAULT_MAX_CONCURRENT_OPERATIONS, allocator(), &provider);
    if (iree_status_is_ok(status)) status = iree_io_parameters_module_create(
        engine->instance, 1, &provider, allocator(), &engine->parameters);
    iree_io_parameter_provider_release(provider);
    iree_io_parameter_index_release(index);
    iree_io_file_handle_release(file);
    check(status);
    *output = engine.release();
  });
}

int load(void* handle, const uint8_t* data, size_t size, uint32_t width,
         void** output, char* error, size_t capacity) {
  if (output) *output = nullptr;
  return boundary(error, capacity, [&] {
    if (!handle || !data || !size || !output || !width || width % 8)
      throw std::invalid_argument("Invalid recognition module");
    auto session = std::make_unique<Session>();
    session->engine = static_cast<Engine*>(handle);
    session->width = width;
    session->bytecode.assign(data, data + size);
    check(iree_vm_bytecode_module_create(session->engine->instance,
          IREE_VM_BYTECODE_MODULE_FLAG_NONE,
          iree_make_const_byte_span(session->bytecode.data(), size),
          iree_allocator_null(), allocator(), &session->model));
    check(iree_vm_module_lookup_function_by_name(session->model,
          IREE_VM_FUNCTION_LINKAGE_EXPORT, IREE_SV("recognition"), &session->function));
    iree_vm_module_t* modules[] = {session->engine->hal, session->engine->parameters, session->model};
    check(iree_vm_context_create_with_modules(session->engine->instance,
          IREE_VM_CONTEXT_FLAG_NONE, 3, modules, allocator(), &session->context));
    *output = session.release();
  });
}

int run(void* handle, const float* input, size_t input_count, float* output,
        size_t output_count, char* error, size_t capacity) {
  return boundary(error, capacity, [&] {
    auto* session = static_cast<Session*>(handle);
    if (!session || !input || !output || input_count != size_t(3*48)*session->width ||
        output_count != size_t(session->width/8)*kClasses)
      throw std::invalid_argument("Recognition tensor dimensions differ from the bucket contract");
    iree_hal_dim_t shape[] = {1,3,48,session->width};
    iree_hal_buffer_params_t params = {};
    params.type = IREE_HAL_MEMORY_TYPE_HOST_LOCAL | IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE;
    params.usage = IREE_HAL_BUFFER_USAGE_DEFAULT;
    iree_hal_buffer_view_t* input_view = nullptr;
    iree_vm_list_t *inputs = nullptr, *outputs = nullptr;
    auto status = iree_hal_buffer_view_allocate_buffer_copy(
        session->engine->npu, iree_hal_device_allocator(session->engine->npu),
        4, shape, IREE_HAL_ELEMENT_TYPE_FLOAT_32, IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR,
        params, iree_make_const_byte_span(input, input_count*sizeof(float)), &input_view);
    if (iree_status_is_ok(status)) status = iree_vm_list_create(
        iree_vm_make_undefined_type_def(), 1, allocator(), &inputs);
    if (iree_status_is_ok(status)) status = iree_vm_list_create(
        iree_vm_make_undefined_type_def(), 1, allocator(), &outputs);
    if (iree_status_is_ok(status)) {
      auto ref = iree_hal_buffer_view_move_ref(input_view);
      input_view = nullptr;
      status = iree_vm_list_push_ref_move(inputs, &ref);
      iree_vm_ref_release(&ref);
    }
    if (iree_status_is_ok(status)) status = iree_vm_invoke(session->context,
        session->function, IREE_VM_INVOCATION_FLAG_NONE, nullptr, inputs, outputs, allocator());
    if (iree_status_is_ok(status)) {
      auto* view = static_cast<iree_hal_buffer_view_t*>(iree_vm_list_get_ref_deref(
          outputs, 0, iree_hal_buffer_view_type()));
      if (!view || iree_vm_list_size(outputs) != 1 ||
          iree_hal_buffer_view_element_type(view) != IREE_HAL_ELEMENT_TYPE_FLOAT_32 ||
          iree_hal_buffer_view_shape_rank(view) != 3 ||
          iree_hal_buffer_view_shape_dim(view,0) != 1 ||
          iree_hal_buffer_view_shape_dim(view,1) != session->width/8 ||
          iree_hal_buffer_view_shape_dim(view,2) != kClasses) {
        status = iree_make_status(IREE_STATUS_INVALID_ARGUMENT, "Invalid recognition output ABI");
      } else {
        iree_hal_buffer_mapping_t mapping = {};
        status = iree_hal_buffer_map_range(iree_hal_buffer_view_buffer(view),
            IREE_HAL_MAPPING_MODE_SCOPED, IREE_HAL_MEMORY_ACCESS_READ,
            0, output_count*sizeof(float), &mapping);
        if (iree_status_is_ok(status)) {
          std::memcpy(output,mapping.contents.data,output_count*sizeof(float));
          status = iree_hal_buffer_unmap_range(&mapping);
        }
      }
    }
    iree_hal_buffer_view_release(input_view);
    iree_vm_list_release(outputs);
    iree_vm_list_release(inputs);
    check(status);
  });
}

void destroy_session(void* handle) { delete static_cast<Session*>(handle); }
void destroy_engine(void* handle) { delete static_cast<Engine*>(handle); }
const LightOcrAieApiV1 api = {1,sizeof(LightOcrAieApiV1),create,load,run,destroy_session,destroy_engine};
}  // namespace

extern "C" __attribute__((visibility("default")))
const LightOcrAieApiV1* LightOcrAieGetApi(uint32_t version) {
  return version == 1 ? &api : nullptr;
}
