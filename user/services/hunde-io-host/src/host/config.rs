use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::protocol::{IO_INVALID_PATH, IO_PROTOCOL_DATA_SIZE, IO_REGISTER_FAILED};
use hunde::service::ServiceName;
use hunde::wuff::{BundledComponent, BundledFilesystem};

pub(crate) const PATH: &[u8] = b"/sbin/config/io-host.conf";
const LIMIT: usize = 16 * 1024;

pub(crate) struct ServiceDefinition {
    pub(crate) name: ServiceName,
    pub(crate) driver: BundledComponent,
}

impl ServiceDefinition {
    pub(crate) fn new(name: &str, driver: BundledComponent) -> Self {
        Self {
            name: ServiceName::from_string(name.to_string()),
            driver,
        }
    }
}

struct FilesystemDefinition {
    name: String,
    image: BundledFilesystem,
}

impl FilesystemDefinition {
    fn new(name: &str, image: BundledFilesystem) -> Self {
        Self {
            name: name.to_string(),
            image,
        }
    }
}

#[derive(Clone)]
pub(crate) struct Filesystems(Rc<[FilesystemDefinition]>);

impl Filesystems {
    fn new(definitions: Vec<FilesystemDefinition>) -> Self {
        Self(definitions.into())
    }

    pub(crate) fn find(&self, name: &str) -> Option<BundledFilesystem> {
        self.0
            .iter()
            .find(|definition| definition.name == name)
            .map(|definition| definition.image)
    }
}

pub(crate) struct Configuration {
    pub(crate) services: Vec<ServiceDefinition>,
    pub(crate) filesystems: Filesystems,
}

impl Configuration {
    pub(crate) fn load() -> Result<Self, IoError> {
        Self::parse(&hunde::bootstrap::read_file(PATH, LIMIT)?)
    }

    pub(crate) fn parse(bytes: &[u8]) -> Result<Self, IoError> {
        if bytes.len() > LIMIT {
            return Err(IoError(IO_INVALID_PATH));
        }
        let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
        let mut services = Vec::<ServiceDefinition>::new();
        let mut filesystems = Vec::<FilesystemDefinition>::new();
        for line in text.lines() {
            let mut fields = line
                .split('#')
                .next()
                .unwrap_or_default()
                .split_ascii_whitespace();
            let Some(kind) = fields.next() else {
                continue;
            };
            let name = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
            let source = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
            if fields.next().is_some()
                || name.len() >= IO_PROTOCOL_DATA_SIZE as usize
                || name.contains('\0')
            {
                return Err(IoError(IO_INVALID_PATH));
            }
            match kind {
                "driver" => {}
                "service" => {
                    if (name.starts_with('/') && !crate::filesystem::control::canonical(name))
                        || (!name.starts_with('/') && !identifier(name))
                    {
                        return Err(IoError(IO_INVALID_PATH));
                    }
                    if services
                        .iter()
                        .any(|definition| definition.name.as_str() == name)
                        || [
                            "hund-dateisystem",
                            "hund-io-system",
                            "hunde-io-host.mounts",
                            "/sbin",
                        ]
                        .contains(&name)
                    {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    let driver = BundledComponent::find(source)?;
                    services.push(ServiceDefinition::new(name, driver));
                }
                "filesystem" => {
                    if !identifier(name) || ["console", "embedfs"].contains(&name) {
                        return Err(IoError(IO_INVALID_PATH));
                    }
                    if filesystems.iter().any(|definition| definition.name == name) {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    filesystems.push(FilesystemDefinition::new(
                        name,
                        BundledFilesystem::find(source)?,
                    ));
                }
                _ => return Err(IoError(IO_INVALID_PATH)),
            }
        }
        if services.is_empty() || filesystems.is_empty() {
            return Err(IoError(IO_INVALID_PATH));
        }
        Ok(Self {
            services,
            filesystems: Filesystems::new(filesystems),
        })
    }
}

fn identifier(name: &str) -> bool {
    !name.is_empty()
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte))
}

#[cfg(test)]
mod tests {
    use super::*;
    const DEFAULT: &[u8] = include_bytes!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../config/io-host.conf"
    ));

    #[test]
    fn bundled_manifest_preserves_service_names_and_filesystem_factories() {
        let config = Configuration::parse(DEFAULT).unwrap();
        assert_eq!(
            config
                .services
                .iter()
                .map(|service| service.name.as_str())
                .collect::<Vec<_>>(),
            ["socket_router", "hund-rohr"]
        );
        assert_eq!(
            config.filesystems.find("ext2.wuff").unwrap().path(),
            "/sbin/components/ext2fs.wuff"
        );
        assert_eq!(
            config.filesystems.find("fat32.wuff").unwrap().path(),
            "/sbin/components/fat32.wuff"
        );
        assert!(config.filesystems.find("unknown").is_none());
    }

    #[test]
    fn names_order_and_multiple_instances_are_configuration_not_host_policy() {
        let config = Configuration::parse(b"# test\nservice /scratch /sbin/components/memfs.wuff\nservice /other /sbin/components/memfs.wuff\nfilesystem custom /sbin/components/ext2fs.wuff # alias\n").unwrap();
        assert_eq!(config.services[0].name.as_str(), "/scratch");
        assert_eq!(config.services[1].name.as_str(), "/other");
        assert!(config.filesystems.find("custom").is_some());
        assert!(config.filesystems.find("ext2.wuff").is_none());
    }

    #[test]
    fn malformed_duplicates_untrusted_images_and_abi_mismatches_are_errors() {
        for line in [
            "unknown a b",
            "service a",
            "service a b extra",
            "builtin /dev unknown",
            "service /sbin /sbin/components/memfs.wuff",
            "service hund-io-system /sbin/components/socket.wuff",
            "service /tmp/../bad /sbin/components/memfs.wuff",
            "service /tmp\0bad /sbin/components/memfs.wuff",
            "service a /tmp/socket.wuff",
            "service a /sbin/components/unknown.wuff",
            "service a /sbin/components/ext2fs.wuff",
            "filesystem bad /sbin/components/socket.wuff",
            "filesystem ext2.wuff /sbin/components/ext2fs.wuff",
            "builtin /dev devfs",
        ] {
            let mut text = DEFAULT.to_vec();
            text.extend_from_slice(line.as_bytes());
            assert!(Configuration::parse(&text).is_err(), "{line}");
        }
        for text in [
            b"".as_slice(),
            b"# empty",
            b"\xff",
            &alloc::vec![b'x'; LIMIT + 1],
        ] {
            assert!(Configuration::parse(text).is_err());
        }
    }
}
