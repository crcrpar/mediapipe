# Copyright 2019 The MediaPipe Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

licenses(["notice"])  # Apache 2.0

package(default_visibility = ["//visibility:private"])

cc_binary(
    name = "libmediapipe_jni.so",
    linkshared = 1,
    linkstatic = 1,
    deps = [
        "//mediapipe/graphs/edge_detection:android_calculators",
        "//mediapipe/java/com/google/mediapipe/framework/jni:mediapipe_framework_jni",
    ],
)

cc_library(
    name = "mediapipe_jni_lib",
    srcs = [":libmediapipe_jni.so"],
    alwayslink = 1,
)

# Maps the binary graph to an alias (e.g., the app name) for convenience so that the alias can be
# easily incorporated into the app via, for example,
# MainActivity.BINARY_GRAPH_NAME = "appname.binarypb".
genrule(
    name = "binary_graph",
    srcs = ["//mediapipe/graphs/edge_detection:android_gpu_binary_graph"],
    outs = ["edgedetectiongpu.binarypb"],
    cmd = "cp $< $@",
)

android_library(
    name = "mediapipe_lib",
    srcs = glob(["*.java"]),
    assets = [
        ":binary_graph",
    ],
    assets_dir = "",
    manifest = "AndroidManifest.xml",
    resource_files = glob(["res/**"]),
    deps = [
        ":mediapipe_jni_lib",
        "//mediapipe/java/com/google/mediapipe/components:android_camerax_helper",
        "//mediapipe/java/com/google/mediapipe/components:android_components",
        "//mediapipe/java/com/google/mediapipe/framework:android_framework",
        "//mediapipe/java/com/google/mediapipe/glutil",
        "//third_party:android_constraint_layout",
        "//third_party:androidx_appcompat",
        "//third_party:opencv",
        "@androidx_concurrent_futures//jar",
        "@com_google_guava_android//jar",
    ],
)

android_binary(
    name = "edgedetectiongpu",
    aapt_version = "aapt2",
    manifest = "AndroidManifest.xml",
    manifest_values = {"applicationId": "com.google.mediapipe.apps.edgedetectiongpu"},
    multidex = "native",
    deps = [
        ":mediapipe_lib",
    ],
)
