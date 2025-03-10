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

#ifndef MEDIAPIPE_FRAMEWORK_CALCULATOR_CONTRACT_H_
#define MEDIAPIPE_FRAMEWORK_CALCULATOR_CONTRACT_H_

#include <map>
#include <memory>
#include <string>
#include <typeindex>

// TODO: Move protos in another CL after the C++ code migration.
#include "mediapipe/framework/calculator.pb.h"
#include "mediapipe/framework/graph_service.h"
#include "mediapipe/framework/mediapipe_options.pb.h"
#include "mediapipe/framework/packet_generator.pb.h"
#include "mediapipe/framework/packet_type.h"
#include "mediapipe/framework/port.h"
#include "mediapipe/framework/port/any_proto.h"
#include "mediapipe/framework/status_handler.pb.h"
#include "mediapipe/framework/tool/options_util.h"

namespace mediapipe {

// CalculatorContract contains the expectations and properties of a Node
// object, such as the expected packet types of input and output streams and
// input and output side packets.
//
// Setters and getters are available for specifying an InputStreamHandler and
// it's options from inside a calculator's GetContract() method. Ex:
//  cc->SetInputStreamHandler("FixedSizeInputStreamHandler");
//  MediaPipeOptions options;
//  options.MutableExtension(FixedSizeInputStreamHandlerOptions::ext)
//      ->set_fixed_min_size(2);
//  cc->SetInputStreamHandlerOptions(options);
//
class CalculatorContract {
 public:
  ::mediapipe::Status Initialize(const CalculatorGraphConfig::Node& node);
  ::mediapipe::Status Initialize(const PacketGeneratorConfig& node);
  ::mediapipe::Status Initialize(const StatusHandlerConfig& node);

  // Returns the options given to this node.
  const CalculatorOptions& Options() const { return node_config_->options(); }

  // Returns the options given to this calculator.  Template argument T must
  // be the type of the protobuf extension message or the protobuf::Any
  // message containing the options.
  template <class T>
  const T& Options() const {
    return options_.Get<T>();
  }

  // Returns the PacketTypeSet for the input streams.
  PacketTypeSet& Inputs() { return *inputs_; }
  const PacketTypeSet& Inputs() const { return *inputs_; }

  // Returns the PacketTypeSet for the output streams.
  PacketTypeSet& Outputs() { return *outputs_; }
  const PacketTypeSet& Outputs() const { return *outputs_; }

  // Returns the PacketTypeSet for the input side packets.
  PacketTypeSet& InputSidePackets() { return *input_side_packets_; }
  const PacketTypeSet& InputSidePackets() const { return *input_side_packets_; }

  // Returns the PacketTypeSet for the output side packets.
  PacketTypeSet& OutputSidePackets() { return *output_side_packets_; }
  const PacketTypeSet& OutputSidePackets() const {
    return *output_side_packets_;
  }

  // Set this Node's default InputStreamHandler.
  // If there is an InputStreamHandler specified in the graph (.pbtxt) for this
  // Node, then the graph's InputStreamHandler will take priority.
  void SetInputStreamHandler(const std::string& name) {
    input_stream_handler_ = name;
  }
  void SetInputStreamHandlerOptions(const MediaPipeOptions& options) {
    input_stream_handler_options_ = options;
  }

  // Returns the name of this Nodes's InputStreamHandler, or empty std::string
  // if none is set.
  std::string GetInputStreamHandler() const { return input_stream_handler_; }

  // Returns the MediaPipeOptions of this Node's InputStreamHandler, or empty
  // options if none is set.
  MediaPipeOptions GetInputStreamHandlerOptions() const {
    return input_stream_handler_options_;
  }

  class GraphServiceRequest {
   public:
    // APIs that should be used by calculators.
    GraphServiceRequest& Optional() {
      optional_ = true;
      return *this;
    }

    // Internal use.
    GraphServiceRequest(const GraphServiceBase& service) : service_(service) {}

    const GraphServiceBase& Service() const { return service_; }

    bool IsOptional() const { return optional_; }

   private:
    GraphServiceBase service_;
    bool optional_ = false;
  };

  GraphServiceRequest& UseService(const GraphServiceBase& service) {
    auto it = service_requests_.emplace(service.key, service).first;
    return it->second;
  }

  const std::map<std::string, GraphServiceRequest>& ServiceRequests() const {
    return service_requests_;
  }

 private:
  template <class T>
  void GetNodeOptions(T* result) const;

  const CalculatorGraphConfig::Node* node_config_ = nullptr;
  tool::OptionsMap options_;
  std::unique_ptr<PacketTypeSet> inputs_;
  std::unique_ptr<PacketTypeSet> outputs_;
  std::unique_ptr<PacketTypeSet> input_side_packets_;
  std::unique_ptr<PacketTypeSet> output_side_packets_;
  std::string input_stream_handler_;
  MediaPipeOptions input_stream_handler_options_;
  std::map<std::string, GraphServiceRequest> service_requests_;
};

}  // namespace mediapipe

#endif  // MEDIAPIPE_FRAMEWORK_CALCULATOR_CONTRACT_H_
