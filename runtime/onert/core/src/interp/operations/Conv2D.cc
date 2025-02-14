/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "OperationUtil.h"
#include "../Registration.h"

#include "ir/operation/Conv2D.h"
#include "util/ShapeInference.h"
#include "util/Utils.h"

#include <cker/operation/Conv.h>
#include <misc/polymorphic_downcast.h>

namespace onert
{
namespace interp
{
namespace conv2d
{

void prepareConv2D(ExecEnv *env, const ir::Operation &node)
{
  const auto in_index = node.getInputs().at(ir::operation::Conv2D::INPUT);
  const auto kernel_index = node.getInputs().at(ir::operation::Conv2D::KERNEL);
  const auto bias_index = node.getInputs().at(ir::operation::Conv2D::BIAS);
  const auto out_index = node.getOutputs().at(0);

  const auto in_tensor = env->tensorAt(in_index);
  const auto kernel_tensor = env->tensorAt(kernel_index);
  const auto bias_tensor = env->tensorAt(bias_index);

  assert(in_tensor->getShape().rank() == 4);
  assert(kernel_tensor->getShape().rank() == 4);
  assert(bias_tensor->getShape().rank() == 1);

  UNUSED_RELEASE(in_tensor);
  UNUSED_RELEASE(kernel_tensor);
  UNUSED_RELEASE(bias_tensor);

  const auto output_info = env->graph().operands().at(out_index).info();
  if (output_info.total_size() == 0)
  {
    // Handle unspecified output shape
    const auto &conv_node = nnfw::misc::polymorphic_downcast<const ir::operation::Conv2D &>(node);
    const auto infered_output_shape = shape_inference::inferConv2DShape(
      in_tensor->tensorInfo().shape(), kernel_tensor->tensorInfo().shape(), conv_node.param());
    env->allocateIfNeeded(
      out_index, ir::OperandInfo::createStaticInfo(infered_output_shape, output_info.typeInfo()));
  }
  else
  {
    env->allocateIfNeeded(out_index, output_info);
  }

  auto out_tensor = env->tensorAt(out_index);
  UNUSED_RELEASE(out_tensor);

  // Handle same ifm & ofm data type only
  assert(in_tensor->data_type() == out_tensor->data_type());
  assert(out_tensor->getShape().rank() == 4);
}

void invoke(const ITensor *ifm_tensor, const ITensor *ker_tensor, const ITensor *bias_tensor,
            const ITensor *ofm_tensor, const ir::operation::Conv2D::Param &param)
{
  // TODO Support NCHW frontned
  const auto ifm_shape = ifm_tensor->tensorInfo().shape().asFeature(ir::Layout::NHWC);
  const auto ofm_shape = ofm_tensor->tensorInfo().shape().asFeature(ir::Layout::NHWC);
  // Kernel format is [depth_out, kernel_height, kernel_width, depth_in].
  const auto &ker_shape = ker_tensor->tensorInfo().shape();
  const auto ker_height = ker_shape.dim(1);
  const auto ker_width = ker_shape.dim(2);
  const auto padding =
    ir::calculatePadding(param.padding, ifm_shape, ofm_shape, param.stride, ker_width, ker_height);

  // Calculate
  float activation_min, activation_max;
  calculateActivationRange(param.activation, &activation_min, &activation_max);

  nnfw::cker::ConvParams cker_param;
  cker_param.padding_type = convertPaddingType(param.padding.type);
  cker_param.padding_values.width = padding.left;
  cker_param.padding_values.height = padding.top;
  cker_param.stride_width = param.stride.horizontal;
  cker_param.stride_height = param.stride.vertical;
  cker_param.dilation_width_factor = 1;
  cker_param.dilation_height_factor = 1;
  cker_param.float_activation_min = activation_min;
  cker_param.float_activation_max = activation_max;

  const auto cker_ifm_shape = convertShape(ifm_tensor->tensorInfo().shape());
  const auto cker_ker_shape = convertShape(ker_tensor->tensorInfo().shape());
  const auto cker_bias_shape = convertShape(bias_tensor->tensorInfo().shape());
  const auto cker_ofm_shape = convertShape(ofm_tensor->tensorInfo().shape());
  const float *ifm_ptr = reinterpret_cast<const float *>(ifm_tensor->bufferRO());
  const float *ker_ptr = reinterpret_cast<const float *>(ker_tensor->bufferRO());
  const float *bias_ptr = reinterpret_cast<const float *>(bias_tensor->bufferRO());
  float *ofm_ptr = reinterpret_cast<float *>(ofm_tensor->buffer());

  nnfw::cker::Conv conv_kernel;
  conv_kernel(cker_param, cker_ifm_shape, ifm_ptr, cker_ker_shape, ker_ptr, cker_bias_shape,
              bias_ptr, cker_ofm_shape, ofm_ptr);
}

void invokeConv2D(const ExecEnv *env, const ir::Operation &node)
{
  const auto &conv_node = nnfw::misc::polymorphic_downcast<const ir::operation::Conv2D &>(node);

  const auto ifm_index = node.getInputs().at(ir::operation::Conv2D::INPUT);
  const auto ker_index = node.getInputs().at(ir::operation::Conv2D::KERNEL);
  const auto bias_index = node.getInputs().at(ir::operation::Conv2D::BIAS);
  const auto ofm_index = node.getOutputs().at(0);

  const auto ifm_tensor = env->tensorAt(ifm_index);
  const auto ker_tensor = env->tensorAt(ker_index);
  const auto bias_tensor = env->tensorAt(bias_index);
  const auto ofm_tensor = env->tensorAt(ofm_index);

  const auto data_type = ifm_tensor->data_type();
  if (data_type == ir::DataType::FLOAT32)
  {
    invoke(ifm_tensor, ker_tensor, bias_tensor, ofm_tensor, conv_node.param());
  }
  else
  {
    throw std::runtime_error{"NYI: Support float32 only"};
  }
}
} // namespace conv2d

OpKernel *getConv2D()
{
  static OpKernel kernel = {conv2d::prepareConv2D, conv2d::invokeConv2D};
  return &kernel;
}

} // namespace interp
} // namespace onert
