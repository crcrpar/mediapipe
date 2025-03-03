// Copyright 2019 The MediaPipe Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mediapipe/java/com/google/mediapipe/framework/jni/graph.h"

#include <pthread.h>

#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/port/canonical_errors.h"
#include "mediapipe/framework/port/logging.h"
#include "mediapipe/framework/port/proto_ns.h"
#include "mediapipe/framework/port/status.h"
#include "mediapipe/framework/port/threadpool.h"
#include "mediapipe/framework/tool/executor_util.h"
#include "mediapipe/framework/tool/name_util.h"
#include "mediapipe/gpu/gpu_shared_data_internal.h"
#include "mediapipe/gpu/graph_support.h"
#include "mediapipe/java/com/google/mediapipe/framework/jni/jni_util.h"
#include "mediapipe/java/com/google/mediapipe/framework/jni/packet_context_jni.h"
#ifdef __ANDROID__
#include "mediapipe/util/android/file/base/helpers.h"
#else
#include "mediapipe/framework/port/file_helpers.h"
#endif  // __ANDROID__
#ifndef MEDIAPIPE_DISABLE_GPU
#include "mediapipe/gpu/egl_surface_holder.h"
#endif  // !defined(MEDIAPIPE_DISABLE_GPU)

namespace mediapipe {
namespace android {

namespace internal {
// PacketWithContext is the native counterpart of the Java Packet.
class PacketWithContext {
 public:
  PacketWithContext(Graph* context, const Packet& packet)
      : context_(context), packet_(packet) {}

  ~PacketWithContext() {}

  Graph* GetContext() { return context_; }

  Packet& packet() { return packet_; }

 private:
  Graph* context_;
  Packet packet_;
};

// A callback handler that wraps the java callback, and submits it for
// execution through Graph.
class CallbackHandler {
 public:
  CallbackHandler(Graph* context, jobject callback)
      : context_(context), java_callback_(callback) {}

  ~CallbackHandler() {
    // The jobject global reference is managed by the Graph directly.
    // So no-op here.
    if (java_callback_) {
      LOG(ERROR) << "Java callback global reference is not released.";
    }
  }

  void PacketCallback(const Packet& packet) {
    context_->CallbackToJava(mediapipe::java::GetJNIEnv(), java_callback_,
                             packet);
  }

  void PacketWithHeaderCallback(const Packet& packet, const Packet& header) {
    context_->CallbackToJava(mediapipe::java::GetJNIEnv(), java_callback_,
                             packet, header);
  }

  std::function<void(const Packet&)> CreateCallback() {
    return std::bind(&CallbackHandler::PacketCallback, this,
                     std::placeholders::_1);
  }

  std::function<void(const Packet&, const Packet&)> CreateCallbackWithHeader() {
    return std::bind(&CallbackHandler::PacketWithHeaderCallback, this,
                     std::placeholders::_1, std::placeholders::_2);
  }

  // Releases the global reference to the java callback object.
  // This is called by the Graph, since releasing of a jni object
  // requires JNIEnv object that we can not keep a copy of.
  void ReleaseCallback(JNIEnv* env) {
    env->DeleteGlobalRef(java_callback_);
    java_callback_ = nullptr;
  }

