## Framework Concepts

- [CalculatorBase](#calculatorbase)
- [Life of a Calculator](#life-of-a-calculator)
- [Identifying inputs and outputs](#identifying-inputs-and-outputs)
- [Processing](#processing)
- [GraphConfig](#graphconfig)
- [Subgraph](#subgraph)

Each calculator is a node of a graph. We describe how to create a new
calculator, how to initialize a calculator, how to perform its calculations,
input and output streams, timestamps, and options. Each node in the graph is
implemented as a `Calculator`. The bulk of graph execution happens inside its
calculators. A calculator may receive zero or more input streams and/or side
packets and produces zero or more output streams and/or side packets.

### CalculatorBase

A calculator is created by defining a new sub-class of the
[`CalculatorBase`](http://github.com/google/mediapipe/mediapipe/framework/calculator_base.cc)
class, implementing a number of methods, and registering the new sub-class with
Mediapipe. At a minimum, a new calculator must implement the below four methods

* `GetContract()`
  * Calculator authors can specify the expected types of inputs and outputs of a calculator in GetContract(). When a graph is initialized, the framework calls a static method to verify if the packet types of the connected inputs and outputs match the information in this specification.
* `Open()`
  * After a graph starts, the framework calls `Open()`. The input side packets are available to the calculator at this point. `Open()` interprets the node configuration (see Section \ref{graph_config}) operations and prepares the calculator's per-graph-run state. This function may also write packets to calculator outputs. An error during `Open()` can terminate the graph run.
* `Process()`
  * For a calculator with inputs, the framework calls `Process()` repeatedly whenever at least one input stream has a packet available. The framework by default guarantees that all inputs have the same timestamp (see Section \ref{scheduling} for more information). Multiple `Process()` calls can be invoked simultaneously when parallel execution is enabled. If an error occurs during `Process()`, the framework calls `Close()` and the graph run terminates.
* `Close()`
  * After all calls to `Process()` finish or when all input streams close, the framework calls `Close()`. This function is always called if `Open()` was called and succeeded and even if the graph run terminated because of an error. No inputs are available via any input streams during `Close()`, but it still has access to input side packets and therefore may write outputs. After `Close()` returns, the calculator should be considered a dead node. The calculator object is destroyed as soon as the graph finishes running.

The following are code snippets from
[CalculatorBase.h](http://github.com/google/mediapipe/mediapipe/framework/calculator_base.h).

```c++
class CalculatorBase {
 public:
  ...

  // The subclasses of CalculatorBase must implement GetContract.
  // ...
  static ::MediaPipe::Status GetContract(CalculatorContract* cc);

  // Open is called before any Process() calls, on a freshly constructed
  // calculator.  Subclasses may override this method to perform necessary
  // setup, and possibly output Packets and/or set output streams' headers.
  // ...
  virtual ::MediaPipe::Status Open(CalculatorContext* cc) {
    return ::MediaPipe::OkStatus();
  }

  // Processes the incoming inputs. May call the methods on cc to access
  // inputs and produce outputs.
  // ...
  virtual ::MediaPipe::Status Process(CalculatorContext* cc) = 0;

  // Is called if Open() was called and succeeded.  Is called either
  // immediately after processing is complete or after a graph run has ended
  // (if an error occurred in the graph).  ...
  virtual ::MediaPipe::Status Close(CalculatorContext* cc) {
    return ::MediaPipe::OkStatus();
  }

  ...
};
```
### Life of a calculator

During initialization of a MediaPipe graph, the framework calls a
`GetContract()` static method to determine what kinds of packets are expected.

The framework constructs and destroys the entire calculator for each graph run (e.g. once per video or once per image). Expensive or large objects that remain constant across graph runs should be supplied as input side packets so the calculations are not repeated on subsequent runs.

After initialization, for each run of the graph, the following sequence occurs:

* `Open()`
* `Process()` (repeatedly)
* `Close()`

The framework calls `Open()` to initialize the calculator. `Open()` should interpret any options and set up the calculator's per-graph-run state. `Open()` may obtain input side packets and write packets to calculator outputs. If appropriate, it should call `SetOffset()` to reduce potential packet buffering of input streams.

If an error occurs during `Open()` or `Process()` (as indicated by one of them returning a non-`Ok ` status), the graph run is terminated with no further calls to the calculator's methods, and the calculator is destroyed.

For a calculator with inputs, the framework calls `Process()` whenever at least one input has a packet available. The framework guarantees that inputs all have the same timestamp, that timestamps increase with each call to `Process()` and that all packets are delivered. As a consequence, some inputs may not have any packets when `Process()` is called. An input whose packet is missing appears to produce an empty packet (with no timestamp).

The framework calls `Close()` after all calls to `Process()`. All inputs will have been exhausted, but `Close()` has access to input side packets and may write outputs. After Close returns, the calculator is destroyed.

Calculators with no inputs are referred to as sources. A source calculator continues to have `Process()` called as long as it returns an `Ok` status. A source calculator indicates that it is exhausted by returning a stop status (i.e. MediaPipe::tool::StatusStop).

### Identifying inputs and outputs

The public interface to a calculator consists of a set of input streams and
output streams. In a CalculatorGraphConfiguration, the outputs from some
calculators are connected to the inputs of other calculators using named
streams. Stream names are normally lowercase, while input and output tags are
normally UPPERCASE. In the example below, the output with tag name `VIDEO` is
connected to the input with tag name `VIDEO_IN` using the stream named
`video_stream`.

```proto
# Graph describing calculator SomeAudioVideoCalculator
node {
  calculator: "SomeAudioVideoCalculator"
  input_stream: "INPUT:combined_input"
  output_stream: "VIDEO:video_stream"
}
node {
  calculator: "SomeVideoCalculator"
  input_stream: "VIDEO_IN:video_stream"
  output_stream: "VIDEO_OUT:processed_video"
}
```

Input and output streams can be identified by index number, by tag name, or by a
combination of tag name and index number. You can see some examples of input and
output identifiers in the example below. `SomeAudioVideoCalculator` identifies
its video output by tag and its audio outputs by the combination of tag and
index. The input with tag `VIDEO` is connected to the stream named
`video_stream`. The outputs with tag `AUDIO` and indices `0` and `1` are
connected to the streams named `audio_left` and `audio_right`.
`SomeAudioCalculator` identifies its audio inputs by index only (no tag needed).

```proto
# Graph describing calculator SomeAudioVideoCalculator
node {
  calculator: "SomeAudioVideoCalculator"
  input_stream: "combined_input"
  output_stream: "VIDEO:video_stream"
  output_stream: "AUDIO:0:audio_left"
  output_stream: "AUDIO:1:audio_right"
}

node {
  calculator: "SomeAudioCalculator"
  input_stream: "audio_left"
  input_stream: "audio_right"
  output_stream: "audio_energy"
}
```

In the calculator implementation, inputs and outputs are also identified by tag
name and index number.  In the function below input are output are identified:

*   By index number: The combined input stream is identified simply by index
    `0`.
*   By tag name: The video output stream is identified by tag name "VIDEO".
*   By tag name and index number: The output audio streams are identified by the
    combination of the tag name `AUDIO` and the index numbers `0` and `1`.

```c++
// c++ Code snippet describing the SomeAudioVideoCalculator GetContract() method
class SomeAudioVideoCalculator : public CalculatorBase {
 public:
  static ::mediapipe::Status GetContract(CalculatorContract* cc) {
    cc->Inputs().Index(0).SetAny();
    // SetAny() is used to specify that whatever the type of the
    // stream is, it's acceptable.  This does not mean that any
    // packet is acceptable.  Packets in the stream still have a
    // particular type.  SetAny() has the same effect as explicitly
    // setting the type to be the stream's type.
    cc->Outputs().Tag("VIDEO").Set<ImageFrame>();
    cc->Outputs().Get("AUDIO", 0).Set<Matrix>;
    cc->Outputs().Get("AUDIO", 1).Set<Matrix>;
    return ::mediapipe::OkStatus();
  }
```

### Processing

`Process()` called on a non-source node must return `::mediapipe::OkStatus()` to
indicate that all went well, or any other status code to signal an error

If a non-source calculator returns `tool::StatusStop()`, then this signals the
graph is being cancelled early. In this case, all source calculators and graph
input streams will be closed (and remaining Packets will propagate through the
graph).

A source node in a graph will continue to have `Process()` called on it as long
as it returns `::mediapipe::OkStatus(`). To indicate that there is no more data
to be generated return `tool::StatusStop()`. Any other status indicates an error
has occurred.

`Close()` returns `::mediapipe::OkStatus()` to indicate success. Any other
status indicates a failure.

Here is the basic `Process()` function. It uses the `Input()` method (which can
be used only if the calculator has a single input) to request its input data. It
then uses `std::unique_ptr` to allocate the memory needed for the output packet,
and does the calculations. When done it releases the pointer when adding it to
the output stream.

```c++
::util::Status MyCalculator::Process() {
  const Matrix& input = Input()->Get<Matrix>();
  std::unique_ptr<Matrix> output(new Matrix(input.rows(), input.cols()));
  // do your magic here....
  //    output->row(n) =  ...
  Output()->Add(output.release(), InputTimestamp());
  return ::mediapipe::OkStatus();
}
```

### GraphConfig

A `GraphConfig` is a specification that describes the topology and functionality
of a MediaPipe graph. In the specification, a node in the graph represents an
instance of a particular calculator. All the necessary configurations of the
node, such its type, inputs and outputs must be described in the specification.
Description of the node can also include several optional fields, such as
node-specific options, input policy and executor, discussed in Section
[Framework Concepts > Scheduling mechanics](scheduling_sync.md#scheduling-mechanics).

`GraphConfig` has several other fields to configure the global graph-level
settings, eg, graph executor configs, number of threads, and maximum queue size
of input streams. Several graph-level settings are useful for tuning the
performance of the graph on different platforms (eg, desktop v.s. mobile). For
instance, on mobile, attaching a heavy model-inference calculator to a separate
executor can improve the performance of a real-time application since this
enables thread locality.

Below is a trivial `GraphConfig` example where we have series of passthrough
calculators :

```proto
# This graph named main_pass_throughcals_nosubgraph.pbtxt contains 4
# passthrough calculators.
input_stream: "in"
node {
    calculator: "PassThroughCalculator"
    input_stream: "in"
    output_stream: "out1"
}
node {
    calculator: "PassThroughCalculator"
    input_stream: "out1"
    output_stream: "out2"
}
node {
    calculator: "PassThroughCalculator"
    input_stream: "out2"
    output_stream: "out3"
}
node {
    calculator: "PassThroughCalculator"
    input_stream: "out3"
    output_stream: "out4"
}
```

### Subgraph

To modularize a `CalculatorGraphConfig` into sub-modules and assist with re-use
of perception solutions, a MediaPipe graph can be defined as a `Subgraph`. The
public interface to a subgraph consists of a set of input and output streams
similar to the public interface of a calculator. The subgraph can then be
included in an `CalculatorGraphConfig` as if it were a calculator. When a
MediaPipe graph is loaded from a `CalculatorGraphConfig`, each subgraph node is
replaced by the corresponding graph of calculators. As a result, the semantics
and performance of the subgraph is identical to the corresponding graph of
calculators.

Below is an example of how to create a subgraph named `TwoPassThroughSubgraph`

1. Defining the subgraph.

    ```proto
    # This subgraph is defined in two_pass_through_subgraph.pbtxt
    # that is registered in the BUILD file as "TwoPassThroughSubgraph"
    input_stream: "out1"
    output_stream: "out3"

    node {
        calculator: "PassThroughculator"
        input_stream: "out1"
        output_stream: "out2"
    }
    node {
        calculator: "PassThroughculator"
        input_stream: "out2"
        output_stream: "out3"
    }
    ```

The public interface to the graph that consist of:
   * Graph input streams
   * Graph output streams
   * Graph input side packets
   * Graph output side packets

2. Register the subgraph using BUILD rule `mediapipe_simple_subgraph`
   * The parameter `register_as` defines the component name for the new subgraph

    ```proto
    # Small section of BUILD file for registering the "TwoPassThroughSubgraph"
    # subgraph for use by main graph main_pass_throughcals.pbtxt
    #
    mediapipe_simple_subgraph(
        name = "twopassthrough_subgraph",
        graph = "twopassthrough_subgraph.pbtxt",
        register_as = "TwoPassThroughSubgraph",
        deps = [
                "//mediapipe/calculators/core:pass_through_calculator",
                "//mediapipe/framework:calculator_graph",
        ],
    )
    ```

3. Use the subgraph in the main graph

    ```proto
    # This main graph is defined in main_pass_throughcals.pbtxt
    # using subgraph called "TwoPassThroughSubgraph"
    #
    input_stream: "in"
    node {
        calculator: "PassThroughCalculator"
        input_stream: "in"
        output_stream: "out1"
    }
    node {
        calculator: "TwoPassThroughSubgraph"
        input_stream: "out1"
        output_stream: "out3"
    }
    node {
        calculator: "PassThroughCalculator"
        input_stream: "out3"
        output_stream: "out4"
    }
    ```

<!---
### Graph Templates

A MediaPipe graph template looks exactly like a calculator graph .pbtxt file with some embedded parameters like `%num_detectors%`. When the template parameters are replaced by argument values, the template defines a complete `CalculatorGraphConfig`.

1. Defining and using a graph template by writing a `CalculatorGraphConfig` text protobuf file containing template parameters. The file extension .textpbt stands for "text protobuf template".
    ```proto
    # Test graph with an iteration template directive
    node: {
    name: %name_1%
    calculator: "IntervalFilterCalculator"
    options: {
        [mediapipe.IntervalFilterCalculatorOptions.ext] {
        intervals {
            % for (interval : intervals_1) %
            interval {
            start_us: %interval.begin%
            end_us: %interval.end%
            }
            %end%
        }
        }
    }
    }
    ```
2. Specify values for the template parameters as name-value pairs in a [`TemplateDict protobuf`](http://github.com/mediapipe/framework/tool/calculator_graph_template.proto)
    ```proto
    # Some test template arguments for iteration_test_template.textpbt
    arg: {key: "name_1" value: {str: "hooloo"}}
    arg: {key: "intervals_1" value: {
    element: { dict: {
        arg: {key: "begin" value: {num:33}}
        arg: {key: "end" value: {num:44}}
    }}
    element: { dict: {
        arg: {key: "begin" value: {num:55}}
        arg: {key: "end" value: {num:66}}
    }}
    element: { dict: {
        arg: {key: "begin" value: {num:77}}
        arg: {key: "end" value: {num:88}}
    }}
    }}
    ```
3. Register the subgraph using the build rule: `mediapipe_template_subgraph`.
    ```proto
    mediapipe_template_graph(
        name = "iteration_test_subgraph",
        register_as = "IterationTestTemplateSubgraph",
        template = "iteration_test_template.textpbt",
        deps = [
            "//mediapipe/core:interval_filter_calculator",
        ],
    )
    ```
    Alternatively, the template and the parameter values can be combined using the build rule: `mediapipe_template_graph`.
    ```proto
    mediapipe_template_graph(
        name = "iteration_test_2_graph",
        arg_file = "iteration_test_arg.pbtxt",
        template = "iteration_test_template.textpbt",
    )
    ```
4. The result is a complete CalculatorGraphConfig protobuf, such as the following:
    ```proto
    node {
    name: "hooloo"
    calculator: "IntervalFilterCalculator"
    options {
        [mediapipe.IntervalFilterCalculatorOptions.ext] {
        intervals {
            interval {
            start_us: 33
            end_us: 44
            }
            interval {
            start_us: 55
            end_us: 66
            }
            interval {
            start_us: 77
            end_us: 88
            }
        }
        }
    }
    }
    ```
Graph template parameters
* For a template parameter, you can specify a simple parameter name such as `%end_time%`
```proto
interval {
  start_us: 10
  end_us: %end_time%
}
```
or a more complex expression, such as `% begin_time + duration %`
```proto
interval {
  start_us: %begin_time%
  end_us: % begin_time + duration %
}
```
--->
