use alloc::collections::{BTreeMap, BTreeSet};
use hunde::abi::IoError;
use hunde::driver::{Event, Manager, Operation, Request, Response};
use hunde::protocol::*;

pub(crate) trait Controller {
    fn route(&self, pid: u64, irq: u64) -> Result<(), IoError>;
    fn rearm(&self, irq: u64) -> Result<(), IoError>;
}

impl Controller for Manager {
    fn route(&self, pid: u64, irq: u64) -> Result<(), IoError> {
        self.route(pid, irq)
    }
    fn rearm(&self, irq: u64) -> Result<(), IoError> {
        self.rearm(irq)
    }
}

struct Registration {
    connection: u64,
    generation: u64,
    outstanding: Option<u64>,
    pending: Option<Event>,
    waiting: bool,
}

impl Registration {
    fn new(connection: u64, generation: u64) -> Self {
        Self {
            connection,
            generation,
            outstanding: None,
            pending: None,
            waiting: false,
        }
    }
}

pub(crate) struct Registrations {
    external: BTreeMap<u64, Registration>,
    hosted: BTreeSet<u64>,
    generation: u64,
}

impl Registrations {
    pub(crate) fn new() -> Self {
        Self {
            external: BTreeMap::new(),
            hosted: BTreeSet::new(),
            generation: 0,
        }
    }

    pub(crate) fn reserve_hosted(&mut self, irq: u64) -> Result<(), IoError> {
        if self.external.contains_key(&irq) || !self.hosted.insert(irq) {
            return Err(IoError(SERVICE_IRQ_ALREADY_REGISTERED));
        }
        Ok(())
    }

    pub(crate) fn request(
        &mut self,
        connection: u64,
        pid: u64,
        request: Request,
        controller: &impl Controller,
    ) -> Option<Response> {
        let invalid = || Some(Response::error(request.irq, INTERRUPT_PROTOCOL_INVALID));
        match request.operation {
            Operation::Subscribe => {
                if self.hosted.contains(&request.irq)
                    || self.external.contains_key(&request.irq)
                    || self
                        .external
                        .values()
                        .any(|registration| registration.connection == connection)
                {
                    return Some(Response::error(
                        request.irq,
                        INTERRUPT_PROTOCOL_ALREADY_REGISTERED,
                    ));
                }
                if let Err(error) = controller.route(pid, request.irq) {
                    let code = if error.0 == SERVICE_PERMISSION_DENIED {
                        INTERRUPT_PROTOCOL_DENIED
                    } else {
                        INTERRUPT_PROTOCOL_INVALID
                    };
                    return Some(Response::error(request.irq, code));
                }
                self.generation = self.generation.wrapping_add(1).max(1);
                self.external
                    .insert(request.irq, Registration::new(connection, self.generation));
                Some(Response::subscribed(request.irq, self.generation))
            }
            Operation::Wait => {
                if let Some(registration) = self.external.get_mut(&request.irq)
                    && registration.connection == connection
                    && registration.generation == request.generation
                    && registration.outstanding.is_none()
                    && !registration.waiting
                {
                    registration.waiting = true;
                    return None;
                }
                invalid()
            }
            Operation::Acknowledge => {
                if let Some(registration) = self.external.get_mut(&request.irq)
                    && registration.connection == connection
                    && registration.generation == request.generation
                    && request.sequence != 0
                    && registration.outstanding == Some(request.sequence)
                {
                    if controller.rearm(request.irq).is_err() {
                        return invalid();
                    }
                    registration.outstanding = None;
                    return Some(Response::acknowledged(request));
                }
                Some(Response::error(
                    request.irq,
                    INTERRUPT_PROTOCOL_STALE_ACKNOWLEDGEMENT,
                ))
            }
            Operation::Invalid => invalid(),
        }
    }

    pub(crate) fn record(&mut self, event: Event) {
        if let Some(registration) = self.external.get_mut(&event.irq) {
            registration.pending = Some(event);
        }
    }

