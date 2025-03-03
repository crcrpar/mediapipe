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

#include "mediapipe/framework/legacy_calculator_support.h"

namespace mediapipe {

// We only define this variable for two specializations of the template
// because it is only meant to be used for these two types.
#if EMSCRIPTEN_WORKAROUND_FOR_B121216479
template <>
CalculatorContext*
    LegacyCalculatorSupport::Scoped<CalculatorContext>::current_ = nullptr;
template <>
CalculatorContract*
    LegacyCalculatorSupport::Scoped<CalculatorContract>::current_ = nullptr;
#else
template <>
thread_local CalculatorContext*
    LegacyCalculatorSupport::Scoped<CalculatorContext>::current_ = nullptr;
template <>
thread_local CalculatorContract*
    LegacyCalculatorSupport::Scoped<CalculatorContract>::current_ = nullptr;
#endif  // EMSCRIPTEN_WORKAROUND_FOR_B121216479

}  // namespace mediapipe
