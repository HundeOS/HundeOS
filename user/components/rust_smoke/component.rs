#![no_std]
#![no_main]

use hunde::abi::IoError;
use hunde::component::{LifecycleComponent, LifecycleHost};

struct SmokeComponent;

impl LifecycleComponent for SmokeComponent {
    fn initialize(host: &LifecycleHost<'_>) -> Result<(), IoError> {
        host.log(b"Rust host component initialized")
    }
}

hunde::lifecycle_component!(SmokeComponent, b"Rust host component panicked");
