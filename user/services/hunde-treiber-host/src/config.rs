use alloc::{
    string::{String, ToString},
    vec::Vec,
};
use hunde::{
    abi::IoError,
    protocol::{IO_INVALID_PATH, IO_REGISTER_FAILED},
    wuff::BundledDriver,
};

pub(crate) struct Definition {
    pub(crate) name: String,
    pub(crate) image: BundledDriver,
    pub(crate) device_id: Option<u32>,
}

impl Definition {
    fn new(name: &str, image: BundledDriver, device_id: Option<u32>) -> Self {
        Self {
            name: name.to_string(),
            image,
            device_id,
        }
    }
}

pub(crate) fn parse(bytes: &[u8]) -> Result<Vec<Definition>, IoError> {
    if bytes.len() > 16 * 1024 {
        return Err(IoError(IO_INVALID_PATH));
    }
    let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
    let mut definitions: Vec<Definition> = Vec::new();
    for line in text.lines() {
        let mut fields = line
            .split('#')
            .next()
            .unwrap_or_default()
            .split_ascii_whitespace();
        let device_id = match fields.next() {
            Some("polled-driver") => None,
            Some("virtio-driver") => Some(
                fields
                    .next()
                    .and_then(|value| value.parse::<u32>().ok())
                    .filter(|value| *value != 0)
                    .ok_or(IoError(IO_INVALID_PATH))?,
            ),
            _ => continue,
        };
        let name = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
        let source = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
        if fields.next().is_some()
            || name.is_empty()
            || name.len() > 253
            || !name
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        {
            return Err(IoError(IO_INVALID_PATH));
        }
        if definitions.iter().any(|definition| {
            definition.name == name || (device_id.is_some() && definition.device_id == device_id)
        }) || [
            "hund-unterbrict",
            "hunde-puffer",
            "hund-io-system",
            "hund-dateisystem",
        ]
        .contains(&name)
        {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        definitions.push(Definition::new(
            name,
            BundledDriver::find(source)?,
            device_id,
        ));
    }
    Ok(definitions)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_explicit_approved_poll_only_images_are_selected() {
        let definitions = parse(b"service network /sbin/components/network.wuff\npolled-driver cable /sbin/components/net_dummy.wuff\n").unwrap();
        assert_eq!(definitions.len(), 1);
        assert_eq!(definitions[0].name, "cable");
        assert!(parse(b"polled-driver cable /tmp/driver.wuff").is_err());
        assert!(parse(b"polled-driver cable /sbin/components/network.wuff").is_err());
        assert!(parse(b"polled-driver bad/name /sbin/components/net_dummy.wuff").is_err());
        assert!(parse(b"polled-driver hunde-puffer /sbin/components/net_dummy.wuff").is_err());
        assert!(parse(b"polled-driver cable /sbin/components/net_dummy.wuff extra").is_err());
        assert!(parse(b"polled-driver cable /sbin/components/net_dummy.wuff\npolled-driver cable /sbin/components/net_dummy.wuff").is_err());
    }

    #[test]
    fn virtio_instances_are_selected_by_transport_id_and_approved_manifest_image() {
        let definitions =
            parse(b"virtio-driver 1 ethernet /sbin/components/virtio_net.wuff").unwrap();
        assert_eq!(definitions.len(), 1);
        assert_eq!(definitions[0].name, "ethernet");
        assert_eq!(definitions[0].device_id, Some(1));
        for source in [
            "virtio-driver 0 ethernet /sbin/components/virtio_net.wuff",
            "virtio-driver nope ethernet /sbin/components/virtio_net.wuff",
            "virtio-driver 1 ethernet /sbin/components/network.wuff",
            "virtio-driver 1 ethernet /tmp/network.wuff",
            "virtio-driver 1 ethernet /sbin/components/virtio_net.wuff extra",
        ] {
            assert!(parse(source.as_bytes()).is_err(), "{source}");
        }
        assert!(parse(b"virtio-driver 1 ethernet /sbin/components/virtio_net.wuff\npolled-driver ethernet /sbin/components/net_dummy.wuff").is_err());
        assert!(parse(b"virtio-driver 1 ethernet /sbin/components/virtio_net.wuff\nvirtio-driver 1 other /sbin/components/virtio_net.wuff").is_err());
    }
}
