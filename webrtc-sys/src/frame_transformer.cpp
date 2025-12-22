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

#include <iostream>
#include <memory>

#include "api/make_ref_counted.h"
#include "livekit/rtp_receiver.h"
#include "livekit/webrtc.h"
#include "webrtc-sys/src/frame_transformer.rs.h"

namespace livekit {

// RecorderFrameTransformerImpl implementation

RecorderFrameTransformerImpl::RecorderFrameTransformerImpl(
    std::shared_ptr<PeerConnectionFactory> peer_factory,
    rust::Box<RtcFrameTransformerObserverWrapper> observer)
    : peer_factory_(peer_factory), observer_(std::move(observer)) {
  std::cout << "[DEBUG C++] RecorderFrameTransformerImpl created" << std::endl;
}

void RecorderFrameTransformerImpl::Transform(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
  static int frame_count = 0;
  frame_count++;
  if (frame_count <= 5 || frame_count % 100 == 0) {
    std::cout << "[DEBUG C++] Transform called, frame #" << frame_count
              << ", size=" << frame->GetData().size() << " bytes" << std::endl;
  }

  webrtc::MutexLock lock(&mutex_);

  if (enabled_) {
    // Extract frame data for the observer
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
        dynamic_cast<webrtc::TransformableVideoFrameInterface*>(frame.get());
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

    // Notify the Rust observer
    observer_->on_encoded_frame(encoded_frame);
  }

  // Pass the frame through to the decoder if callback is available
  if (callback_) {
    callback_->OnTransformedFrame(std::move(frame));
  }
  // Note: If no callback, frame is dropped (not passed to decoder)
  // This is fine for recording - we just captured the data above
}

void RecorderFrameTransformerImpl::RegisterTransformedFrameCallback(
    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback) {
  std::cout << "[DEBUG C++] RegisterTransformedFrameCallback called, callback="
            << (callback ? "valid" : "null") << std::endl;
  webrtc::MutexLock lock(&mutex_);
  callback_ = callback;
}

void RecorderFrameTransformerImpl::UnregisterTransformedFrameCallback() {
  webrtc::MutexLock lock(&mutex_);
  callback_ = nullptr;
}

void RecorderFrameTransformerImpl::set_enabled(bool enabled) {
  webrtc::MutexLock lock(&mutex_);
  enabled_ = enabled;
}

bool RecorderFrameTransformerImpl::enabled() const {
  webrtc::MutexLock lock(&mutex_);
  return enabled_;
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
  std::cout << "[DEBUG C++] set_rtp_receiver_frame_transformer called" << std::endl;
  std::cout << "[DEBUG C++]   receiver=" << (receiver ? "valid" : "null") << std::endl;
  std::cout << "[DEBUG C++]   transformer=" << (transformer ? "valid" : "null") << std::endl;

  auto rtc_receiver = receiver->rtc_receiver();
  std::cout << "[DEBUG C++]   rtc_receiver=" << (rtc_receiver ? "valid" : "null") << std::endl;

  auto impl = transformer->impl();
  std::cout << "[DEBUG C++]   transformer->impl()=" << (impl ? "valid" : "null") << std::endl;

  std::cout << "[DEBUG C++]   Calling SetDepacketizerToDecoderFrameTransformer..." << std::endl;
  rtc_receiver->SetDepacketizerToDecoderFrameTransformer(impl);
  std::cout << "[DEBUG C++]   SetDepacketizerToDecoderFrameTransformer completed" << std::endl;
}

}  // namespace livekit
