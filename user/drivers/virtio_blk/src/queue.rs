use hunde::abi::IoError;
use hunde::protocol::SERVICE_INVALID_BUFFER;
use hunde::virtio::{Descriptor, NEXT, WRITE};

pub const QUEUE_SIZE: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    Read,
    Write,
}

pub struct Submission {
    pub descriptors: [Descriptor; 3],
    pub header: [u8; 16],
}

impl Submission {
    pub fn new(
        direction: Direction,
        sector: u64,
        length: usize,
        header_address: u64,
        data_address: u64,
        status_address: u64,
    ) -> Result<Self, IoError> {
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
        Ok(Self {
            header,
            descriptors: [
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
            ],
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn read_submission_writes_only_data_and_status_descriptors() {
        let submission =
            Submission::new(Direction::Read, 42, 1024, 0x1000, 0x2000, 0x3000).unwrap();
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
    }
    #[test]
    fn write_submission_keeps_data_read_only_and_rejects_bad_lengths() {
        assert!(Submission::new(Direction::Write, 0, 0, 1, 2, 3).is_err());
        assert!(Submission::new(Direction::Write, 0, 513, 1, 2, 3).is_err());
        let submission = Submission::new(Direction::Write, 2, 512, 1, 2, 3).unwrap();
        assert_eq!(submission.header[..4], 1_u32.to_le_bytes());
        assert_eq!(
            submission.descriptors[1].bytes()[12..14],
            NEXT.to_le_bytes()
        );
    }
}
