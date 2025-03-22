# Setup

- `linux/` : source code for linux 6.1. DO NOT modify! should be copied to each assignment's directory for modification.
	- REMEMBER to add copied `linux/` into `.gitignore` whenever you copied it. Only the root directory should contain a full linux source code.
		
## Shared Directory

The default arguments for shared directory is:

```
-virtfs local,path=$SHARED_DIR,mount_tag=shared,security_
model=passthrough,readonly
```

To equip guest OS with write permission of the shared directory, modify your qemu arguments to:

```
-virtfs local,path=$SHARED_DIR,mount_tag=shared,security_
model=mapped
```

- Disable read-only
- Use mapped model to let guest OS write data into shared directory.