/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_initializer_framework_helper.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "tensorflow/compiler/xla/stream_executor/tpu/libtftpu.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_api_dlsym_set_fn.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_initialize_util.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_executor_c_api.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_ops_c_api.h"
#include "tensorflow/tsl/platform/errors.h"
#include "tensorflow/tsl/platform/logging.h"

#if !defined(PLATFORM_GOOGLE)
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_api.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_platform.h"
#include "tensorflow/tsl/platform/env.h"
#elif defined(LIBTPU_STATIC)
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_api.h"
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_platform.h"
#endif  // PLATFORM_GOOGLE

namespace tensorflow {
namespace tpu {

#if !defined(PLATFORM_GOOGLE)
#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_library_init_fns.inc"

tsl::Status InitializeTpuLibrary(void* library_handle) {
  tsl::Status s = InitializeTpuStructFns(library_handle);

  // Retrieve arguments from environment if applicable
  std::pair<std::vector<std::string>, std::vector<const char*>> args =
      GetLibTpuInitArguments();

  // TPU platform registration must only be performed after the library is
  // loaded. We do not want to register a TPU platform in XLA without the
  // supporting library providing the necessary APIs.
  if (s.ok()) {
    void (*initialize_fn)(bool init_library, int num_args, const char** args);
    initialize_fn = reinterpret_cast<decltype(initialize_fn)>(
        dlsym(library_handle, "TfTpu_Initialize"));
    (*initialize_fn)(/*init_library=*/true, args.second.size(),
                     args.second.data());

    RegisterTpuPlatform();
  }

  return s;
}

// TODO(b/261484192): refactor this function to align with supporting different
// PJRT plugins.
tsl::Status FindAndLoadTpuLibrary() {
  const char* env_value = getenv("TPU_LIBRARY_PATH");
  const char* libtpu_path =
      env_value && strlen(env_value) > 0 ? env_value : "libtpu.so";
  LOG(INFO) << "Libtpu path is: " << libtpu_path;
  void* library = dlopen(libtpu_path, RTLD_LAZY);
  if (library) {
    // We can open the shared library which means we are in a TPU environment.
    // Try to acquire exclusive access.
    TF_RETURN_IF_ERROR(TryAcquireTpuLock());
    TF_RETURN_IF_ERROR(InitializeTpuLibrary(library));
  } else {
    LOG(INFO) << "Failed to open libtpu: " << dlerror();
  }

  return ::tsl::OkStatus();
}

#elif defined(LIBTPU_STATIC)

#include "tensorflow/compiler/xla/stream_executor/tpu/tpu_library_init_fns.inc"

tsl::Status InitializeTpuLibrary() {
  // Retrieve arguments from environment if applicable
  std::pair<std::vector<std::string>, std::vector<const char*>> args =
      GetLibTpuInitArguments();

  TfTpu_Initialize(/*init_library*/ true, args.second.size(),
                   args.second.data());

  RegisterTpuPlatform();
  return ::tsl::OkStatus();
}

tsl::Status FindAndLoadTpuLibrary() {
  // We can open the shared library which means we are in a TPU environment.
  // Try to acquire exclusive access.
  TF_RETURN_IF_ERROR(TryAcquireTpuLock());
  TF_RETURN_IF_ERROR(InitializeTpuLibrary());
  return ::tsl::OkStatus();
}

#else   // PLATFORM_GOOGLE
tsl::Status InitializeTpuLibrary(void* library_handle) {
  return tsl::errors::Unimplemented(
      "You must statically link in a TPU library.");
}
#endif  // PLATFORM_GOOGLE

}  // namespace tpu
}  // namespace tensorflow
