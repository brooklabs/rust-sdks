// Copyright 2025 LiveKit, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::sync::Arc;

use crate::impl_thread_safety;

#[cxx::bridge(namespace = "livekit")]
pub mod ffi {
    /// Metadata about an encoded frame
    #[derive(Debug, Clone)]
    pub struct EncodedFrameInfo {
        pub ssrc: u32,
        pub timestamp: u32,
        pub is_key_frame: bool,
        pub capture_time_ms: i64,
        pub codec_mime_type: String,
        /// Frame ID from VideoFrameMetadata - should be sequential, -1 if not available
        pub frame_id: i64,
    }

    /// The encoded frame data passed to Rust
    #[derive(Debug)]
    pub struct EncodedFrameData {
        pub data: Vec<u8>,
        pub info: EncodedFrameInfo,
    }

    unsafe extern "C++" {
        include!("livekit/frame_transformer.h");
        include!("livekit/rtp_receiver.h");
        include!("livekit/peer_connection_factory.h");

        type RtpReceiver = crate::rtp_receiver::ffi::RtpReceiver;
        type PeerConnectionFactory = crate::peer_connection_factory::ffi::PeerConnectionFactory;

        pub type RecorderFrameTransformer;

        /// Create a new frame transformer that intercepts encoded frames
        pub fn new_recorder_frame_transformer(
            peer_factory: SharedPtr<PeerConnectionFactory>,
            observer: Box<RtcFrameTransformerObserverWrapper>,
        ) -> SharedPtr<RecorderFrameTransformer>;

        /// Set the frame transformer on an RTP receiver (after depay, before decode)
        pub fn set_rtp_receiver_frame_transformer(
            receiver: SharedPtr<RtpReceiver>,
            transformer: SharedPtr<RecorderFrameTransformer>,
        );

        /// Enable or disable the frame transformer
        pub fn set_enabled(self: &RecorderFrameTransformer, enabled: bool);

        /// Check if the frame transformer is enabled
        pub fn enabled(self: &RecorderFrameTransformer) -> bool;
    }

    extern "Rust" {
        type RtcFrameTransformerObserverWrapper;

        /// Called when an encoded frame is received
        fn on_encoded_frame(
            self: &RtcFrameTransformerObserverWrapper,
            frame: EncodedFrameData,
        );
    }
}

impl_thread_safety!(ffi::RecorderFrameTransformer, Send + Sync);

pub use ffi::{EncodedFrameData, EncodedFrameInfo};

/// Trait for receiving encoded frames
pub trait FrameTransformerObserver: Send + Sync {
    fn on_encoded_frame(&self, frame: EncodedFrameData);
}

/// Wrapper for passing the observer to C++
pub struct RtcFrameTransformerObserverWrapper {
    observer: Arc<dyn FrameTransformerObserver>,
}

impl RtcFrameTransformerObserverWrapper {
    pub fn new(observer: Arc<dyn FrameTransformerObserver>) -> Self {
        Self { observer }
    }

    fn on_encoded_frame(&self, frame: EncodedFrameData) {
        self.observer.on_encoded_frame(frame);
    }
}
