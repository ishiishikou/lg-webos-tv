# Investigation log

## 2026-09-29 / 2026-09-30

### Network / SSAP

- SSAP TLS WebSocket connection established.
- Public service list includes `tv`, `media.viewer`, `media.controls` etc.
- No public TS/stream/tuner export service was found.
- `media.viewer` is the TV-side media viewer (external media -> TV), not TV -> PC streaming.

### DTV metadata

`ssap://tv/getCurrentChannel` and `ssap://tv/getChannelProgramInfo` work.

Observed terrestrial DTV playback is provided by:

```text
playerService = com.webos.service.legacybroadcast
```

The terrestrial channel's `ipChanServerUrl` was empty.

### Screen capture

`ssap://tv/executeOneShot` returns an HTTPS image URI.

Observed behavior:

- 960x540 JPEG
- ~2.5 fps when repeatedly requested
- Includes decoded terrestrial broadcast video
- Also includes decoded HDD recording playback
- requested 1920x1080 was still returned as 960x540

A local MJPEG proof of concept was also tested successfully.

### USB HDD recording

The USB HDD is mounted inside webOS and can be read from Developer Mode SSH.

STR files show a 192-byte packet cycle. At offsets 4, 196, 388... the MPEG-TS sync byte `0x47` is visible.

The payload is scrambled. A simple test of every 16-byte sliding window in `.lg_dvr_dev` as an AES-128-ECB key did not recover PES headers, so `.lg_dvr_dev` is not trivially the recording key.

### Developer Mode / SSH

Official Developer Mode SSH is working as `prisoner`.

The product image does not ship `ls-monitor`, but LS2 permission files are readable and were used to enumerate private methods.

### getTSPath

Confirmed in the device's LS2 API permission metadata:

```text
com.webos.service.legacybroadcast.frontend/getTSPath
```

Calling it via public Luna bus fails because the method is private.

### Capture pipeline

`com.webos.service.videooutput/getStatus` shows DTV output from VDEC at roughly 29.97fps.

Relevant local components:

```text
libvtcapture.so
libdile_vt.so
libhalgal.so
vtCaptureTestSuite
```

The test suite contains one-shot and continuous-capture modes, and the capture stack supports YUV/RGB/JPEG/PNG/shared-memory output.

Starting it as the Developer Mode user fails at privileged LS2 registration rather than at the hardware capture layer.

### Device nodes

Relevant LG driver nodes include:

```text
/dev/lg/pvr0
/dev/lg/pvr0up0
/dev/lg/pvr0dn0
/dev/lg/sdec0
/dev/lg/te0
/dev/lg/demod0
/dev/lg/arib2
```

Direct read is not sufficient; userspace ioctl setup appears to be required.

### Security / publication hygiene

Do not commit:

- TV pairing client key
- Developer Mode passphrase
- LAN addresses
- MAC addresses
- account credentials
- raw files containing those values


### getTSPath reverse engineering

Static analysis of the TV's `libelement-frontend.so.1.0.0` corrected an earlier assumption about `getTSPath`.

`AdapterFrontend::getDemodConfig()` builds a request containing `tunerNo`, calls:

```text
luna://com.webos.service.legacybroadcast.frontend/getTSPath
```

and parses numeric response fields:

```text
portType
inputType
demodType
```

Therefore `getTSPath` does **not** appear to return a filesystem path/FIFO containing MPEG-TS. It supplies transport-stream hardware routing/configuration metadata used by the frontend adapter.

### vtCapture jail boundary

`libvtcapture.so` contains a literal reference to:

```text
/dev/video60
```

Sysfs identifies major 81/minor 6 as:

```text
vt-capture-dev
```

The kernel device exists, but the Developer Mode jail does not expose `/dev/video60`. Only selected V4L2 nodes such as the ADC and VDEC nodes are visible. The `prisoner` process has no effective Linux capabilities.

Direct `vtCapture_create()` and `vtCapture_createEx()` tests also fail during privileged Luna registration (`LSRegisterPubPriv FAILED`) before capture initialization.

This gives two independent boundaries for the high-FPS decoded-video route:

1. private/privileged Luna registration
2. hidden V4L2 capture device

### ALSA decoded-audio candidate

The Developer Mode account belongs to the `audio` group and can open several ALSA capture PCMs.

Most relevant:

```text
hw:1,2  MixerCapture      S32_LE, 2ch, 48000 Hz
hw:1,1  SpeakerFeedback   S32_LE, 8ch, 48000 Hz
hw:0,12 dsnoop capture    S16_LE, 2ch, 48000 Hz
```

Recording from `MixerCapture` succeeds as `prisoner`.

A 2-second recording made while the TV output was temporarily muted still contained a strong, nearly continuous PCM signal. The TV mute state was then restored. This strongly suggests that `MixerCapture` taps an internal pre-mute/pre-volume mix rather than microphone/speaker-acoustic feedback.

The next validation is to prove that this PCM tracks the current DTV/recording program audio.


### Program-audio validation and transport

A source-switch A/B test strengthened the `MixerCapture` identification:

```text
Live TV active:
  MixerCapture S32_LE stereo RMS: ~61.9M / ~61.9M

Developer Mode app foreground:
  MixerCapture RMS: 0 / 0
```

After the test, Live TV and the original channel were restored.

This indicates that `hw:1,2 (MixerCapture)` follows active media playback and is a practical decoded-program-audio tap.

A real-time transport probe also worked without root:

```sh
arecord -q -D hw:1,2 -f S32_LE -c 2 -r 48000 -t raw | base64
```

When run remotely through `ares-novacom`, base64 output streamed incrementally rather than only at process exit. Measured on the current setup:

