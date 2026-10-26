use crate::host::Subsystem;
use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Operation, ProcessId, Redirect, Reply, Request,
    SessionId, UserId,
};
use hunde::protocol::{
    IO_INVALID_MOUNT_POINT, IO_INVALID_PATH, IO_MODE_TYPE_DIR, IO_OPERATION_NOT_SUPPORTED,
    IO_PERMISSION_DENIED, IO_REGISTER_FAILED, IO_UNKNOWN_HANDLE,
};
use hunde::service::{ReadinessMessage, ServiceName};

struct Mount {
    path: String,
    console: bool,
}

impl Mount {
    fn parse(bytes: &[u8]) -> Result<Self, IoError> {
        let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_MOUNT_POINT))?;
        let mut fields = text
            .split('#')
            .next()
            .unwrap_or("")
            .split_ascii_whitespace();
        if fields.next() != Some("device") || fields.next().is_none() {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        let format = fields.next().ok_or(IoError(IO_INVALID_MOUNT_POINT))?;
        let path = fields.next().ok_or(IoError(IO_INVALID_MOUNT_POINT))?;
        if fields.next().is_some()
            || !super::control::canonical(path)
            || format.contains('\0')
            || bytes.len() >= 256
            || (format == "console" && !crate::console::terminal_path(path))
        {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        Ok(Self {
            path: path.to_string(),
            console: format == "console",
        })
    }

    fn contains(&self, path: &[u8]) -> bool {
        if self.console {
            return path == self.path.as_bytes();
        }
        path.starts_with(self.path.as_bytes())
            && (self.path == "/"
                || path.len() == self.path.len()
                || path.get(self.path.len()) == Some(&b'/'))
    }

    fn relative<'path>(&self, path: &'path [u8]) -> &'path [u8] {
        if self.path == "/" {
            path
        } else {
            let relative = &path[self.path.len()..];
            if relative.is_empty() { b"/" } else { relative }
        }
    }
}

struct MountTree {
    root: Option<Mount>,
    mounted: Vec<Mount>,
}

impl MountTree {
    fn new() -> Self {
        Self {
            root: None,
            mounted: Vec::new(),
        }
    }

    fn mounts(&self) -> impl Iterator<Item = &Mount> {
        self.root.iter().chain(self.mounted.iter())
    }

    fn add(&mut self, mount: Mount) -> Result<(), IoError> {
        if self.mounts().any(|existing| existing.path == mount.path) {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        if self.mounts().count() >= 16 {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        if mount.path == "/" {
            self.root = Some(mount);
        } else {
            self.mounted.push(mount);
        }
        Ok(())
    }

    fn select(&self, path: &[u8]) -> Result<&Mount, IoError> {
        self.mounts()
            .filter(|mount| mount.contains(path))
            .max_by_key(|mount| mount.path.len())
            .ok_or(IoError(IO_INVALID_PATH))
    }

    fn children<'tree>(&'tree self, path: &[u8]) -> Vec<&'tree [u8]> {
        let mut children = Vec::new();
        for mount in self.mounts() {
            let mounted = mount.path.as_bytes();
            if mounted == path || !mounted.starts_with(path) {
                continue;
            }
            let start = if path.ends_with(b"/") {
                path.len()
            } else {
                if mounted.get(path.len()) != Some(&b'/') {
                    continue;
                }
                path.len() + 1
            };
            if let Some(child) = mounted
                .get(start..)
                .and_then(|rest| rest.split(|byte| *byte == b'/').next())
                && !child.is_empty()
                && !children.contains(&child)
            {
                children.push(child);
            }
        }
        children
    }

