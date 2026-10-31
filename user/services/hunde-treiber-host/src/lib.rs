#![cfg_attr(feature = "service", no_std)]

extern crate alloc;

mod interrupts;

use alloc::format;
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::boot::{BlockDriverConfiguration, InterruptController, VirtioDeviceConfiguration};
use hunde::driver::{self, Client, Event, Listener, Manager, Response};
use hunde::wuff::{BundledDriver, LoadedDriver};
use hunde_virtio_blk::probe_slots;
use hunde_virtio_keyboard::probe as probe_keyboard;
use interrupts::Registrations;

#[cfg(feature = "service")]
hunde::executable!(run, b"Rust driver host panicked");

const ENDPOINT: &[u8] = b"hund-unterbrict";

fn hosts_sd_host(kind: u64, controller: &InterruptController) -> bool {
    matches!(
        kind,
        hunde::protocol::SD_HOST_KIND_PCI | hunde::protocol::SD_HOST_KIND_BCM2835
    ) || (kind == hunde::protocol::SD_HOST_KIND_CV181X
        && matches!(controller, InterruptController::Gic { .. }))
}

trait Replies {
    fn reply(&mut self, response: Response) -> Result<bool, IoError>;
}

impl Replies for Client {
    fn reply(&mut self, response: Response) -> Result<bool, IoError> {
        self.reply(response)
    }
}

struct Consumer<Transport: Replies = Client> {
    client: Transport,
    pending: Option<Response>,
}

impl<Transport: Replies> Consumer<Transport> {
    fn new(client: Transport) -> Self {
        Self {
            client,
            pending: None,
        }
    }

    fn send(&mut self, response: Response) {
        self.pending = Some(response);
        self.flush();
    }

    fn flush(&mut self) -> bool {
        if let Some(response) = self.pending
            && self.client.reply(response) == Ok(true)
        {
            self.pending = None;
            return true;
        }
        false
    }
}

pub fn run() -> Result<(), IoError> {
    hunde::service::become_service()?;
    let manager = Manager::register()?;
    let listener = Listener::listen(ENDPOINT)?;
    let mut registrations = Registrations::new();
    let hardware = hunde::boot::HardwareInfo::load()?;
    let mut drivers = Vec::new();
    let mut sdhci_irq = None;
    let mut sdhci_irq_seen = false;
    if let Some(virtio) = hardware.virtio_mmio {
        let slots = probe_slots(virtio)?;
        let image = BundledDriver::find("/sbin/components/virtio_blk.wuff")?;
        for (count, slot) in (0..64)
            .rev()
            .filter(|slot| slots & (1_u64 << slot) != 0)
            .enumerate()
        {
            if count == 16 {
                return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
            }
            let path = format!("/dev/virtio{}", count + 1);
            let configuration = BlockDriverConfiguration::new(virtio, slot, path.as_bytes())?;
            let irq = virtio
                .first_irq
                .checked_add(slot)
                .ok_or(IoError(hunde::protocol::SERVICE_INVALID_BUFFER))?;
            manager.route_self(irq)?;
            let driver = LoadedDriver::load(image, &configuration.wire_bytes())?;
            if driver.irq() != Some(irq) {
                return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
            }
            registrations.reserve_hosted(irq)?;
            listener.publish_ready(path.as_bytes())?;
            drivers.push(driver);
        }
        if let Some(slot) = probe_keyboard(virtio)? {
            let configuration = VirtioDeviceConfiguration::new(virtio, slot)?;
            let irq = virtio
                .first_irq
                .checked_add(slot)
                .ok_or(IoError(hunde::protocol::SERVICE_INVALID_BUFFER))?;
            manager.route_self(irq)?;
            let image = BundledDriver::find("/sbin/components/virtio_keyboard.wuff")?;
            let driver = LoadedDriver::load(image, &configuration.wire_bytes())?;
            if driver.irq() != Some(irq) {
                return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
            }
            registrations.reserve_hosted(irq)?;
            listener.publish_ready(b"keyboard")?;
            hunde::runtime::log(b"Rust virtio_keyboard.wuff ready");
            drivers.push(driver);
        }
    }
    if hardware.framebuffer.is_none() {
        let image = BundledDriver::find("/sbin/components/uart_rx.wuff")?;
        manager.route_self(hardware.uart.irq)?;
        let receiver = LoadedDriver::load(image, hardware.uart.wire_bytes())?;
        if receiver.irq() != Some(hardware.uart.irq) {
            return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
        }
        registrations.reserve_hosted(hardware.uart.irq)?;
        listener.publish_ready(b"uart_rx")?;
        drivers.push(receiver);
        let image = BundledDriver::find("/sbin/components/uart_tx.wuff")?;
        let transmitter = LoadedDriver::load(image, hardware.uart.wire_bytes())?;
        if transmitter.irq().is_some() {
            return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
        }
        listener.publish_ready(b"uart_tx")?;
        drivers.push(transmitter);
    }
    if let Some(sd_host) = hardware.sd_host
        && hosts_sd_host(sd_host.kind, &hardware.interrupt_controller)
    {
        manager.route_self(sd_host.device.irq)?;
        let image = BundledDriver::find("/sbin/components/sdhci.wuff")?;
        let driver = LoadedDriver::load(image, &sd_host.wire_bytes())?;
        if driver.irq() != Some(sd_host.device.irq) {
            return Err(IoError(hunde::protocol::SERVICE_DEVICE_REGISTRATION_FAILED));
        }
        registrations.reserve_hosted(sd_host.device.irq)?;
        listener.publish_ready(b"/dev/mmc0")?;
        drivers.push(driver);
        sdhci_irq = Some(sd_host.device.irq);
    }
    listener.publish_ready(ENDPOINT)?;
    hunde::runtime::log(b"Rust driver host ready");
    let mut consumers: Vec<Consumer> = Vec::new();
    driver::prepare_wait();
    loop {
        let mut work = false;
        for connection in listener.connections() {
            if consumers
                .iter()
                .any(|consumer| consumer.client.connection() == connection)
            {
                continue;
            }
            if let Ok(client) = Client::accept(connection) {
                consumers.push(Consumer::new(client));
            }
            work = true;
        }
        for consumer in &mut consumers {
            work |= consumer.flush();
            if consumer.pending.is_some() {
                continue;
            }
            if let Ok(Some(request)) = consumer.client.poll() {
                if let Some(response) = registrations.request(
                    consumer.client.connection(),
                    consumer.client.pid(),
                    request,
                    manager.as_ref(),
                ) {
                    consumer.send(response);
                }
                work = true;
            }
            if consumer.pending.is_none()
                && let Some(response) = registrations.delivery(consumer.client.connection())
            {
                consumer.send(response);
                work = true;
            }
        }
        for driver in &mut drivers {
            let progress = driver.step()?;
            work |= progress.worked;
            if progress.rearm {
                manager.rearm(driver.irq().ok_or(IoError(hunde::protocol::IO_PANIC))?)?;
            }
        }
        if let Some(event) = manager.poll()? {
            if sdhci_irq == Some(event.irq) && !sdhci_irq_seen {
                hunde::runtime::log(b"Rust SDHCI IRQ delivered");
                sdhci_irq_seen = true;
            }
            dispatch(&mut drivers, &mut registrations, event);
            work = true;
        }
        if !work {
            if sdhci_irq.is_some() {
                driver::wait_ticks(10);
            } else {
                driver::wait();
            }
        }
    }
}

