// Copyright (c) Facebook Inc. and Microsoft Corporation.
// Licensed under the MIT license.

#include "onnx/defs/schema.h"
namespace ONNX_NAMESPACE {
using SupportType = OpSchema::SupportType;

ONNX_OPERATOR_SET_SCHEMA(
    If,
    1,
    OpSchema()
        .SetDoc("If conditional")
        .Input(0, "cond", "Condition for the if", "B")
        .Output(
            0,
            "outputs",
            "Values that are live-out to the enclosing scope. The return values in "
            "the `then_branch` and `else_branch` must be of the same shape and same "
            "data type.",
            "V",
            OpSchema::Variadic)
        .Attr(
            "then_branch",
            "Graph to run if condition is true. Has N outputs: values you wish to "
            "be live-out to the enclosing scope. The number of outputs must match"
            " the number of outputs in the else_branch.",
            AttributeProto::GRAPH)
        .Attr(
            "else_branch",
            "Graph to run if condition is false. Has N outputs: values you wish to"
            " be live-out to the enclosing scope. The number of outputs must match"
            " the number of outputs in the then_branch.",
            AttributeProto::GRAPH)
        .TypeConstraint("V", OpSchema::all_tensor_types(), "All Tensor types")
        .TypeConstraint("B", {"tensor(bool)"}, "Only bool"));

static const char* Loop_ver1_doc = R"DOC(
Generic Looping construct. This loop has multiple termination conditions:

1) Trip count. Iteration count specified at runtime. Set by
   specifying the input M. Optional. Set to empty string to omit.
   Note that a static trip count (specified at graph construction time) can be
   specified by passing in a constant node for input M.
2) Loop termination condition. This is an input to the op that determines
   whether to run the first iteration and also a loop-carried dependency for
   the body graph. The body graph must yield a value for the condition variable,
   whether this input is provided or not.

This table summarizes the operating modes of this operator with equivalent
C-style code:

    Operator inputs defined as (max_trip_count, condition_var).

    input ("", ""):
        for (int i=0; ; ++i) {
          cond = ... // Note this value is ignored, but is required in the body
        }

    input ("", cond) // Note this is analogous to a while loop
        bool cond = ...;
        for (int i=0; cond; ++i) {
          cond = ...;
        }

    input ("", 1) // Note this is analogous to a do-while loop
        bool cond = true
        for (int i=0; cond; ++i) {
          cond = ...;
        }

    input (trip_count, "") // Note this is analogous to a for loop
        int trip_count = ...
        for (int i=0; i < trip_count; ++i) {
          cond = ...; // ignored
        }

    input (trip_count, cond)
        int trip_count = ...;
        bool cond = ...;
        for (int i=0; i < trip_count && cond; ++i) {
          cond = ...;
        }


*Sample usage - cond as well as trip count*

    graph predict-net {
      %a = Constant[value = <Scalar Tensor [3]>]()
      %b = Constant[value = <Scalar Tensor [6]>]()
      %keepgoing = Constant[value = <Scalar Tensor [1]>]()
      %max_trip_count = Constant[value = <Scalar Tensor [10]>]()
      %keepgoing_out, %b_out, %user_defined_vals = Loop[body = <graph body-net>](%max_trip_count, %keepgoing, %b)
      return
    }

    graph body-net (
      %i[INT32, scalar]
      %keepgoing[BOOL, scalar]
      %b[INT32, scalar]
    ) {
      %my_local = Add(%a, %b)
      %b_out = Sub(%a, %b)
      %keepgoing_out = Greater(%my_local, %b_out)
      %user_defined_vals = Add(%b, %b)
      return %keepgoing_out, %b_out, %user_defined_vals
    }

*Sample equivalent C code*

    {
      /* User-defined code (enclosing scope) */
      int a = 3, b = 6;
      bool keepgoing = true; // Analogous to input cond
      /* End user-defined code */

      /* Implicitly-defined code */
      const int max_trip_count = 10; // Analogous to input M
      int user_defined_vals[]; // Imagine this is resizable
      /* End implicitly-defined code */
      for (int i=0; i < max_trip_count && keepgoing; ++i) {
        /* User-defined code (loop body) */
        int my_local = a + b; // Reading values in the enclosing scope is fine
        b = a - b; // writes fine if we specify b as a loop-carried dependency
        keepgoing = my_local > b; // keepgoing is a loop-carried dependency
        user_defined_vals[i] = b + b;
        /* End user-defined code */
      }
      // my_local = 123; // Can't do this. my_local was defined in the the body

      // These below values are live-out from the loop and therefore accessible
      b_out; user_defined_vals; keepgoing_out;
    }

There are several things of note in this code snippet:

1) Values from the enclosing scope (i.e. variable a here) are in scope and can
   be referenced in the inputs of the loop.
2) Any variables which you wish to make available in the enclosing scope (i.e.
   the variables b and keepgoing) must be declared as either loop-carried
   dependencies (both at the op inputs and output and at the body net input and
   output) or scan_outputs.
