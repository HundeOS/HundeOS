use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Reply, Request, SessionId};
use hunde::service::{ReadinessMessage, ServiceName};

pub(crate) struct ConfiguredComponent<Driver> {
    name: ServiceName,
    ready: ReadinessMessage,
    driver: Driver,
}

impl<Driver> ConfiguredComponent<Driver> {
    pub(crate) fn new(name: ServiceName, ready: ReadinessMessage, driver: Driver) -> Self {
        Self {
            name,
            ready,
            driver,
        }
    }
}

pub(crate) fn image_ready(path: &str) -> ReadinessMessage {
    ReadinessMessage::from_string(alloc::format!(
        "Rust {} ready",
        path.rsplit('/').next().unwrap_or(path)
    ))
}

impl<Driver: IoComponent> IoComponent for ConfiguredComponent<Driver> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.driver.execute(request)
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.driver.disconnect(session)
    }
    fn reclaim_cache(&mut self) -> Result<(), IoError> {
        self.driver.reclaim_cache()
    }
}

impl<Driver: IoComponent> super::Subsystem for ConfiguredComponent<Driver> {
    fn endpoint(&self) -> ServiceName {
        self.name.clone()
    }
    fn ready_message(&self) -> ReadinessMessage {
        self.ready.clone()
    }
}