trait Device {
    fn irq(&self) -> u64;
    fn deliver(&mut self, event: Event);
}

impl Device for LoadedDriver {
    fn irq(&self) -> u64 {
        self.irq().unwrap_or_default()
    }
    fn deliver(&mut self, event: Event) {
        if self.irq() == Some(event.irq) {
            self.deliver();
        }
    }
}

fn dispatch(devices: &mut [impl Device], registrations: &mut Registrations, event: Event) {
    if let Some(device) = devices.iter_mut().find(|device| device.irq() == event.irq) {
        device.deliver(event);
    } else {
        registrations.record(event);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use hunde::boot::{Gic, Plic};

    #[test]
    fn cv181x_is_hosted_only_on_the_gic_board_path() {
        let gic = InterruptController::Gic {
            kind: 1,
            info: Gic::new(),
        };
        let plic = InterruptController::Plic {
            kind: 3,
            info: Plic::new(),
        };
        assert!(hosts_sd_host(hunde::protocol::SD_HOST_KIND_CV181X, &gic));
        assert!(!hosts_sd_host(hunde::protocol::SD_HOST_KIND_CV181X, &plic));
        assert!(hosts_sd_host(hunde::protocol::SD_HOST_KIND_BCM2835, &gic));
        assert!(hosts_sd_host(hunde::protocol::SD_HOST_KIND_PCI, &gic));
    }

    struct FakeReplies {
        attempts: Vec<Response>,
        result: Result<bool, IoError>,
    }

    impl FakeReplies {
        fn new() -> Self {
            Self {
                attempts: Vec::new(),
                result: Ok(false),
            }
        }
    }

    impl Replies for FakeReplies {
        fn reply(&mut self, response: Response) -> Result<bool, IoError> {
            self.attempts.push(response);
            self.result
        }
    }

    #[test]
    fn backpressure_and_transport_errors_retain_the_exact_reply_until_sent() {
        let response = Response::delivery(Event::new(48, 9, 100, 110), 2);
        let mut consumer = Consumer::new(FakeReplies::new());
        consumer.send(response);
        assert_eq!(consumer.pending, Some(response));
        consumer.client.result = Err(IoError(-99));
        assert!(!consumer.flush());
        assert_eq!(consumer.pending, Some(response));
        consumer.client.result = Ok(true);
        assert!(consumer.flush());
        assert_eq!(consumer.pending, None);
        assert!(!consumer.flush());
        assert_eq!(consumer.client.attempts, [response, response, response]);
    }

    struct FakeDevice {
        irq: u64,
        events: Vec<Event>,
    }
    impl FakeDevice {
        fn new(irq: u64) -> Self {
            Self {
                irq,
                events: Vec::new(),
            }
        }
    }
    impl Device for FakeDevice {
        fn irq(&self) -> u64 {
            self.irq
        }
        fn deliver(&mut self, event: Event) {
            self.events.push(event);
        }
    }

    #[test]
    fn hosted_interrupts_reach_only_the_owning_device_without_external_delivery() {
        let mut devices = [FakeDevice::new(48), FakeDevice::new(49)];
        let mut registrations = Registrations::new();
        let event = Event::new(49, 7, 100, 120);
        dispatch(&mut devices, &mut registrations, event);
        assert!(devices[0].events.is_empty());
        assert_eq!(devices[1].events, [event]);
        assert_eq!(registrations.delivery(1), None);
    }
}
