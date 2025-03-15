# Setup

- `linux/` : source code for linux 6.1. DO NOT modify! should be copied to each assignment's directory for modification.
	- kernel source code already built with :
		```
		make ARCH=arm64 defconfig
		make ARCH=arm64 -j$(nproc) 
		```
		