 private:
  Graph* context_;
  // java callback object
  jobject java_callback_;
};
}  // namespace internal

Graph::Graph()
    : graph_loaded_(false),
      executor_stack_size_increased_(false),
      global_java_packet_cls_(nullptr) {}

Graph::~Graph() {
  if (running_graph_) {
    running_graph_->Cancel();
    running_graph_->WaitUntilDone().IgnoreError();
  }
  // Cleans up the jni objects.
  JNIEnv* env = mediapipe::java::GetJNIEnv();
  if (env == nullptr) {
    LOG(ERROR) << "Can't attach to java thread, no jni clean up performed.";
    return;
  }
  for (const auto& handler : callback_handlers_) {
    handler->ReleaseCallback(env);
  }
  if (global_java_packet_cls_) {
    env->DeleteGlobalRef(global_java_packet_cls_);
    global_java_packet_cls_ = nullptr;
  }
}

int64_t Graph::WrapPacketIntoContext(const Packet& packet) {
  absl::MutexLock lock(&all_packets_mutex_);
  auto packet_context = new internal::PacketWithContext(this, packet);
  // Since the value of the all_packets_ map is a unique_ptr, resets it with the
  // new allocated object.
  all_packets_[packet_context].reset(packet_context);
  VLOG(2) << "Graph packet reference buffer size: " << all_packets_.size();
  return reinterpret_cast<int64_t>(packet_context);
}

// static
Packet Graph::GetPacketFromHandle(int64_t packet_handle) {
  internal::PacketWithContext* packet_with_context =
      reinterpret_cast<internal::PacketWithContext*>(packet_handle);
  return packet_with_context->packet();
}

// static
Graph* Graph::GetContextFromHandle(int64_t packet_handle) {
  internal::PacketWithContext* packet_with_context =
      reinterpret_cast<internal::PacketWithContext*>(packet_handle);
  return packet_with_context->GetContext();
}

// static
bool Graph::RemovePacket(int64_t packet_handle) {
  internal::PacketWithContext* packet_with_context =
      reinterpret_cast<internal::PacketWithContext*>(packet_handle);
  Graph* context = packet_with_context->GetContext();
  absl::MutexLock lock(&(context->all_packets_mutex_));
  return context->all_packets_.erase(packet_with_context) != 0;
}

void Graph::EnsureMinimumExecutorStackSizeForJava() {}

::mediapipe::Status Graph::AddCallbackHandler(std::string output_stream_name,
                                              jobject java_callback) {
  if (!graph_loaded_) {
    return ::mediapipe::InternalError("Graph is not loaded!");
  }
  std::unique_ptr<internal::CallbackHandler> handler(
      new internal::CallbackHandler(this, java_callback));
  std::string side_packet_name;
  tool::AddCallbackCalculator(output_stream_name, &graph_, &side_packet_name,
                              /* use_std_function = */ true);
  EnsureMinimumExecutorStackSizeForJava();
  side_packets_callbacks_.emplace(
      side_packet_name, MakePacket<std::function<void(const Packet&)>>(
                            handler->CreateCallback()));
  callback_handlers_.emplace_back(std::move(handler));
  return ::mediapipe::OkStatus();
}

::mediapipe::Status Graph::AddCallbackWithHeaderHandler(
    std::string output_stream_name, jobject java_callback) {
  if (!graph_loaded_) {
    return ::mediapipe::InternalError("Graph is not loaded!");
  }
  std::unique_ptr<internal::CallbackHandler> handler(
      new internal::CallbackHandler(this, java_callback));
  std::string side_packet_name;
  tool::AddCallbackWithHeaderCalculator(output_stream_name, output_stream_name,
                                        &graph_, &side_packet_name,
                                        /* use_std_function = */ true);
  EnsureMinimumExecutorStackSizeForJava();
  side_packets_callbacks_.emplace(
      side_packet_name,
      MakePacket<std::function<void(const Packet&, const Packet&)>>(
          handler->CreateCallbackWithHeader()));
  callback_handlers_.emplace_back(std::move(handler));
  return ::mediapipe::OkStatus();
}

int64_t Graph::AddSurfaceOutput(const std::string& output_stream_name) {
  if (!graph_loaded_) {
    LOG(ERROR) << "Graph is not loaded!";
    return 0;
  }

#ifdef MEDIAPIPE_DISABLE_GPU
  LOG(FATAL) << "GPU support has been disabled in this build!";
#else
  CalculatorGraphConfig::Node* sink_node = graph_.add_node();
  sink_node->set_name(::mediapipe::tool::GetUnusedNodeName(
      graph_, absl::StrCat("egl_surface_sink_", output_stream_name)));
  sink_node->set_calculator("GlSurfaceSinkCalculator");
  sink_node->add_input_stream(output_stream_name);
  sink_node->add_input_side_packet(
      absl::StrCat(kGpuSharedTagName, ":", kGpuSharedSidePacketName));

  const std::string input_side_packet_name =
      ::mediapipe::tool::GetUnusedSidePacketName(
          graph_, absl::StrCat(output_stream_name, "_surface"));
  sink_node->add_input_side_packet(
      absl::StrCat("SURFACE:", input_side_packet_name));

  auto it_inserted = output_surface_side_packets_.emplace(
      input_side_packet_name,
      AdoptAsUniquePtr(new mediapipe::EglSurfaceHolder()));

  return WrapPacketIntoContext(it_inserted.first->second);
#endif  // defined(MEDIAPIPE_DISABLE_GPU)
}

::mediapipe::Status Graph::LoadBinaryGraph(std::string path_to_graph) {
  std::string graph_config_string;
  ::mediapipe::Status status =
      mediapipe::file::GetContents(path_to_graph, &graph_config_string);
  if (!status.ok()) {
    return status;
  }
  if (!graph_.ParseFromString(graph_config_string)) {
    return ::mediapipe::InvalidArgumentError(
        absl::StrCat("Failed to parse the graph: ", path_to_graph));
  }
  graph_loaded_ = true;
  return ::mediapipe::OkStatus();
}

::mediapipe::Status Graph::LoadBinaryGraph(const char* data, int size) {
  if (!graph_.ParseFromArray(data, size)) {
    return ::mediapipe::InvalidArgumentError("Failed to parse the graph");
  }
  graph_loaded_ = true;
  return ::mediapipe::OkStatus();
}

const CalculatorGraphConfig& Graph::GetCalculatorGraphConfig() {
  return graph_;
}

void Graph::CallbackToJava(JNIEnv* env, jobject java_callback_obj,
                           const Packet& packet) {
  jclass callback_cls = env->GetObjectClass(java_callback_obj);
  jmethodID processMethod = env->GetMethodID(
      callback_cls, "process",
      absl::StrFormat("(L%s;)V", std::string(Graph::kJavaPacketClassName))
          .c_str());

  int64_t packet_handle = WrapPacketIntoContext(packet);
  // Creates a Java Packet.
  VLOG(2) << "Creating java packet preparing for callback to java.";
  jobject java_packet =
      CreateJavaPacket(env, global_java_packet_cls_, packet_handle);
  VLOG(2) << "Calling java callback.";
  env->CallVoidMethod(java_callback_obj, processMethod, java_packet);
  // release the packet after callback.
  RemovePacket(packet_handle);
  env->DeleteLocalRef(callback_cls);
  env->DeleteLocalRef(java_packet);
  VLOG(2) << "Returned from java callback.";
}

void Graph::CallbackToJava(JNIEnv* env, jobject java_callback_obj,
                           const Packet& packet, const Packet& header_packet) {
  jclass callback_cls = env->GetObjectClass(java_callback_obj);
  jmethodID processMethod = env->GetMethodID(
      callback_cls, "process",
      absl::StrFormat("(L%s;L%s;)V", std::string(Graph::kJavaPacketClassName),
                      std::string(Graph::kJavaPacketClassName))
          .c_str());

  int64_t packet_handle = WrapPacketIntoContext(packet);
  int64_t header_packet_handle = WrapPacketIntoContext(header_packet);
  // Creates a Java Packet.
  jobject java_packet =
      CreateJavaPacket(env, global_java_packet_cls_, packet_handle);
  jobject java_header_packet =
      CreateJavaPacket(env, global_java_packet_cls_, header_packet_handle);
  env->CallVoidMethod(java_callback_obj, processMethod, java_packet,
                      java_header_packet);
  // release the packet after callback.
  RemovePacket(packet_handle);
  RemovePacket(header_packet_handle);
  env->DeleteLocalRef(callback_cls);
  env->DeleteLocalRef(java_packet);
  env->DeleteLocalRef(java_header_packet);
}

void Graph::SetPacketJavaClass(JNIEnv* env) {
  if (global_java_packet_cls_ == nullptr) {
    jclass packet_cls =
        env->FindClass(mediapipe::android::Graph::kJavaPacketClassName);
    global_java_packet_cls_ =
        reinterpret_cast<jclass>(env->NewGlobalRef(packet_cls));
  }
}

::mediapipe::Status Graph::RunGraphUntilClose(JNIEnv* env) {
  // Get a global reference to the packet class, so it can be used in other
  // native thread for call back.
  SetPacketJavaClass(env);
  // Running as a synchronized mode, the same Java thread is available through
  // out the run.
  CalculatorGraph calculator_graph(graph_);
  // TODO: gpu & services set up!
  ::mediapipe::Status status =
      calculator_graph.Run(CreateCombinedSidePackets());
  LOG(INFO) << "Graph run finished.";

  return status;
}

::mediapipe::Status Graph::StartRunningGraph(JNIEnv* env) {
  if (running_graph_) {
    return ::mediapipe::InternalError("Graph is already running.");
  }
  // Get a global reference to the packet class, so it can be used in other
  // native thread for call back.
  SetPacketJavaClass(env);
  // Running as a synchronized mode, the same Java thread is available
  // throughout the run.
  running_graph_.reset(new CalculatorGraph());
  // Set the mode for adding packets to graph input streams.
  running_graph_->SetGraphInputStreamAddMode(graph_input_stream_add_mode_);
  if (VLOG_IS_ON(2)) {
    LOG(INFO) << "input side packet streams:";
    for (auto& name : graph_.input_stream()) {
      LOG(INFO) << name;
    }
  }
  ::mediapipe::Status status;
#ifndef MEDIAPIPE_DISABLE_GPU
  status = running_graph_->SetGpuResources(gpu_resources_);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    running_graph_.reset(nullptr);
    return status;
  }
#endif  // !defined(MEDIAPIPE_DISABLE_GPU)