    fn route(
        &self,
        path: &[u8],
        target: &[u8],
        offset: u64,
        listing: bool,
    ) -> Result<Reply, IoError> {
        validate_path(path)?;
        if !target.is_empty() {
            validate_path(target)?;
        }
        let mut child_count = 0;
        if listing {
            let children = self.children(path);
            child_count = children.len() as u64;
            if offset < child_count {
                let entries = children
                    .into_iter()
                    .skip(offset as usize)
                    .take(3)
                    .map(|name| {
                        DirectoryEntry::new(
                            FileInfo::new(
                                if self.mounts().any(|mount| {
                                    mount.console
                                        && mount.path.as_bytes().strip_prefix(path).is_some_and(
                                            |suffix| {
                                                suffix.strip_prefix(b"/").unwrap_or(suffix) == name
                                            },
                                        )
                                }) {
                                    hunde::protocol::IO_MODE_TYPE_CHAR as u32 | 0o666
                                } else {
                                    IO_MODE_TYPE_DIR as u32 | 0o755
                                },
                                Credentials::new(UserId(0), GroupId(0)),
                                0,
                                0,
                            ),
                            name,
                        )
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                return Ok(Reply::Directory(entries));
            }
        }
        let mount = self.select(path)?;
        let relative_target = if target.is_empty() {
            target
        } else {
            if self.select(target)?.path != mount.path {
                return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
            }
            mount.relative(target)
        };
        Ok(Reply::Redirect(Redirect::new(
            &mount.path,
            mount.relative(path),
            relative_target,
            offset - child_count,
        )?))
    }
}

fn validate_path(path: &[u8]) -> Result<(), IoError> {
    if !path.starts_with(b"/")
        || path.len() >= 256
        || path.contains(&0)
        || path
            .split(|byte| *byte == b'/')
            .any(|part| part == b"." || part == b"..")
    {
        return Err(IoError(IO_INVALID_PATH));
    }
    Ok(())
}

#[derive(Clone)]
pub(crate) struct RootFilesystem(Rc<RefCell<MountTree>>);

impl RootFilesystem {
    pub(crate) fn new() -> Self {
        Self(Rc::new(RefCell::new(MountTree::new())))
    }

    pub(crate) fn mount(&self, image: &str, path: &str) -> Result<(), IoError> {
        let config = alloc::format!("device - {image} {path}");
        self.0.borrow_mut().add(Mount::parse(config.as_bytes())?)
    }
}

pub(crate) struct Namespace<Authority> {
    tree: RootFilesystem,
    authorize: Authority,
}

impl<Authority> Namespace<Authority> {
    pub(crate) fn new(tree: RootFilesystem, authorize: Authority) -> Self {
        Self { tree, authorize }
    }
}

impl<Authority: Fn(ProcessId) -> Result<(), IoError> + 'static> IoComponent
    for Namespace<Authority>
{
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let reply = match request.operation {
            Operation::Mount(config) => {
                (self.authorize)(request.pid)?;
                self.tree.0.borrow_mut().add(Mount::parse(config)?)?;
                Reply::Value(0)
            }
            Operation::Open(path, _)
            | Operation::FileInfo(path)
            | Operation::Mkdir(path)
            | Operation::Remove(path, _) => self.tree.0.borrow().route(path, b"", 0, false)?,
            Operation::ListDirectory(path, offset) => {
                self.tree.0.borrow().route(path, b"", offset, true)?
            }
            Operation::Link { source, target } => {
                validate_path(target)?;
                self.tree.0.borrow().route(source, target, 0, false)?
            }
            _ => return Err(IoError(IO_UNKNOWN_HANDLE)),
        };
        Ok(Some(reply))
    }

    fn disconnect(&mut self, _session: SessionId) -> Result<(), IoError> {
        Ok(())
    }
}

impl<Authority: Fn(ProcessId) -> Result<(), IoError> + 'static> Subsystem for Namespace<Authority> {
    fn endpoint(&self) -> ServiceName {
        ServiceName::new("hund-dateisystem")
    }
    fn ready_message(&self) -> ReadinessMessage {
        ReadinessMessage::new("Rust filesystem namespace ready")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::Handle;

    fn mount(tree: &mut MountTree, path: &str) {
        tree.add(Mount::parse(alloc::format!("device - ext2 {path}").as_bytes()).unwrap())
            .unwrap();
    }

    #[test]
    fn console_nodes_are_listed_as_character_devices_and_do_not_capture_subpaths() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/");
        mount(&mut tree, "/dev");
        tree.add(Mount::parse(b"device uart_rx,uart_tx console /dev/tty0").unwrap())
            .unwrap();
        assert_eq!(tree.select(b"/dev/tty0").unwrap().path, "/dev/tty0");
        assert_eq!(tree.select(b"/dev/tty0/child").unwrap().path, "/dev");
        let Reply::Directory(entries) = tree.route(b"/dev", b"", 0, true).unwrap() else {
            panic!("Expected directory")
        };
        assert_eq!(entries[0].name(), b"tty0");
        assert_eq!(
            entries[0].info.mode & 0xf000,
            hunde::protocol::IO_MODE_TYPE_CHAR as u32
        );
        assert!(Mount::parse(b"device uart_rx,uart_tx console /tmp/tty0").is_err());
    }

