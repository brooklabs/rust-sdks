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

#pragma once

#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "livekit/peer_connection_factory.h"
#include "livekit/rtp_receiver.h"
#include "livekit/webrtc.h"
#include "rtc_base/synchronization/mutex.h"
#include "rust/cxx.h"

namespace livekit {

class RtcFrameTransformerObserverWrapper;

// Forward declare the ffi types that cxx will generate
struct EncodedFrameInfo;
struct EncodedFrameData;

// Internal frame transformer implementation that inherits from FrameTransformerInterface
class RecorderFrameTransformerImpl
    : public webrtc::FrameTransformerInterface {
 public:
  RecorderFrameTransformerImpl(
      std::shared_ptr<PeerConnectionFactory> peer_factory,
      rust::Box<RtcFrameTransformerObserverWrapper> observer);

  // FrameTransformerInterface implementation
  void Transform(
      std::unique_ptr<webrtc::TransformableFrameInterface> frame) override;
  void RegisterTransformedFrameCallback(
      webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback) override;
  void UnregisterTransformedFrameCallback() override;

  // Control methods - lock-free using atomic
  void set_enabled(bool enabled);
  bool enabled() const;

 private:
  // Extract frame data without holding any locks
  EncodedFrameData ExtractFrameData(
      webrtc::TransformableFrameInterface* frame) const;

  std::shared_ptr<PeerConnectionFactory> peer_factory_;
  rust::Box<RtcFrameTransformerObserverWrapper> observer_;

  // Mutex only protects callback registration, not the hot path
  mutable webrtc::Mutex callback_mutex_;
  webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback_;

  // Atomic for lock-free enabled check on hot path
  std::atomic<bool> enabled_{true};
};

// Wrapper class that can be exposed via cxx (using shared_ptr)
class RecorderFrameTransformer {
 public:
  RecorderFrameTransformer(
      std::shared_ptr<PeerConnectionFactory> peer_factory,
      rust::Box<RtcFrameTransformerObserverWrapper> observer);

  void set_enabled(bool enabled) const;
  bool enabled() const;

  webrtc::scoped_refptr<RecorderFrameTransformerImpl> impl() const {
    return impl_;
  }

 private:
  webrtc::scoped_refptr<RecorderFrameTransformerImpl> impl_;
};

// Create a new frame transformer for an RTP receiver
std::shared_ptr<RecorderFrameTransformer> new_recorder_frame_transformer(
    std::shared_ptr<PeerConnectionFactory> peer_factory,
    rust::Box<RtcFrameTransformerObserverWrapper> observer);

// Set the frame transformer on an RTP receiver (after depay, before decode)
void set_rtp_receiver_frame_transformer(
    std::shared_ptr<RtpReceiver> receiver,
    std::shared_ptr<RecorderFrameTransformer> transformer);

}  // namespace livekit