  for (const auto& service_packet : service_packets_) {
    status = running_graph_->SetServicePacket(*service_packet.first,
                                              service_packet.second);
    if (!status.ok()) {
      LOG(ERROR) << status.message();
      running_graph_.reset(nullptr);
      return status;
    }
  }

  status = running_graph_->Initialize(graph_);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    running_graph_.reset(nullptr);
    return status;
  }
  LOG(INFO) << "Start running the graph, waiting for inputs.";
  status =
      running_graph_->StartRun(CreateCombinedSidePackets(), stream_headers_);
  if (!status.ok()) {
    LOG(ERROR) << status;
    running_graph_.reset(nullptr);
    return status;
  }
  return mediapipe::OkStatus();
}

::mediapipe::Status Graph::SetTimestampAndMovePacketToInputStream(
    const std::string& stream_name, int64_t packet_handle, int64_t timestamp) {
  internal::PacketWithContext* packet_with_context =
      reinterpret_cast<internal::PacketWithContext*>(packet_handle);
  Packet& packet = packet_with_context->packet();

  // Set the timestamp of the packet in-place by calling the rvalue-reference
  // version of At here.
  packet = std::move(packet).At(Timestamp(timestamp));

  // Then std::move it into the input stream.
  return AddPacketToInputStream(stream_name, std::move(packet));
}

