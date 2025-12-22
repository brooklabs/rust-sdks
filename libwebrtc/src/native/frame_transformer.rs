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

use cxx::SharedPtr;
use parking_lot::Mutex;
use webrtc_sys::frame_transformer::{self as sys_ft};

use crate::{peer_connection_factory::PeerConnectionFactory, rtp_receiver::RtpReceiver};

/// Callback type for receiving encoded frames
pub type OnEncodedFrame = Box<dyn FnMut(EncodedFrameData) + Send + Sync>;

/// Metadata about an encoded frame
#[derive(Debug, Clone)]
pub struct EncodedFrameInfo {
    pub ssrc: u32,
    pub timestamp: u32,
    pub is_key_frame: bool,
    pub capture_time_ms: i64,
    pub codec_mime_type: String,
}

/// Encoded frame data with metadata
#[derive(Debug, Clone)]
pub struct EncodedFrameData {
    pub data: Vec<u8>,
    pub info: EncodedFrameInfo,
}

/// A frame transformer that intercepts encoded frames before decoding.
/// This allows recording raw H264/H265 packets without decoding overhead.
#[derive(Clone)]
pub struct RecorderFrameTransformer {
    observer: Arc<RtcFrameTransformerObserver>,
    pub(crate) sys_handle: SharedPtr<sys_ft::ffi::RecorderFrameTransformer>,
}

impl RecorderFrameTransformer {
    /// Create a new frame transformer for recording encoded frames
    pub fn new(peer_factory: &PeerConnectionFactory) -> Self {
        let observer = Arc::new(RtcFrameTransformerObserver::default());
        let sys_handle = sys_ft::ffi::new_recorder_frame_transformer(
            peer_factory.handle.sys_handle.clone(),
            Box::new(sys_ft::RtcFrameTransformerObserverWrapper::new(observer.clone())),
        );
        Self { observer, sys_handle }
    }

    /// Attach this transformer to an RTP receiver to intercept encoded video frames
    pub fn attach_to_receiver(&self, receiver: &RtpReceiver) {
        sys_ft::ffi::set_rtp_receiver_frame_transformer(
            receiver.handle.sys_handle.clone(),
            self.sys_handle.clone(),
        );
    }

    /// Enable or disable frame interception
    pub fn set_enabled(&self, enabled: bool) {
        self.sys_handle.set_enabled(enabled);
    }

    /// Check if frame interception is enabled
    pub fn enabled(&self) -> bool {
        self.sys_handle.enabled()
    }

    /// Set handler for encoded frames
    pub fn on_encoded_frame(&self, handler: Option<OnEncodedFrame>) {
        *self.observer.frame_handler.lock() = handler;
    }
}

#[derive(Default)]
struct RtcFrameTransformerObserver {
    frame_handler: Mutex<Option<OnEncodedFrame>>,
}

impl sys_ft::FrameTransformerObserver for RtcFrameTransformerObserver {
    fn on_encoded_frame(&self, frame: sys_ft::EncodedFrameData) {
        let mut handler = self.frame_handler.lock();
        if let Some(f) = handler.as_mut() {
            f(frame.into());
        }
    }
}

impl From<sys_ft::ffi::EncodedFrameInfo> for EncodedFrameInfo {
    fn from(value: sys_ft::ffi::EncodedFrameInfo) -> Self {
        Self {
            ssrc: value.ssrc,
            timestamp: value.timestamp,
            is_key_frame: value.is_key_frame,
            capture_time_ms: value.capture_time_ms,
            codec_mime_type: value.codec_mime_type,
        }
    }
}

impl From<sys_ft::ffi::EncodedFrameData> for EncodedFrameData {
    fn from(value: sys_ft::ffi::EncodedFrameData) -> Self {
        Self {
            data: value.data.into_iter().collect(),
            info: value.info.into(),
        }
    }
}
