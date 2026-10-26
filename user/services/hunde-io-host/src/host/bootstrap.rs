use super::config::ServiceDefinition;
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::protocol::{IO_INVALID_MOUNT_POINT, IO_INVALID_PATH};
use hunde::wuff::BundledComponent;

const PATH: &[u8] = b"/sbin/config/bootstrap.fstab";
const LIMIT: usize = 16 * 1024;

pub(crate) struct Mount {
    pub(crate) service: ServiceDefinition,
}

impl Mount {
    fn new(path: &str, image: BundledComponent) -> Self {
        Self {
            service: ServiceDefinition::new(path, image),
        }
    }

    pub(crate) fn load() -> Result<Vec<Self>, IoError> {
        Self::parse(&hunde::bootstrap::read_file(PATH, LIMIT)?)
    }

    fn parse(bytes: &[u8]) -> Result<Vec<Self>, IoError> {
        if bytes.len() > LIMIT {
            return Err(IoError(IO_INVALID_PATH));
        }
        let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
        let mut mounts = Vec::<Self>::new();
        for line in text.lines() {
            let fields: Vec<_> = line
                .split('#')
                .next()
                .unwrap_or_default()
                .split_ascii_whitespace()
                .collect();
            if fields.is_empty() {
                continue;
            }
            if fields.len() != 4
                || fields[0] != "device"
                || fields[1] != "-"
                || fields[3].len() >= 256
                || !crate::filesystem::control::canonical(fields[3])
                || mounts
                    .iter()
                    .any(|mount| mount.service.name.as_str() == fields[3])
            {
                return Err(IoError(IO_INVALID_MOUNT_POINT));
            }
            mounts.push(Self::new(fields[3], BundledComponent::find(fields[2])?));
        }
        if mounts.is_empty() {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        Ok(mounts)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const DEFAULT: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../config/bootstrap.fstab"
    ));

    #[test]
    fn initial_mounts_are_approved_wuffs_in_manifest_order() {
        let mounts = Mount::parse(DEFAULT).unwrap();
        assert_eq!(
            mounts
                .iter()
                .map(|mount| mount.service.name.as_str())
                .collect::<Vec<_>>(),
            ["/sbin", "/boot", "/dev", "/tmp", "/proc"]
        );
        assert_eq!(
            mounts[0].service.driver.path(),
            "/sbin/components/embedfs.wuff"
        );
    }

    #[test]
    fn invalid_mounts_duplicates_and_wrong_abis_are_rejected() {
        for row in [
            "device - /sbin/components/embedfs.wuff /sbin",
            "device - /sbin/components/ext2fs.wuff /other",
            "device - /tmp/unknown.wuff /other",
            "device - /sbin/components/memfs.wuff /bad/../path",
            "device disk /sbin/components/memfs.wuff /other",
            "device - /sbin/components/memfs.wuff /other extra",
        ] {
            let mut bytes = DEFAULT.to_vec();
            bytes.extend_from_slice(row.as_bytes());
            assert!(Mount::parse(&bytes).is_err(), "{row}");
        }
        assert!(Mount::parse(b"").is_err());
    }
}