3) Values created in the body cannot be accessed in the enclosing scope.

Note that the semantics of this op support "diagonal" or "wavefront" execution.
(See Step 3 here for an example:
https://devblogs.nvidia.com/optimizing-recurrent-neural-networks-cudnn-5/).
Frontends should emit multi-layer RNNs as a series of While operators (with
time being the inner looping dimension), with each successive layer consuming
the scan_outputs from the previous layer, possibly going through several
point-wise operators (e.g. dropout, residual connections, linear layer).
)DOC";

ONNX_OPERATOR_SET_SCHEMA(
    Loop,
    1,
    OpSchema()
        .SetDoc(Loop_ver1_doc)
        .Input(
            0,
            "M",
            "A maximum trip-count for the loop specified at runtime. Optional."
            " pass empty string to skip.",
            "I")
        .Input(
            1,
            "cond",
            "A boolean termination condition. Pass empty string to skip.",
            "B")
        .Input(
            2,
            "v_initial",
            "The initial values of any loop-carried dependencies (values that "
            "change across loop iterations)",
            "V",
            OpSchema::Variadic)
        .Output(
            0,
            "v_final_and_scan_outputs",
            "Final N loop carried dependency values then K scan_outputs",
            "V",
            OpSchema::Variadic)
        .Attr(
            "body",
            "The graph run each iteration. It has 2+N inputs: (iteration_num, "
            "condition, loop carried dependencies...). It has 1+N+K outputs: "
            "(condition, loop carried dependencies..., scan_outputs...). Each "
            "scan_output is created by concatenating the value of the specified "
            "output value at the end of each iteration of the loop. It is an error"
            " if the dimensions or data type of these scan_outputs change across loop"
            " iterations.",
            AttributeProto::GRAPH)
        .TypeConstraint("V", OpSchema::all_tensor_types(), "All Tensor types")
        .TypeConstraint("I", {"int64"}, "Only int64")
        .TypeConstraint("B", {"bool"}, "Only bool"));

static const char* scan_ver1_doc = R"DOC(
Scan can be used to iterate over (specified axes of) one or more scan_input tensors,
constructing zero or more scan_output tensors. It combines ideas from general recurrences,
functional programming constructs such as scan, fold, map, and zip and is intended to enable
generalizations of RNN-like constructs for sequence-to-sequence processing.
Other tensors (referred to as state_variables here) can be used to carry a state
when iterating from one element to another (similar to hidden-state in RNNs, also referred
to as loop-carried dependences in the context of loops). All these tensors are required to
have the same shape in each iteration of the loop (a restriction imposed to enable efficient
memory allocation). Many common usages involve a single scan_input tensor (where functionality
similar to scan, fold and map can be obtained). When more than one scan_input is used,
a behavior similar to zip is obtained.

The attribute body must be a graph, specifying the computation to be performed in
every iteration. It takes as input the current values of the state_variables and
the current iterated element of the scan_inputs. It must return the (updated) values
of the state_variables and zero or more scan_output_element tensors. The values of the
scan_output_element tensors are concatenated over all the iterations to produce the
scan_output values of the scan construct (similar to the concatenated intermediate
hidden-state values of RNN-like constructs).

The scan operation returns the final values of the state_variables as well as the
scan_outputs.

The operation supports batching, and the batch-axis is required to be 0.
When multiple scan_input tensors are used, they must all have the same batch-size,
and they must all have the same maximum-sequence-length (the dimensionality of the
sequence axis or scan axis). The sequence axis or scan axis is required to be 1.

The operation has an optional sequence_lens input (of shape [BATCH_SIZE]) to
allow variable length sequences of length <= the maximum-sequence-length. If this
input is not specified, all sequences are assumed to be of length equal to
maximum-sequence-length. For variable length input sequences, the scan_outputs
will consist of a sequence of same length as the input, padded to the
maximum-sequence-length.

The optional attribute directions can be used to scan a sequence in the reverse direction.
If this attribute is omitted, all sequences are scanned in the forward direction.
A bidirectional scan be performed by specifying the same tensor input twice in the
scan_inputs, once with a forward direction, and once with a backward direction.

Note that because of the ONNX restriction that only the last parameter of an operator can
be variadic, the initial-states and scan-inputs are listed together as one input parameter.
Similarly, the final-states and scan-outputs are listed together as one output parameter.
The attribute num_scan_inputs indicates the number M of scan-inputs.

The behavior of

    Scan <
        num_scan_inputs = m,
        body = loop-body
    > (sequence_lengths, init_1, ..., init_n, scan_1, ..., scan_m)

