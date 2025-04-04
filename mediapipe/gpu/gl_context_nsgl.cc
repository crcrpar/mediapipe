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

#include <utility>

#include "absl/memory/memory.h"
#include "mediapipe/framework/port/logging.h"
#include "mediapipe/framework/port/ret_check.h"
#include "mediapipe/framework/port/status.h"
#include "mediapipe/framework/port/status_builder.h"
#include "mediapipe/gpu/gl_context.h"
#include "mediapipe/gpu/gl_context_internal.h"

#if HAS_NSGL

namespace mediapipe {

GlContext::StatusOrGlContext GlContext::Create(std::nullptr_t nullp,
                                               bool create_thread) {
  return Create(static_cast<NSOpenGLContext*>(nil), create_thread);
}

GlContext::StatusOrGlContext GlContext::Create(const GlContext& share_context,
                                               bool create_thread) {
  return Create(share_context.context_, create_thread);
}

GlContext::StatusOrGlContext GlContext::Create(NSOpenGLContext* share_context,
                                               bool create_thread) {
  std::shared_ptr<GlContext> context(new GlContext());
  RETURN_IF_ERROR(context->CreateContext(share_context));
  RETURN_IF_ERROR(context->FinishInitialization(create_thread));
  return std::move(context);
}

::mediapipe::Status GlContext::CreateContext(NSOpenGLContext* share_context) {
  // TODO: choose a better list?
  NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAAccelerated,
                                          NSOpenGLPFAColorSize,
                                          24,
                                          NSOpenGLPFAAlphaSize,
                                          8,
                                          NSOpenGLPFADepthSize,
                                          16,
                                          0};

  pixel_format_ = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
  if (!pixel_format_) {
    // On several Forge machines, the default config fails. For now let's do
    // this.
    LOG(WARNING)
        << "failed to create pixel format; trying without acceleration";
    NSOpenGLPixelFormatAttribute attrs_no_accel[] = {NSOpenGLPFAColorSize,
                                                     24,
                                                     NSOpenGLPFAAlphaSize,
                                                     8,
                                                     NSOpenGLPFADepthSize,
                                                     16,
                                                     0};
    pixel_format_ =
        [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs_no_accel];
  }
  if (!pixel_format_)
    return ::mediapipe::InternalError(
        "Could not create an NSOpenGLPixelFormat");
  context_ = [[NSOpenGLContext alloc] initWithFormat:pixel_format_
                                        shareContext:share_context];

  // Try to query pixel format from shared context.
  if (!context_) {
    LOG(WARNING) << "Requested context not created, using queried context.";
    CGLContextObj cgl_ctx =
        static_cast<CGLContextObj>([share_context CGLContextObj]);
    CGLPixelFormatObj cgl_fmt =
        static_cast<CGLPixelFormatObj>(CGLGetPixelFormat(cgl_ctx));
    pixel_format_ =
        [[NSOpenGLPixelFormat alloc] initWithCGLPixelFormatObj:cgl_fmt];
    context_ = [[NSOpenGLContext alloc] initWithFormat:pixel_format_
                                          shareContext:share_context];
  }

  RET_CHECK(context_) << "Could not create an NSOpenGLContext";

  CVOpenGLTextureCacheRef cache;
  CVReturn err = CVOpenGLTextureCacheCreate(
      kCFAllocatorDefault, NULL, context_.CGLContextObj,
      pixel_format_.CGLPixelFormatObj, NULL, &cache);
  RET_CHECK_EQ(err, kCVReturnSuccess) << "Error at CVOpenGLTextureCacheCreate";
  texture_cache_.adopt(cache);

  return ::mediapipe::OkStatus();
}

void GlContext::DestroyContext() {}

GlContext::ContextBinding GlContext::ThisContextBinding() {
  GlContext::ContextBinding result;
  result.context_object = shared_from_this();
  result.context = context_;
  return result;
}

void GlContext::GetCurrentContextBinding(GlContext::ContextBinding* binding) {
  binding->context = [NSOpenGLContext currentContext];
}

::mediapipe::Status GlContext::SetCurrentContextBinding(
    const ContextBinding& new_binding) {
  if (new_binding.context) {
    [new_binding.context makeCurrentContext];
  } else {
    [NSOpenGLContext clearCurrentContext];
  }
  return ::mediapipe::OkStatus();
}

bool GlContext::HasContext() const { return context_ != nil; }

bool GlContext::IsCurrent() const {
  return HasContext() && ([NSOpenGLContext currentContext] == context_);
}

}  // namespace mediapipe

#endif  // HAS_NSGL
