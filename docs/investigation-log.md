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
