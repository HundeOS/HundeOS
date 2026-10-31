use hunde::abi::IoError;
use hunde::protocol::SERVICE_INVALID_BUFFER;

pub const QUEUE_SIZE: usize = 8;
pub const QUEUE_BYTES: usize = 8192;
pub const DESCRIPTOR_BYTES: usize = 16;
pub const USED_OFFSET: usize = 4096;

const NEXT: u16 = 1;
const WRITE: u16 = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Descriptor {
    address: u64,
    length: u32,
    flags: u16,
    next: u16,
}

impl Descriptor {
    fn new(address: u64, length: u32, flags: u16, next: u16) -> Self {
        Self {
            address,
            length,
            flags,
            next,
        }
    }

    pub fn bytes(self) -> [u8; DESCRIPTOR_BYTES] {
        let mut bytes = [0; DESCRIPTOR_BYTES];
        bytes[..8].copy_from_slice(&self.address.to_le_bytes());
        bytes[8..12].copy_from_slice(&self.length.to_le_bytes());
        bytes[12..14].copy_from_slice(&self.flags.to_le_bytes());
        bytes[14..16].copy_from_slice(&self.next.to_le_bytes());
        bytes
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    Read,
    Write,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Submission {
    pub descriptors: [Descriptor; 3],
    pub header: [u8; 16],
    pub available_slot: usize,
    pub available_index: u16,
}

pub struct Queue {
    next_available: u16,
    last_used: u16,
}

impl Queue {
    pub const fn new(last_used: u16) -> Self {
        Self {
            next_available: 0,
            last_used,
        }
    }

    pub fn prepare(
        &self,
        direction: Direction,
        sector: u64,
        length: usize,
        header_address: u64,
        data_address: u64,
        status_address: u64,
    ) -> Result<Submission, IoError> {
        let length = u32::try_from(length).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        if length == 0 || !length.is_multiple_of(512) {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let mut header = [0; 16];
        let operation: u32 = match direction {
            Direction::Read => 0,
            Direction::Write => 1,
        };
        header[..4].copy_from_slice(&operation.to_le_bytes());
        header[8..].copy_from_slice(&sector.to_le_bytes());
        let descriptors = [
            Descriptor::new(header_address, 16, NEXT, 1),
            Descriptor::new(
                data_address,
                length,
                NEXT | if direction == Direction::Read {
                    WRITE
                } else {
                    0
                },
                2,
            ),
            Descriptor::new(status_address, 1, WRITE, 0),
        ];
        let available_slot = usize::from(self.next_available) % QUEUE_SIZE;
        Ok(Submission {
            descriptors,
            header,
            available_slot,
            available_index: self.next_available.wrapping_add(1),
        })
    }

    pub fn publish(&mut self) {
        self.next_available = self.next_available.wrapping_add(1);
    }

    pub fn complete(&mut self, used_index: u16) -> bool {
        if used_index == self.last_used {
            return false;
        }
        self.last_used = used_index;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_submission_writes_only_data_and_status_descriptors() {
        let mut queue = Queue::new(0);
        let submission = queue
            .prepare(Direction::Read, 42, 1024, 0x1000, 0x2000, 0x3000)
            .unwrap();
        assert_eq!(submission.header[..4], 0_u32.to_le_bytes());
        assert_eq!(submission.header[8..], 42_u64.to_le_bytes());
        assert_eq!(
            submission.descriptors[0].bytes()[12..14],
            NEXT.to_le_bytes()
        );
        assert_eq!(
            submission.descriptors[1].bytes()[12..14],
            (NEXT | WRITE).to_le_bytes()
        );
        assert_eq!(
            submission.descriptors[2].bytes()[12..14],
            WRITE.to_le_bytes()
        );
        assert_eq!(submission.available_slot, 0);
        assert_eq!(submission.available_index, 1);
        assert_eq!(queue.next_available, 0);
        queue.publish();
        assert_eq!(queue.next_available, 1);
        assert!(!queue.complete(0));
        assert!(queue.complete(1));
    }

    #[test]
    fn write_submission_keeps_data_read_only_and_rejects_bad_lengths() {
        let queue = Queue::new(7);
        assert!(queue.prepare(Direction::Write, 0, 0, 1, 2, 3).is_err());
        assert!(queue.prepare(Direction::Write, 0, 513, 1, 2, 3).is_err());
        let submission = queue.prepare(Direction::Write, 2, 512, 1, 2, 3).unwrap();
        assert_eq!(submission.header[..4], 1_u32.to_le_bytes());
        assert_eq!(
            submission.descriptors[1].bytes()[12..14],
            NEXT.to_le_bytes()
        );
        assert_eq!(submission.available_index, 1);
        assert_eq!(queue.last_used, 7);
    }

    #[test]
    fn available_ring_wraps_without_losing_sequence() {
        let mut queue = Queue::new(u16::MAX);
        queue.next_available = u16::MAX;
        let submission = queue.prepare(Direction::Read, 0, 512, 1, 2, 3).unwrap();
        assert_eq!(
            submission.available_slot,
            usize::from(u16::MAX) % QUEUE_SIZE
        );
        assert_eq!(submission.available_index, 0);
        assert!(queue.complete(0));
    }
}