- first PCM data: about 545 ms after startup
- sustained transport: approximately real-time
- 5 seconds: expected 1,920,000 bytes of decoded S32_LE stereo PCM

This makes audio substantially more promising than the current JPEG video path under official Developer Mode.


### High-FPS path narrowing

Additional official-Developer-Mode tests ruled out several candidate paths.

#### Temporary Developer Mode service

A minimal app/service was installed with `ares-install`, then removed after testing.

Calls from the service returned:

```text
capture/getCapability     -> Denied method call
capture/createHandle      -> Denied method call
getTSPath                 -> Denied method call
```

The service sandbox had neither `/dev/video20` nor `/dev/video60`.

#### Dedicated VT device is fully hidden

Sysfs exposes the dedicated capture device as major 81/minor 6 (`vt-capture-dev`), but scanning the entire Developer Mode `/dev` tree found no node or alias with 81:6.

#### HAL_GAL / libgm

`/dev/gfx` is visible and can be opened by `prisoner`, but `HAL_GAL_Init()` segfaulted when called from the Developer Mode shell. `libgm.so` is absent on this model.

#### Public screenshot path cannot be scaled

Multiple concurrent SSAP `executeOneShot` calls do not generate independent frames. 2/4/8 concurrent calls returned one unique JPEG per batch and total latency increased with concurrency.

SSAP format requests for JPG, PNG, RGB, and YUV422 all returned the same JPEG resource. The public API adapter therefore normalizes screenshot output rather than exposing raw capture buffers.

This leaves the dedicated VT capture path as the main high-FPS route, but it remains behind both LS2 privilege and the hidden `/dev/video60` device boundary.


### LG V4L2 capture ABI identified

Public LG-derived V4L2 extension headers were located that match the capture IDs observed in runtime logs.

Important capture controls include:

```text
V4L2_CID_EXT_CAPTURE_CAPABILITY_INFO
V4L2_CID_EXT_CAPTURE_PLANE_INFO
V4L2_CID_EXT_CAPTURE_VIDEO_WIN_INFO
V4L2_CID_EXT_CAPTURE_PLANE_PROP
V4L2_CID_EXT_CAPTURE_FREEZE_MODE
V4L2_CID_EXT_CAPTURE_DONE_USER_PROCESSING
V4L2_CID_EXT_CAPTURE_PHYSICAL_MEMORY_INFO
V4L2_CID_EXT_CAPTURE_OUTPUT_FRAMERATE
V4L2_CID_EXT_CAPTURE_DIVIDE_FRAMERATE
```

The capture location enum contains:

```text
SCALER_INPUT
SCALER_OUTPUT
DISPLAY_OUTPUT
BLENDED_OUTPUT
OSD_OUTPUT
```

and `v4l2_ext_capture_plane_prop` carries the selected location, capture rectangle, and buffer count.

This aligns with both the on-device `libvtcapture` strings and third-party runtime logs showing `V4L2_CID_EXT_CAPTURE_PLANE_PROP` on `video60`.

Therefore the high-FPS pipeline is no longer ambiguous at the driver-API level. The unresolved part is access to the dedicated 81:6 VT capture node from official Developer Mode.


### Horizontal sweep of alternative non-root paths

A broad read-only sweep was performed to look for overlooked high-FPS routes outside the previously investigated SSAP/vtCapture/PVR paths.

#### UPnP / DLNA

The TV runs `upnpd` and `umediaserver` as root.

The installed UPnP package contains DMC/DMR Luna-bus libraries:

```text
libdmcplus_lunabus.so
libdmrplus_lunabus.so
```

No MediaServer / ContentDirectory implementation was found in the visible package/service metadata. This makes a built-in "Live TV as DLNA media server" path unlikely.

#### Second-screen gateway

`com.webos.service.secondscreen.gateway` exists and exposes paired-device, service-list and app2app APIs, but calls from the Developer Mode user are denied.

#### Framebuffer

The Developer Mode user is in the `video` group and can open:

```text
/dev/fb0  osd0_fb
/dev/fb1  osd1_fb
/dev/fb2  osd2_fb
/dev/fb3  crsr_fb
```

However actual framebuffer reads fail with `EPERM`. These are OSD/cursor framebuffers, not an obvious decoded-video plane.

#### DVR/databroadcast named pipe

`legacybroadcast.databroadcast/getNamedPipePath` exists, but it is grouped with DSM-CC/BML/data-broadcast APIs rather than DVR playback. It is therefore not currently considered a likely full A/V stream endpoint.

#### uMediaServer / Starfish pipeline

The TV runs `umediaserver` and `starfish-media-pipeline`. A private `com.webos.starfish-record-pipeline` role also exists.

This is a genuine remaining candidate and has not yet been fully ruled out, although the visible role only communicates with dynamically-created pipeline controller services and does not expose a general public screen-recording API.

#### Video thumbnailer / ACR / remote diagnostics

Root services/processes discovered include:

```text
videothumbnailer
acr2
remotediag
captureservice
```

`videothumbnailer` is registered on both public and private LS2 buses with broad inbound/outbound role permissions, but no public method schema was found.

The ACR API metadata visibly exposes `startAcr` callbacks but no raw-frame/stream export method.

No evidence was found that `remotediag` exposes a reusable local video stream.

#### Remaining overlooked-path candidates after one horizontal pass

The two most technically plausible non-root branches still worth focused analysis are:

1. LG-specific ioctls/control paths on the Developer Mode-visible `/dev/video20` VDEC node.
2. The private `starfish-record-pipeline` / uMediaServer recording path.

All other branches checked in this sweep are either clearly low-FPS, privilege-gated, OSD-only, data-broadcast-specific, or appear to be renderer/control-plane rather than TV-to-PC media export.
