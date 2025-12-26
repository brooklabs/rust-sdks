/*
 * Copyright 2025 LiveKit, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/frame_transformer.h"

#include <memory>

#include "api/make_ref_counted.h"
#include "livekit/rtp_receiver.h"
#include "livekit/webrtc.h"
#include "rtc_base/logging.h"
#include "webrtc-sys/src/frame_transformer.rs.h"

namespace livekit {

// RecorderFrameTransformerImpl implementation

RecorderFrameTransformerImpl::RecorderFrameTransformerImpl(
    std::shared_ptr<PeerConnectionFactory> peer_factory,
    rust::Box<RtcFrameTransformerObserverWrapper> observer)
    : peer_factory_(peer_factory), observer_(std::move(observer)) {
  RTC_LOG(LS_INFO) << "RecorderFrameTransformerImpl created";
}

EncodedFrameData RecorderFrameTransformerImpl::ExtractFrameData(
    webrtc::TransformableFrameInterface* frame) const {
  auto data = frame->GetData();

  EncodedFrameInfo info;
  info.ssrc = frame->GetSsrc();
  info.timestamp = frame->GetTimestamp();
  info.is_key_frame = false;
  info.capture_time_ms = 0;

  // Get mime type from the frame
  info.codec_mime_type = rust::String(frame->GetMimeType());

  // Try to get more info if this is a video frame
  auto* video_frame =
      dynamic_cast<webrtc::TransformableVideoFrameInterface*>(frame);
  if (video_frame) {
    info.is_key_frame = video_frame->IsKeyFrame();
    auto capture_time = frame->CaptureTime();
    if (capture_time.has_value()) {
      info.capture_time_ms = capture_time->ms();
    }
  }

  // Copy frame data to Rust-compatible vector
  rust::Vec<uint8_t> frame_data;
  frame_data.reserve(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    frame_data.push_back(data[i]);
  }

  EncodedFrameData encoded_frame;
  encoded_frame.data = std::move(frame_data);
  encoded_frame.info = info;

  return encoded_frame;
}

void RecorderFrameTransformerImpl::Transform(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
  // Check enabled flag atomically - no lock needed
  if (enabled_.load(std::memory_order_acquire)) {
    // Extract frame data without holding any lock
    EncodedFrameData encoded_frame = ExtractFrameData(frame.get());

    // Notify the Rust observer without holding C++ mutex
    // The Rust side has its own synchronization for the callback
    observer_->on_encoded_frame(encoded_frame);
  }

  // Get callback under lock, but call it outside the lock
  webrtc::scoped_refptr<webrtc::TransformedFrameCallback> cb;
  {
    webrtc::MutexLock lock(&callback_mutex_);
    cb = callback_;
  }

  // Pass the frame through to the decoder if callback is available
  // This is called outside the lock to avoid holding mutex during WebRTC callback
  if (cb) {
    cb->OnTransformedFrame(std::move(frame));
  }
  // Note: If no callback, frame is dropped (not passed to decoder)
  // This is fine for recording - we just captured the data above
}

void RecorderFrameTransformerImpl::RegisterTransformedFrameCallback(
    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback) {
  RTC_LOG(LS_INFO) << "RegisterTransformedFrameCallback called, callback="
                   << (callback ? "valid" : "null");
  webrtc::MutexLock lock(&callback_mutex_);
  callback_ = callback;
}

void RecorderFrameTransformerImpl::UnregisterTransformedFrameCallback() {
  webrtc::MutexLock lock(&callback_mutex_);
  callback_ = nullptr;
}

void RecorderFrameTransformerImpl::set_enabled(bool enabled) {
  enabled_.store(enabled, std::memory_order_release);
}

bool RecorderFrameTransformerImpl::enabled() const {
  return enabled_.load(std::memory_order_acquire);
}

// RecorderFrameTransformer wrapper implementation

RecorderFrameTransformer::RecorderFrameTransformer(
    std::shared_ptr<PeerConnectionFactory> peer_factory,
    rust::Box<RtcFrameTransformerObserverWrapper> observer)
    : impl_(webrtc::make_ref_counted<RecorderFrameTransformerImpl>(
          peer_factory, std::move(observer))) {}

void RecorderFrameTransformer::set_enabled(bool enabled) const {
  impl_->set_enabled(enabled);
}

bool RecorderFrameTransformer::enabled() const {
  return impl_->enabled();
}

// Factory functions

std::shared_ptr<RecorderFrameTransformer> new_recorder_frame_transformer(
    std::shared_ptr<PeerConnectionFactory> peer_factory,
    rust::Box<RtcFrameTransformerObserverWrapper> observer) {
  return std::make_shared<RecorderFrameTransformer>(
      peer_factory, std::move(observer));
}

void set_rtp_receiver_frame_transformer(
    std::shared_ptr<RtpReceiver> receiver,
    std::shared_ptr<RecorderFrameTransformer> transformer) {
  RTC_LOG(LS_INFO) << "set_rtp_receiver_frame_transformer called";

  auto rtc_receiver = receiver->rtc_receiver();
  auto impl = transformer->impl();

  RTC_LOG(LS_INFO) << "Attaching frame transformer to RTP receiver";
  rtc_receiver->SetDepacketizerToDecoderFrameTransformer(impl);
}

}  // namespace livekit
