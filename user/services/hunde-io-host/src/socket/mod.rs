use crate::host::Subsystem;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Reply, Request, SessionId};
use hunde::service::{ReadinessMessage, ServiceName};
use hunde::wuff::{BundledComponent, LoadedComponent};

pub(crate) struct Socket<Component = LoadedComponent> {
    component: Component,
}

impl<Component> Socket<Component> {
    pub(crate) fn new(component: Component) -> Self {
        Self { component }
    }
}

impl Socket {
    pub(crate) fn load() -> Result<Self, IoError> {
        Ok(Self::new(LoadedComponent::load(BundledComponent::Socket)?))
    }
}

impl<Component: IoComponent> IoComponent for Socket<Component> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.component.execute(request)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.component.disconnect(session)
    }
}

impl<Component: IoComponent> Subsystem for Socket<Component> {
    fn endpoint(&self) -> ServiceName {
        ServiceName::new("socket_router")
    }
    fn ready_message(&self) -> ReadinessMessage {
        ReadinessMessage::new("Rust socket.wuff ready")
    }
}
