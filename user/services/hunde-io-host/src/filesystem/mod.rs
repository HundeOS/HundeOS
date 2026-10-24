use crate::host::Subsystem;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Reply, Request, SessionId};
use hunde::service::{ReadinessMessage, ServiceName};
use hunde::wuff::{BundledComponent, BundledFilesystem, LoadedComponent};

pub(crate) mod control;
pub(crate) mod namespace;

#[derive(Clone, Copy)]
pub(crate) enum FilesystemKind {
    Temporary,
    Processes,
    Boot,
}

pub(crate) struct Filesystem<Component = LoadedComponent> {
    name: ServiceName,
    ready: ReadinessMessage,
    component: Component,
}

impl<Component> Filesystem<Component> {
    pub(crate) fn new(kind: FilesystemKind, component: Component) -> Self {
        let (name, ready) = match kind {
            FilesystemKind::Temporary => (
                ServiceName::new("/tmp"),
                ReadinessMessage::new("Rust memfs.wuff ready"),
            ),
            FilesystemKind::Processes => (
                ServiceName::new("/proc"),
                ReadinessMessage::new("Rust procfs.wuff ready"),
            ),
            FilesystemKind::Boot => (
                ServiceName::new("/boot"),
                ReadinessMessage::new("Rust sysinfofs.wuff ready"),
            ),
        };
        Self {
            name,
            ready,
            component,
        }
    }

    fn mounted(name: ServiceName, component: Component, format: BundledFilesystem) -> Self {
        Self {
            name,
            ready: ReadinessMessage::new(match format {
                BundledFilesystem::Ext2 => "Rust ext2fs.wuff ready",
                BundledFilesystem::Fat32 => "Rust fat32.wuff ready",
            }),
            component,
        }
    }
}

impl Filesystem {
    pub(crate) fn devices() -> Result<Filesystem<hunde::devfs::Filesystem>, IoError> {
        Ok(Filesystem {
            name: ServiceName::new("/dev"),
            ready: ReadinessMessage::new("Rust hosted devfs ready"),
            component: hunde::devfs::Filesystem::new()?,
        })
    }

    pub(crate) fn load(kind: FilesystemKind) -> Result<Self, IoError> {
        let bundled = match kind {
            FilesystemKind::Temporary => BundledComponent::Memfs,
            FilesystemKind::Processes => BundledComponent::Procfs,
            FilesystemKind::Boot => BundledComponent::Sysinfofs,
        };
        Ok(Self::new(kind, LoadedComponent::load(bundled)?))
    }
}

impl<Component: IoComponent> IoComponent for Filesystem<Component> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.component.execute(request)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.component.disconnect(session)
    }

    fn reclaim_cache(&mut self) -> Result<(), IoError> {
        self.component.reclaim_cache()
    }
}

impl<Component: IoComponent> Subsystem for Filesystem<Component> {
    fn endpoint(&self) -> ServiceName {
        self.name.clone()
    }

    fn ready_message(&self) -> ReadinessMessage {
        self.ready
    }
}
