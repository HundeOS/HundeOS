use crate::host::Subsystem;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Reply, Request, SessionId};
use hunde::service::{ReadinessMessage, ServiceName};
use hunde::wuff::{BundledComponent, LoadedComponent};

pub(crate) struct Streams<Component = LoadedComponent> {
    component: Component,
}

impl<Component> Streams<Component> {
    pub(crate) fn new(component: Component) -> Self {
        Self { component }
    }
}

impl Streams {
    pub(crate) fn load() -> Result<Self, IoError> {
        Ok(Self::new(LoadedComponent::load(BundledComponent::Pipe)?))
    }
}

impl<Component: IoComponent> IoComponent for Streams<Component> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.component.execute(request)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.component.disconnect(session)
    }
}

impl<Component: IoComponent> Subsystem for Streams<Component> {
    fn endpoint(&self) -> ServiceName {
        ServiceName::new("hund-rohr")
    }
    fn ready_message(&self) -> ReadinessMessage {
        ReadinessMessage::new("Rust pipe.wuff ready")
    }
}
