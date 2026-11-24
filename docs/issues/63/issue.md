<!-- GitHub issue #63: MediaTek error (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/63 -->

### What happens

I've tried everything, but it's not working. I think it's because of my processor; they should make it compatible with all MediaTek processors

### Steps to reproduce

I've tried everything, but it's not working. I think it's because of my processor; they should make it compatible with all MediaTek processors

### Version

v0.8.3 (b24f7da)

### Game version

NSMBU + NSLU (USA)

### Platform

Android

### Hardware

8 GB

### Logs

I've tried everything, but it's not working. I think it's because of my processor; they should make it compatible with all MediaTek processors

### From the app

The Vulkan renderer could not start.

The graphics driver does not support the Vulkan features the game needs: Vulkan 1.3, or Vulkan 1.1 / 1.2 with the VK_KHR_dynamic_rendering extension.

Mali-G57 MC2: Vulkan 1.1.177, driver 32.1.0; VK_KHR_dynamic_rendering missing

Install the newest graphics driver for this GPU (on Linux: an up-to-date Mesa, or the vendor's driver) and start the game again. If no newer driver exists, this GPU cannot run the game's Vulkan renderer.