    #[test]
    fn fat32_wuff_mount_rebases_paths_inside_the_host_namespace() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/");
        tree.add(Mount::parse(b"device /dev/virtio3 fat32.wuff /mnt/fat").unwrap())
            .unwrap();
        let selected = tree.select(b"/mnt/fat/HELLO.TXT").unwrap();
        assert_eq!(selected.path, "/mnt/fat");
        assert_eq!(selected.relative(b"/mnt/fat/HELLO.TXT"), b"/HELLO.TXT");
        assert_eq!(tree.select(b"/mnt/fatty/file").unwrap().path, "/");
    }

    fn redirect(reply: Reply) -> Redirect {
        match reply {
            Reply::Redirect(route) => route,
            _ => panic!("expected redirect"),
        }
    }

    #[test]
    fn bootstrap_overlays_work_before_root_and_survive_root_installation() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/sbin");
        mount(&mut tree, "/boot");
        mount(&mut tree, "/boot/config");
        assert!(tree.root.is_none());
        assert!(tree.select(b"/file").is_err());
        mount(&mut tree, "/");
        assert!(tree.root.is_some());
        assert_eq!(
            redirect(tree.route(b"/boot/config/fstab", b"", 0, false).unwrap()),
            Redirect::new("/boot/config", b"/fstab", b"", 0).unwrap()
        );
        assert_eq!(
            redirect(tree.route(b"/sbin/init", b"", 0, false).unwrap()).service,
            "/sbin"
        );
        assert_eq!(
            redirect(tree.route(b"/other", b"", 0, false).unwrap()).path,
            b"/other"
        );
        assert!(tree.add(Mount::parse(b"device - ext2 /").unwrap()).is_err());
    }

    #[test]
    fn routing_uses_component_boundaries_and_mount_root_paths() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/");
        mount(&mut tree, "/tmp");
        assert_eq!(
            redirect(tree.route(b"/tmp", b"", 0, false).unwrap()).path,
            b"/"
        );
        assert_eq!(
            redirect(tree.route(b"/tmpfile", b"", 0, false).unwrap()).service,
            "/"
        );
        for invalid in [
            b"relative".as_slice(),
            b"/tmp/../file",
            b"/./file",
            b"/nul\0name",
        ] {
            assert_eq!(
                tree.route(invalid, b"", 0, false),
                Err(IoError(IO_INVALID_PATH))
            );
        }
    }

    #[test]
    fn links_rewrite_both_paths_and_reject_cross_mount_targets() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/");
        mount(&mut tree, "/tmp");
        let route = redirect(tree.route(b"/tmp/from", b"/tmp/to", 0, false).unwrap());
        assert_eq!(
            (route.path, route.target),
            (b"/from".to_vec(), b"/to".to_vec())
        );
        assert_eq!(
            tree.route(b"/tmp/from", b"/to", 0, false),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
    }

    #[test]
    fn lists_child_mounts_then_rebases_backend_pagination() {
        let mut tree = MountTree::new();
        mount(&mut tree, "/");
        for path in ["/boot", "/boot/config", "/tmp", "/proc", "/dev"] {
            mount(&mut tree, path);
        }
        let Reply::Directory(first) = tree.route(b"/", b"", 0, true).unwrap() else {
            panic!("expected children")
        };
        assert_eq!(
            first.iter().map(DirectoryEntry::name).collect::<Vec<_>>(),
            [b"boot".as_slice(), b"tmp", b"proc"]
        );
        let Reply::Directory(last) = tree.route(b"/", b"", 3, true).unwrap() else {
            panic!("expected children")
        };
        assert_eq!(last[0].name(), b"dev");
        assert_eq!(redirect(tree.route(b"/", b"", 6, true).unwrap()).offset, 2);
        let Reply::Directory(nested) = tree.route(b"/boot/", b"", 0, true).unwrap() else {
            panic!("expected child")
        };
        assert_eq!(nested[0].name(), b"config");
    }

    #[test]
    fn mount_validation_authority_and_capacity_fail_without_mutation() {
        let tree = RootFilesystem::new();
        let mut namespace = Namespace::new(tree.clone(), |pid| {
            if pid == ProcessId(7) {
                Ok(())
            } else {
                Err(IoError(IO_PERMISSION_DENIED))
            }
        });
        let request = |pid| {
            Request::new(
                SessionId(1),
                ProcessId(pid),
                Handle(0),
                Operation::Mount(b"device - embedfs /sbin"),
            )
        };
        assert!(namespace.execute(request(0)).is_err());
        assert!(namespace.execute(request(8)).is_err());
        assert!(tree.0.borrow().select(b"/sbin").is_err());
        namespace.execute(request(7)).unwrap();
        assert!(tree.0.borrow().select(b"/sbin").is_ok());
        for config in [
            b"device - bad\0format /bad".as_slice(),
            b"device - ext2 /bad/",
            b"device - ext2 /../bad",
            b"device - ext2 /bad extra",
        ] {
            assert!(Mount::parse(config).is_err());
        }
        for index in 0..15 {
            mount(&mut tree.0.borrow_mut(), &alloc::format!("/mount{index}"));
        }
        assert_eq!(
            tree.0
                .borrow_mut()
                .add(Mount::parse(b"device - ext2 /").unwrap()),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert!(tree.0.borrow().root.is_none());
    }
}