is equivalent to the following pseudo-code:

    // T.shape[0] denotes the batch-size of T
    // The batch-size of scan_1, ..., scan_m are all required to be equal
    batch_size = scan_1.shape[0];

    // scan_i.shape[1] denotes the (max) sequence-length of scan_i
    // scan_i.shape[1] is required to be equal to scan_j.shape[1] for all i,j.
    max_sequence_length = scan_1.shape[1];

    for (int batch = 0; batch < batch_size; ++batch) {
        // initialize state-variables
        st_1 = init_1; ... st_n = init_n;
        // initialize scan-output variables: [] denotes an empty tensor
        scan_out_1 = []; ...; scan_out_k = [];
        // identify number of iterations:
        N = (sequence_lengths specified) ? sequence_lengths[batch] : max_sequence_length;

        // execute loop
        for (int t = 0; t < N; ++t) {
            // generate the scan-input elements: the notation T<axis=k>[t] indicates the sub-tensor
            // of rank one less than T obtained by indexing T at position t along axis k.
            si_1 = (scan_1<axis=0>[batch])<axis=1>[t];
            ... ;
            si_m = (scan_m<axis=0>[batch])<axis=1>[t];
            // execute loop-body
            st_1, ..., st_n, so_1, ..., so_k = loop-body(st_1, ..., st_n, si_1, ..., si_m)
            // accumulate the scan-output elements
            scan_out_1 = Concat<axis=0>(scan_out_1, so_1); ... ; scan_out_k = Concat<axis=0>(scan_out_k, so_k);
        }
        // accumulate the outputs for this batch:
        bst_1[batch] = st_1; ..., bst_n[batch] = st_n;
        // Note scan-outputs will have size max_sequence_length, but only first N values will be meaningful.
        // The remaining values have an undefined value.
        b_scan_out_1[batch] = scan_out_1; ...; b_scan_out_k[batch] = scan_out_k;
    }
    return bst_1, ..., bst_n, b_scan_out_1, ..., b_scan_out_k;



*Sample usage: Encoding RNN using a Scan*
The following example shows how a simple RNN over an input tensor %X, with weight tensor %Wi,
recurrence weight tensor %Ri, bias tensors %Wbi and %Rbi, and initial hidden-state %H_0 can
be encoded as a ScanLoop. Note that the loop-body is a nested graph, and it directly computes
%Wi, %Ri, %Wbi, and %Rbi (typically constants or initializers in the body graph). If these
values are computed in the outer graph, they need to be passed in as extra state_variables.

    graph rnn-encoding {
      %H_0 = ... 
      %X = ...
      %Y_h, %Y = Scan[body = <graph rnn-cell-1>, num_scan_inputs=1]("", %H_0, %X)
      return %Y, %Y_h
    }

    graph rnn-cell-1 (
      %H_tminus1[FLOAT, tensor]
      %X_t[FLOAT, tensor]
    ) {
      %Wi = ...
      %Ri = ...
      %Wbi = ...
      %Rbi = ...
      %t1 = X_t * (Wi^T)
      %t2 = H_tminus1*(Ri^T)
      %t3 = Add(%t1, %t2)
      %t4 = Add(%t3, %Wbi)
      %t5 = Add(%t4, %Rbi)
      %Ht = Tanh(%t5)
      %Accumulate = Identity(%Ht)
      return %Ht, %Accumulate
    }

)DOC";

ONNX_OPERATOR_SET_SCHEMA(
	Scan,
	8,
	OpSchema()
	.SetDoc(scan_ver1_doc)
	.Input(
		0,
		"sequence_lens",
		"Optional tensor specifying lengths of the sequences in a batch. "
		"If this input is not specified, all sequences are assumed to be of "
		"the maximum sequence length (the dimension of the sequence axis of "
		"the scan_input tensors).",
		"I",
		OpSchema::Optional)
	.Input(
		1,
		"initial_state_and_scan_inputs",
		"Initial values of the loop's N state variables followed by M scan_inputs",
		"V",
		OpSchema::Variadic)
	.Output(
		0,
		"final_state_and_scan_outputs",
		"Final values of the loop's N state variables followed by K scan_outputs",
		"V",
		OpSchema::Variadic)
	.Attr(
		"body",
		"The graph run each iteration. It has N+M inputs: "
		"(loop state variables..., scan_input_elts...). It has N+K outputs: "
		"(loop state variables..., scan_output_elts...). Each "
		"scan_output is created by concatenating the value of the specified "
		"scan_output_elt value at the end of each iteration of the loop. It is an error"
		" if the dimensions of these values change across loop iterations.",
		AttributeProto::GRAPH,
		true)
	.Attr(
		"num_scan_inputs",
		"An attribute specifying the number of scan_inputs M. ",
		AttributeProto::INT,
		true)
	.Attr(
		"directions",
		"An optional list of M flags. The i-th element of the list specifies the direction "
		"to be scanned for the i-th scan_input tensor: 0 indicates forward direction and 1 "
		"indicates reverse direction. "
		"If omitted, all scan_input tensors will be scanned in the forward direction.",
		AttributeProto::INTS,
		false)
	.TypeConstraint("I", { "tensor(int64)" }, "Int64 tensor")
	.TypeConstraint("V", OpSchema::all_tensor_types(), "All Tensor types"));

} // namespace ONNX_NAMESPACE
