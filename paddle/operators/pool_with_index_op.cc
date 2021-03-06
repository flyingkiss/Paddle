/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/operators/pool_with_index_op.h"

namespace paddle {
namespace operators {

inline int OutputSizeMaxPool(int input_size, int filter_size, int padding,
                             int stride) {
  int output_size = (input_size - filter_size + 2 * padding) / stride + 1;
  return output_size;
}

class MaxPoolWithIndexOp : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

  void InferShape(framework::InferShapeContext *ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("X"),
                   "X(Input) of Pooling should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("Out"),
                   "Out(Output) of Pooling should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("Mask"),
                   "Mask(Output) of Pooling should not be null.");

    auto in_x_dims = ctx->GetInputDim("X");

    std::vector<int> ksize = ctx->Attrs().Get<std::vector<int>>("ksize");
    std::vector<int> strides = ctx->Attrs().Get<std::vector<int>>("strides");
    std::vector<int> paddings = ctx->Attrs().Get<std::vector<int>>("paddings");

    PADDLE_ENFORCE(in_x_dims.size() == 4 || in_x_dims.size() == 5,
                   "Pooling intput should be 4-D or 5-D tensor.");

    if (ctx->Attrs().Get<bool>("globalPooling")) {
      ksize.resize(static_cast<size_t>(in_x_dims.size()) - 2);
      for (size_t i = 0; i < ksize.size(); ++i) {
        paddings[i] = 0;
        ksize[i] = static_cast<int>(in_x_dims[i + 2]);
      }
    }

    PADDLE_ENFORCE(in_x_dims.size() - ksize.size() == 2U,
                   "Input size and pooling size should be consistent.");
    PADDLE_ENFORCE_EQ(ksize.size(), strides.size(),
                      "Strides size and pooling size should be the same.");
    PADDLE_ENFORCE_EQ(ksize.size(), paddings.size(),
                      "Paddings size and pooling size should be the same.");

    std::vector<int64_t> output_shape({in_x_dims[0], in_x_dims[1]});
    for (size_t i = 0; i < ksize.size(); ++i) {
      output_shape.push_back(OutputSizeMaxPool(in_x_dims[i + 2], ksize[i],
                                               paddings[i], strides[i]));
    }
    ctx->SetOutputDim("Out", framework::make_ddim(output_shape));
    ctx->SetOutputDim("Mask", framework::make_ddim(output_shape));
  }
};

class MaxPoolWithIndexOpGrad : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

  void InferShape(framework::InferShapeContext *ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("Mask"), "Input(Mask) must not be null.");
    PADDLE_ENFORCE(ctx->HasInput("X"), "Input(X) must not be null.");
    PADDLE_ENFORCE(ctx->HasOutput(framework::GradVarName("X")),
                   "Input(X@GRAD) should not be null.");
    ctx->SetOutputDim(framework::GradVarName("X"), ctx->GetInputDim("X"));
  }
};

class MaxPool2dWithIndexOpMaker : public framework::OpProtoAndCheckerMaker {
 public:
  MaxPool2dWithIndexOpMaker(framework::OpProto *proto,
                            framework::OpAttrChecker *op_checker)
      : OpProtoAndCheckerMaker(proto, op_checker) {
    AddInput(
        "X",
        "(Tensor) The input tensor of pooling operator. "
        "The format of input tensor is NCHW, where N is batch size, C is the "
        "number of channels, H is the height of the image, "
        "and W is the width of the image.");
    AddOutput("Out",
              "(Tensor) The output tensor of pooling operator. "
              "The format of output tensor is also NCHW, "
              "where N is batch size, C is "
              "the number of channels, H is the height of the image "
              "and W is the width of the image.");
    AddOutput("Mask",
              "(Tensor) The Mask tensor of pooling operator."
              "The format of output tensor is also NCHW, "
              "where N is batch size, C is the number of channels, "
              "H is the height of the image, "
              "and W is the width of the image. "
              "It represents the index in the current feature map.");

    AddAttr<std::vector<int>>("ksize",
                              "(vector<int>) The pooling window size(height, "
                              "width) of pooling operator. "
                              "If globalPooling = true, ksize and paddings "
                              "will be ignored.");  // TODO(Chengduo): Add
                                                    // checker. (Currently,
    // TypedAttrChecker don't support vector type.)
    AddAttr<bool>(
        "globalPooling",
        "(bool, default false) Whether to use the global pooling. "
        "If globalPooling = true, ksize and paddings will be ignored.")
        .SetDefault(false);
    AddAttr<std::vector<int>>("strides",
                              "(vector<int>, default {1, 1}), strides(height, "
                              "width) of pooling operator.")
        .SetDefault({1, 1});  // TODO(Chengduo): Add checker. (Currently,
    // TypedAttrChecker don't support vector type.)
    AddAttr<std::vector<int>>(
        "paddings",
        "(vector<int>, defalut {0, 0}), paddings(height, width) of pooling "
        "operator. "
        "If globalPooling = true, paddings and will be ignored.")
        .SetDefault({0, 0});  // TODO(Chengduo): Add checker. (Currently,
    // TypedAttrChecker don't support vector type.)

    AddComment(R"DOC(
MaxPool2d Operator.

The maxPooling2d with index operation calculates the output and the mask
based on the input, ksize, strides, and paddings parameters. Input(X) and
output(Out, Mask) are in NCHW format, where N is batch size, C is the
number of channels, H is the height of the feature, 
and W is the width of the feature.
Parameters(ksize, strides, paddings) are two elements.
These two elements represent height and width, respectively.
The input(X) size and output(Out, Mask) size may be different.

Example:
  Input:
       X shape: $(N, C, H_{in}, W_{in})$
  Output:
       Out shape: $(N, C, H_{out}, W_{out})$
       Mask shape: $(N, C, H_{out}, W_{out})$
  where
       $$
       H_{out} = (H_{in} - ksize[0] + 2 * paddings[0]) / strides[0] + 1 \\
       W_{out} = (W_{in} - ksize[1] + 2 * paddings[1]) / strides[1] + 1
       $$

)DOC");
  }
};

