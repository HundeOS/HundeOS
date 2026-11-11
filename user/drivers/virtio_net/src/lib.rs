#![no_std]

extern crate alloc;
mod device;

use alloc::rc::Rc;
use core::cell::RefCell;
use device::Device;
use hunde::{
    abi::IoError,
    boot::VirtioEndpointConfiguration,
    packet::{self, Error, Frame, Info, Server},
    protocol::SERVICE_INVALID_BUFFER,
    wuff::{DriverComponent, DriverProgress},
};

struct Link(Rc<RefCell<Device>>);
impl packet::Device for Link {
    fn info(&self) -> Info {
        self.0.borrow().info()
    }
    fn transmit(&mut self, frame: Frame) -> Result<(), Frame> {
        self.0.borrow_mut().transmit(frame)
    }
    fn receive(&mut self) -> Result<Option<Frame>, Error> {
        self.0.borrow_mut().receive()
    }
    fn clear(&mut self) {
        self.0.borrow_mut().clear();
    }
}

pub struct NetworkDriver {
    device: Rc<RefCell<Device>>,
    server: Server<Link>,
    irq: u64,
    pending: bool,
    seen_irq: bool,
    error: Option<IoError>,
}

impl DriverComponent for NetworkDriver {
    fn load(bytes: &[u8]) -> Result<Self, IoError> {
        let configuration = VirtioEndpointConfiguration::from_wire_bytes(bytes)?;
        let hardware = hunde::boot::VirtioDeviceConfiguration::new(
            configuration.hardware,
            configuration.slot,
        )?;
        let irq = hardware
            .hardware
            .first_irq
            .checked_add(hardware.slot)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        let device = Rc::new(RefCell::new(Device::start(hardware)?));
        let server =
            Server::new(&configuration.path, Link(device.clone())).map_err(Error::into_io)?;
        Ok(Self {
            device,
            server,
            irq,
            pending: false,
            seen_irq: false,
            error: None,
        })
    }
    fn irq(&self) -> Option<u64> {
        Some(self.irq)
    }
    fn step(&mut self) -> Result<DriverProgress, IoError> {
        if let Some(error) = self.error.take() {
            return Err(error);
        }
        let server = self.server.step().map_err(Error::into_io)?;
        let (device, drained) = self.device.borrow_mut().poll()?;
        let rearm = self.pending && drained;
        if rearm {
            self.pending = false;
        }
        Ok(DriverProgress::new(server | device | rearm, rearm))
    }
    fn deliver(&mut self) {
        if !self.seen_irq {
            hunde::runtime::log(b"Rust VirtIO net IRQ delivered");
            self.seen_irq = true;
        }
        match self.device.borrow_mut().acknowledge() {
            Ok(()) => self.pending = true,
            Err(error) => self.error = Some(error),
        }
    }
}
