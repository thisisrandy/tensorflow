/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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
#ifndef TENSORFLOW_CORE_COMMON_RUNTIME_EAGER_EXECUTE_NODE_H_
#define TENSORFLOW_CORE_COMMON_RUNTIME_EAGER_EXECUTE_NODE_H_

#include "absl/types/span.h"
#include "tensorflow/core/common_runtime/device.h"
#include "tensorflow/core/common_runtime/eager/context.h"
#include "tensorflow/core/common_runtime/eager/eager_executor.h"
#include "tensorflow/core/common_runtime/eager/execute.h"
#include "tensorflow/core/common_runtime/eager/kernel_and_device.h"
#include "tensorflow/core/common_runtime/eager/tensor_handle.h"
#include "tensorflow/core/framework/step_stats.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/gtl/inlined_vector.h"
#include "tensorflow/core/lib/strings/strcat.h"

namespace tensorflow {

class ExecuteNode : public EagerNode {
 public:
  ExecuteNode(EagerContext* ctx,
              const gtl::InlinedVector<TensorHandle*, 4>& inputs,
              core::RefCountPtr<KernelAndDevice> kernel,
              GraphCollector* graph_collector,
              const DataTypeVector& output_dtypes,
              CancellationManager* cancellation_manager,
              absl::Span<TensorHandle*> retvals)
      : EagerNode(),
        ctx_(ctx),
        inputs_(inputs),
        kernel_(std::move(kernel)),
        graph_collector_(graph_collector),
        cancellation_manager_(cancellation_manager) {
    // Copy the output handles, since the container for them might get
    // destroyed.
    for (auto handle : retvals) {
      handle->Ref();
      retvals_.push_back(handle);
    }

    // This is required to ensure that the tensor handles stay alive across the
    // execution.
    for (auto handle : inputs_) {
      handle->Ref();
    }
  }

  ~ExecuteNode() override {
    for (auto handle : retvals_) {
      handle->Unref();
    }

    for (auto handle : inputs_) {
      handle->Unref();
    }
  }

  Status Run() override {
    const Status status =
        EagerKernelExecute(ctx_, inputs_, kernel_, graph_collector_,
                           cancellation_manager_, absl::MakeSpan(retvals_));
    if (!status.ok()) {
      Abort(status);
      return status;
    }
    // If status is ok, EagerKernelExecute would have called SetTensor on
    // all the output handles.
    return Status::OK();
  }

  void Abort(Status status) override {
    for (auto handle : retvals_) {
      handle->Poison(status);
    }
  }

  string DebugString() const override {
    string out = "[ExecuteNode]";
    strings::StrAppend(&out, " kernel: ", kernel_->name());
    return out;
  }

 private:
  EagerContext* ctx_;
  gtl::InlinedVector<TensorHandle*, 4> inputs_;
  core::RefCountPtr<KernelAndDevice> kernel_;
  GraphCollector* graph_collector_;
  CancellationManager* const cancellation_manager_;
  gtl::InlinedVector<TensorHandle*, 2> retvals_;
};

}  // namespace tensorflow

#endif  // TENSORFLOW_CORE_COMMON_RUNTIME_EAGER_EXECUTE_NODE_H_