class MaxPool3dWithIndexOpMaker : public framework::OpProtoAndCheckerMaker {
 public:
  MaxPool3dWithIndexOpMaker(framework::OpProto *proto,
                            framework::OpAttrChecker *op_checker)
      : OpProtoAndCheckerMaker(proto, op_checker) {
    AddInput("X",
             "(Tensor) The input tensor of pooling operator. "
             "The format of input tensor is NCDHW, where N is batch size, C is "
             "the number of channels, and D, H and W are the depth, height and "
             "width of "
             "the image, respectively");
    AddOutput("Out",
              "(Tensor) The output tensor of pooling operator. "
              "The format of output tensor is also NCDHW, "
              "where N is the batch size, C is the number of channels, "
              "and D, H and W are the depth, height and "
              "width of the image, respectively.");
    AddOutput("Mask",
              "(Tensor) The Mask tensor of pooling operator. "
              "The format of output tensor is also NCDHW, "
              "where N is the batch size, C is the number of channels, and "
              "D, H and W are the depth, height and width "
              "of the image, respectively. "
              "It represents the index in the current feature map.");

    AddAttr<std::vector<int>>("ksize",
                              "(vector<int>) The pooling window size(depth, "
                              "height, width) of pooling operator. "
                              "If globalPooling = true, ksize and paddings "
                              "will be ignored.");  // TODO(Chengduo): Add
                                                    // checker. (Currently,
    // TypedAttrChecker don't support vector type.)
    AddAttr<bool>(
        "globalPooling",
        "(bool, default false) Whether to use the global pooling. "
        "If globalPooling = true, ksize and paddings will be ignored.")
        .SetDefault(false);
    AddAttr<std::vector<int>>("strides",
                              "(vector<int>, default {1,1,1}), strides(depth, "
                              "height, width) of pooling operator.")
        .SetDefault({1, 1, 1});  // TODO(Chengduo): Add checker. (Currently,
    // TypedAttrChecker don't support vector type.)
    AddAttr<std::vector<int>>(
        "paddings",
        "(vector, defalut {0,0,0}), paddings(depth, "
        "height, width) of pooling operator. "
        "If globalPooling = true, paddings and ksize will be ignored.")
        .SetDefault({0, 0, 0});  // TODO(Chengduo): Add checker. (Currently,
    // TypedAttrChecker don't support vector type.)

    AddComment(R"DOC(
MaxPool3d Operator.

The maxpooling3d with index operation calculates the output and the mask
based on the input and ksize, strides, paddings parameters.
Input(X) and output(Out, Mask) are in NCDHW format, where N is batch
size, C is the number of channels, and D, H and W are the depth, height and
width of the feature, respectively. 
Parameters(ksize, strides, paddings) are three elements.
These three elements represent depth, height and width, respectively.
The input(X) size and output(Out, Mask) size may be different.

Example:
  Input:
       X shape: $(N, C, D_{in}, H_{in}, W_{in})$
  Output:
       Out shape: $(N, C, D_{out}, H_{out}, W_{out})$
       Mask shape: $(N, C, D_{out}, H_{out}, W_{out})$
  where
       $$
       D_{out} = (D_{in} - ksize[0] + 2 * paddings[0]) / strides[0] + 1 \\
       H_{out} = (H_{in} - ksize[1] + 2 * paddings[1]) / strides[1] + 1 \\
       W_{out} = (W_{in} - ksize[2] + 2 * paddings[2]) / strides[2] + 1
       $$

)DOC");
  }
};

}  // namespace operators
}  // namespace paddle

namespace ops = paddle::operators;

REGISTER_OP(max_pool2d_with_index, ops::MaxPoolWithIndexOp,
            ops::MaxPool2dWithIndexOpMaker, max_pool2d_with_index_grad,
            ops::MaxPoolWithIndexOpGrad);

REGISTER_OP_CPU_KERNEL(
    max_pool2d_with_index,
    ops::MaxPoolWithIndexKernel<paddle::platform::CPUPlace, float>);
REGISTER_OP_CPU_KERNEL(
    max_pool2d_with_index_grad,
    ops::MaxPoolWithIndexGradKernel<paddle::platform::CPUPlace, float>)

REGISTER_OP(max_pool3d_with_index, ops::MaxPoolWithIndexOp,
            ops::MaxPool3dWithIndexOpMaker, max_pool3d_with_index_grad,
            ops::MaxPoolWithIndexOpGrad);

REGISTER_OP_CPU_KERNEL(
    max_pool3d_with_index,
    ops::MaxPoolWithIndexKernel<paddle::platform::CPUPlace, float>);
REGISTER_OP_CPU_KERNEL(
    max_pool3d_with_index_grad,
    ops::MaxPoolWithIndexGradKernel<paddle::platform::CPUPlace, float>)
