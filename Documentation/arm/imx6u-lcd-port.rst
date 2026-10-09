IM356 LCD port
==============

Scope and source
----------------

The supplier archive named imx6dl-yocto-6.6.3_patch.gz is a tar archive of
Yocto sources, not a unified diff. The 1080p reference is
imx6qdl-sabresd-1920x1080-spwg.dtsi under the supplier linux-imx 6.6.3 tree.
The archive also contains other resolutions and JEIDA configurations; these
are alternatives, not settings to combine with the 1080p SPWG configuration.

This port retains the existing auo,p294ivn02-0 panel selection and uses the
supplier's 1080p SPWG timing: 152942400 Hz, 1920x1080, horizontal front porch
40, sync 60, back porch 220, vertical front porch 7, sync 10, back porch 21.
DRM represents the pixel clock in kHz, so the mode uses 152942 kHz, giving
approximately 60 Hz. Confirm the installed panel model and its timing limits
before deployment: these are supplier board timings, not independently
verified AUO datasheet values. The supplier does not specify sync polarities;
the existing panel polarities and power sequencing delays are retained.

The display path is IPU1 -> DRM LDB -> LVDS panel, using both LVDS links for
one display. Supplier split-mode becomes fsl,dual-channel in the DRM LDB
driver. Supplier spwg/24-bit becomes vesa-24 / RGB888 SPWG. The legacy MXC
framebuffer drivers remain disabled to avoid sharing the IPU with DRM.
No supplier changes to unrelated peripheral drivers are imported.

Schematic sheet 12 connects GPIO3_IO28 through Q11/Q10 to the LVDS power
switch, GPIO1_IO16 to BL_ON, and PWM4 on SD1_CMD through Q8 to BL_ADJ.
The power and backlight enables are active high. Q8 inverts the PWM, so the
backlight uses PWM_POLARITY_INVERTED and a 500000 ns period (2 kHz), matching
the supplier. Default brightness is 80 percent (index 8 in the 11-entry
brightness table); userspace brightness values are 0 through 10.

The existing panel-vdd declaration is 12 V. The control-board schematic
labels its source PANEL1_VCC but does not specify its voltage. Check the
interface-board supply and panel requirements before powering the display.
GPIO control does not generate or regulate this voltage.

Build
-----

Use a separate build directory and the supplied SDK::

  source /opt/imx6ull-pcu/6.12-styhead/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi
  unset CFLAGS CPPFLAGS LDFLAGS
  make O=/tmp/imx6-lcd-port-build ARCH=arm CROSS_COMPILE="$TARGET_PREFIX" HOSTCC=gcc imx_v6_v7_defconfig imx6u-lcd.config
  make -j4 O=/tmp/imx6-lcd-port-build ARCH=arm CROSS_COMPILE="$TARGET_PREFIX" HOSTCC=gcc zImage nxp/imx/imx6dl-imx6u-lcd.dtb

Deploy both the rebuilt kernel and its matching imx6dl-imx6u-lcd.dtb. The
timing is in panel-simple, so updating only the DTB does not update it.
Set the bootloader's DTB selection accordingly. Do not rely on legacy
video=mxcfb arguments to select a mode on the DRM display path.

Validation and board checks
---------------------------

The target DTB and panel-simple, imx-ldb, ipuv3-crtc, pwm_bl and pwm-imx27
objects were cross-compiled with the SDK above. The resolved configuration
enables DRM_IMX_IPUV3, DRM_IMX_LDB, DRM_PANEL_SIMPLE and BACKLIGHT_PWM.
This is build validation, not an on-board display test or a full kernel build.

After boot, inspect the display state::

  dmesg | grep -Ei 'drm|ipu|ldb|panel|backlight|pwm|defer'
  cat /sys/class/drm/card*-LVDS-*/status
  cat /sys/class/drm/card*-LVDS-*/modes
  cat /sys/class/backlight/backlight-lvds/{brightness,max_brightness,bl_power}

Check for a connected LVDS connector with a 1920x1080 mode. Use modetest
with the imx-drm driver to enumerate connectors and display a test pattern;
stop any compositor before a modesetting test. Verify colors, full-screen
geometry and brightness changes, then test blank/unblank and suspend/resume.

For a dark display, distinguish panel supply, BL_ON and PWM output from LVDS
link problems. Measure PANEL1_VCC/LVDS1_VCC and check both LVDS clocks. For
incorrect colors, verify SPWG versus JEIDA on the actual panel. A different
resolution or a single-link panel needs its own matching timing and LDB mode.

Boot failure fixes
------------------

The AUO panel node is named panel, not panel-lvds. With a panel-lvds node
name, the platform bus can match the generic panel-lvds driver by name even
when its OF compatible does not match. That driver requires panel-timing,
whereas the AUO compatible gets its timing from panel-simple. A log message
from the generic driver about missing panel-timing indicates an incorrect
binding, not a reason to add generic timings to the AUO node.

The IPUv3 CRTC bind callback installs ipuv3_drm_mode_config_funcs and
ipuv3_drm_mode_config_helpers before the DRM master registers its fbdev
client. Without the former, drm_mode_validate_driver dereferences a NULL
mode_config.funcs pointer; the latter selects the IPU-specific atomic commit
sequence. Rebuild the kernel as well as the DTB for these fixes.

On the next board boot, confirm that the generic panel timing error and
drm_mode_validate_driver Oops are gone, that imx-drm registers successfully,
and that the LVDS connector exposes the intended mode. The Vivante render
device can be card0, so do not assume that the display is always card0.