    pub(crate) fn delivery(&mut self, connection: u64) -> Option<Response> {
        let registration = self
            .external
            .values_mut()
            .find(|registration| registration.connection == connection)?;
        if !registration.waiting || registration.outstanding.is_some() {
            return None;
        }
        let event = registration.pending.take()?;
        registration.outstanding = Some(event.sequence);
        registration.waiting = false;
        Some(Response::delivery(event, registration.generation))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    use core::cell::RefCell;

    #[derive(Default)]
    struct FakeController {
        routes: RefCell<Vec<(u64, u64)>>,
        rearms: RefCell<Vec<u64>>,
        route_error: Option<IoError>,
        rearm_error: Option<IoError>,
    }
    impl Controller for FakeController {
        fn route(&self, pid: u64, irq: u64) -> Result<(), IoError> {
            self.routes.borrow_mut().push((pid, irq));
            self.route_error.map_or(Ok(()), Err)
        }
        fn rearm(&self, irq: u64) -> Result<(), IoError> {
            self.rearms.borrow_mut().push(irq);
            self.rearm_error.map_or(Ok(()), Err)
        }
    }

    fn subscribed(registrations: &mut Registrations, controller: &FakeController) {
        assert_eq!(
            registrations.request(
                17,
                4,
                Request::new(Operation::Subscribe, 48, 0, 0),
                controller
            ),
            Some(Response::subscribed(48, 1))
        );
    }

    #[test]
    fn authenticates_routes_and_retains_delivery_until_wait_then_matching_ack() {
        let mut registrations = Registrations::new();
        let controller = FakeController::default();
        subscribed(&mut registrations, &controller);
        assert_eq!(*controller.routes.borrow(), [(4, 48)]);
        let event = Event::new(48, 9, 1234, 1250);
        registrations.record(event);
        assert_eq!(registrations.delivery(17), None);
        assert_eq!(
            registrations.request(17, 4, Request::new(Operation::Wait, 48, 1, 0), &controller),
            None
        );
        assert_eq!(
            registrations.delivery(17),
            Some(Response::delivery(event, 1))
        );
        assert_eq!(registrations.delivery(17), None);
        let ack = Request::new(Operation::Acknowledge, 48, 1, 9);
        assert_eq!(
            registrations.request(17, 4, ack, &controller),
            Some(Response::acknowledged(ack))
        );
        assert_eq!(*controller.rearms.borrow(), [48]);
        assert_eq!(
            registrations.request(17, 4, ack, &controller),
            Some(Response::error(
                48,
                INTERRUPT_PROTOCOL_STALE_ACKNOWLEDGEMENT
            ))
        );
        assert_eq!(*controller.rearms.borrow(), [48]);
    }

    #[test]
    fn rejects_hosted_duplicate_and_second_connection_registrations_before_routing() {
        let mut registrations = Registrations::new();
        let controller = FakeController::default();
        registrations.reserve_hosted(49).unwrap();
        subscribed(&mut registrations, &controller);
        for (connection, irq) in [(18, 48), (17, 50), (18, 49)] {
            assert_eq!(
                registrations.request(
                    connection,
                    4,
                    Request::new(Operation::Subscribe, irq, 0, 0),
                    &controller
                ),
                Some(Response::error(irq, INTERRUPT_PROTOCOL_ALREADY_REGISTERED))
            );
        }
        assert_eq!(*controller.routes.borrow(), [(4, 48)]);
    }

    #[test]
    fn rejects_cross_connection_stale_generation_and_wrong_sequence_acknowledgements() {
        let mut registrations = Registrations::new();
        let controller = FakeController::default();
        subscribed(&mut registrations, &controller);
        registrations.request(17, 4, Request::new(Operation::Wait, 48, 1, 0), &controller);
        registrations.record(Event::new(48, 9, 100, 110));
        registrations.delivery(17);
        for (connection, generation, sequence) in [(18, 1, 9), (17, 2, 9), (17, 1, 8), (17, 1, 0)] {
            assert_eq!(
                registrations.request(
                    connection,
                    4,
                    Request::new(Operation::Acknowledge, 48, generation, sequence),
                    &controller
                ),
                Some(Response::error(
                    48,
                    INTERRUPT_PROTOCOL_STALE_ACKNOWLEDGEMENT
                ))
            );
        }
        assert!(controller.rearms.borrow().is_empty());
    }

    #[test]
    fn denied_routes_and_failed_rearms_do_not_commit_state() {
        let mut registrations = Registrations::new();
        let controller = FakeController {
            route_error: Some(IoError(SERVICE_PERMISSION_DENIED)),
            ..FakeController::default()
        };
        assert_eq!(
            registrations.request(
                17,
                4,
                Request::new(Operation::Subscribe, 48, 0, 0),
                &controller
            ),
            Some(Response::error(48, INTERRUPT_PROTOCOL_DENIED))
        );
        let controller = FakeController::default();
        subscribed(&mut registrations, &controller);
        registrations.request(17, 4, Request::new(Operation::Wait, 48, 1, 0), &controller);
        registrations.record(Event::new(48, 9, 100, 110));
        registrations.delivery(17);
        let failing = FakeController {
            rearm_error: Some(IoError(SERVICE_MAP_FAILED)),
            ..FakeController::default()
        };
        let ack = Request::new(Operation::Acknowledge, 48, 1, 9);
        assert_eq!(
            registrations.request(17, 4, ack, &failing),
            Some(Response::error(48, INTERRUPT_PROTOCOL_INVALID))
        );
        assert_eq!(
            registrations.request(17, 4, ack, &controller),
            Some(Response::acknowledged(ack))
        );
    }
}