::mediapipe::Status Graph::AddPacketToInputStream(
    const std::string& stream_name, const Packet& packet) {
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }

  return running_graph_->AddPacketToInputStream(stream_name, packet);
}

::mediapipe::Status Graph::AddPacketToInputStream(
    const std::string& stream_name, Packet&& packet) {
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }

  return running_graph_->AddPacketToInputStream(stream_name, std::move(packet));
}

::mediapipe::Status Graph::CloseInputStream(std::string stream_name) {
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }
  LOG(INFO) << "Close input stream: " << stream_name;
  return running_graph_->CloseInputStream(stream_name);
}

::mediapipe::Status Graph::CloseAllInputStreams() {
  LOG(INFO) << "Close all input streams.";
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }
  return running_graph_->CloseAllInputStreams();
}

::mediapipe::Status Graph::CloseAllPacketSources() {
  LOG(INFO) << "Close all input streams.";
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }
  return running_graph_->CloseAllPacketSources();
}

::mediapipe::Status Graph::WaitUntilDone(JNIEnv* env) {
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }
  ::mediapipe::Status status = running_graph_->WaitUntilDone();
  running_graph_.reset(nullptr);
  return status;
}

::mediapipe::Status Graph::WaitUntilIdle(JNIEnv* env) {
  if (!running_graph_) {
    return ::mediapipe::FailedPreconditionError("Graph must be running.");
  }
  return running_graph_->WaitUntilIdle();
}

void Graph::SetInputSidePacket(const std::string& stream_name,
                               const Packet& packet) {
  side_packets_[stream_name] = packet;
}

void Graph::SetStreamHeader(const std::string& stream_name,
                            const Packet& packet) {
  stream_headers_[stream_name] = packet;
  LOG(INFO) << stream_name << " stream header being set.";
}

void Graph::SetGraphInputStreamAddMode(
    CalculatorGraph::GraphInputStreamAddMode mode) {
  graph_input_stream_add_mode_ = mode;
}

mediapipe::GpuResources* Graph::GetGpuResources() const {
  return gpu_resources_.get();
}

::mediapipe::Status Graph::SetParentGlContext(int64 java_gl_context) {
  if (gpu_resources_) {
    return ::mediapipe::AlreadyExistsError(
        "trying to set the parent GL context, but the gpu shared "
        "data has already been set up.");
  }
#ifdef MEDIAPIPE_DISABLE_GPU
  LOG(FATAL) << "GPU support has been disabled in this build!";
#else
  gpu_resources_ = mediapipe::GpuResources::Create(
                       reinterpret_cast<EGLContext>(java_gl_context))
                       .ValueOrDie();
#endif  // defined(MEDIAPIPE_DISABLE_GPU)
  return ::mediapipe::OkStatus();
}

void Graph::SetServicePacket(const GraphServiceBase& service, Packet packet) {
  service_packets_[&service] = std::move(packet);
}

void Graph::CancelGraph() {
  if (running_graph_) {
    running_graph_->Cancel();
  }
}

std::map<std::string, Packet> Graph::CreateCombinedSidePackets() {
  std::map<std::string, Packet> combined_side_packets = side_packets_callbacks_;
  combined_side_packets.insert(side_packets_.begin(), side_packets_.end());
  combined_side_packets.insert(output_surface_side_packets_.begin(),
                               output_surface_side_packets_.end());
  return combined_side_packets;
}

ProfilingContext* Graph::GetProfilingContext() {
  if (running_graph_) {
    return running_graph_->profiler();
  }
  return nullptr;
}

}  // namespace android
}  // namespace mediapipe
