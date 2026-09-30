# ACR / Ad Overlay capture-client investigation

Date: 2026-09-30  
Issue: #7

## Purpose

Trace how LG first-party ACR / Ad Overlay services consume the privileged capture path, without impersonating their LS2 identities, changing role files, enabling ACR, or modifying capture settings.

The end goal is to determine whether their internal video path reveals a reusable **continuous/high-FPS local** route that is materially different from the already-benchmarked one-shot `com.webos.service.capture` path.

## Evidence already confirmed on OLED65CXPJA / webOS 5

The TV's LS2 role metadata permits both ACR and Ad Overlay to register names matching:

```text
com.webos.service.capture.client*
```

ACR uses:

```text
/usr/sbin/acr2
com.webos.service.acr
com.webos.service.acr.client.sync
com.webos.service.acr.client.async
```

The running `acr2` process is root-owned.

The executable itself is hidden from the Developer Mode filesystem view, but related solution libraries are visible, including:

```text
/usr/lib/libalphonsosolution.so.1.0.0
/usr/lib/libalphonsoadoverlay.so.1.0.0
/usr/lib/libshopping.so.1.0.0
```

## Same-generation firmware confirmation: 04.64.00 / W20O

A GitHub Actions one-shot analysis extracted the official LG 04.64.00 package for the 2020 OLED W20O platform:

```text
04.64.00.01-HE_DTV_W20O_AFABATAA
```

This is the same W20O / webOS 5 generation as the target OLED65CXPJA, but it is the `AFABATAA` regional build rather than the target TV's `AFABJAAA` build. Treat it as very strong same-platform evidence, not a byte-identical dump of the Japanese TV.

The extracted rootfs contains all of:

```text
/usr/sbin/acr2
/usr/sbin/adoverlay-service
/usr/lib/libvtcapture.so.1.0.0
/usr/lib/libalphonsosolution.so.1.0.0
/usr/lib/libalphonsoadoverlay.so.1.0.0
```

Most importantly, `readelf -d /usr/sbin/acr2` contains:

```text
NEEDED  libvtcapture.so.1
```

So on the same 2020 W20O generation, ACR does **not** need to obtain its continuous video path through the slow one-shot `com.webos.service.capture` API. It links the lower-level VT capture library directly.

The same `acr2` binary contains the continuous capture path end-to-end:

```text
capture::VideoCapture::Start()
capture::VideoCapture::ThreadMainFunc()
capture::VideoCapture::ReportCaptureResult(...)
capture::VTCaptureWrapper::GetBuffer(...)
capture::VTCaptureWrapper::VTProcess()
core::Controller::OnVideoCaptured(...)
core::SolutionLoader::SendVideoFrame(capture::VideoCaptureBuffer const&, ...)
```

and imports / references the low-level VT API:

```text
vtCapture_createEx
vtCapture_init
vtCapture_preprocess
vtCapture_process
vtCapture_currentCaptureBuffInfo
vtCapture_planeInfo
vtCapture_postprocess
vtCapture_release
vtCapture_stop
vtCapture_finalize
```

The binary also logs an actual FPS counter and starts capture with an explicit FPS parameter:

```text
[VideoCapture] FPS = %d
msg:start video capture/width:%d/height:%d/fps:%d/progressive:%d
msg:restart video capture/width:%d/height:%d/fps:%d/progressive:%d
```

This is the strongest evidence so far that the high-FPS route already reverse-engineered around `libvtcapture` is also the route used by LG's own ACR implementation on the CX generation.

### Internal dump / one-shot support exists, but no external raw-frame API is proven

The same binary contains internal methods:

```text
Capture::VideoCaptureDump(...)
Capture::VideoCaptureOneShot(...)
VideoCapture::SetCaptureDump(...)
VideoCapture::SetCaptureOneShot(...)
VTCaptureWrapper::SetCaptureDump(...)
VTCaptureWrapper::SetCaptureOneShot(...)
```

However, corresponding raw-frame/dump methods were **not** found as obvious Luna method-name strings in this W20O `acr2`. Confirmed read-style Luna strings include:

```text
getACRstatus
getACRAppStatus
getACRLaunchFlag
getACRSolutionStatus
getAudioCaptureStatus
getVideoCaptureStatus
getCaptureSpeed
```

Therefore the current model is:

- ACR definitely has a continuous VT capture buffer inside the root-owned process on the same W20O generation.
- ACR definitely passes captured video buffers into the selected solution plugin.
- An externally callable ACR method that returns those raw frames is **not** established.
- Reusing ACR now means looking for a legitimate shared-buffer/IPC/export path, not assuming the Luna API returns images.

### Capture policy is explicit

The W20O binary contains detailed reasons that disable video capture, including recording, timeshift, scrambled/protected content, unsupported inputs/resolutions, blocked channels, multiview, store mode, and already-running capture.

That means any live test must distinguish:

1. capture implementation availability;
2. current policy eligibility; and
3. whether video capture is enabled by the downloaded ACR configuration.

## Supporting cross-version static evidence

### 1. The solution-plugin interface can receive video frames

Firmware metadata published by `webosbrew/dev-toolbox-cli` shows that ACR recognition plugins have historically implemented a common video-frame interface.

A 2017 Alphonso plugin exports:

```text
AlphonsoClient::SendVideoFrame(Solution::VideoFrame const&)
AlphonsoSolution::SendVideoFrame(Solution::VideoFrame const&)
AlphonsoSolution::UseCaptureUV()
AlphonsoSolution::NeedCopiedBuffer()
AlphonsoSolution::AdjustVideoCaptureSize(...)
```

A 2020-generation Samba ACR plugin exports the same style of interface:

```text
SambaClient::SendVideoFrame(Solution::VideoFrame const&)
SambaSolution::SendVideoFrame(Solution::VideoFrame const&)
SambaClient::CaptureFormat()
SambaSolution::UseCaptureUV()
SambaSolution::CaptureNoPadding()
SambaSolution::AdjustVideoCaptureSize(...)
```

A 2020 Nielsen plugin likewise exports `SendVideoFrame(Solution::VideoFrame const&)`.

This is strong evidence for an architecture where `acr2` owns capture/orchestration and dispatches captured frames to a selected recognition solution plugin.

It also explains why the currently visible Alphonso adapter library alone does not necessarily contain the LS2 capture-service request strings.

### 2. Newer webOS moves ACR capture onto vtCapture

Public static-analysis reports for webOS 24 show `acr2` linked with `libvtcapture.so.1` and containing capture classes / identifiers such as:

```text
capture::VideoCapture
capture::ImageConverter
capture::CaptureBuff
capture::CaptureDelegate
DaiFastCapture
```

This independently matches the same-generation W20O firmware result above. A live `/proc/<acr2-pid>/maps` check is still useful to confirm the Japanese `AFABJAAA` runtime, but direct `libvtcapture` linkage is no longer only a newer-webOS hypothesis.

### 3. Video capture is configuration-dependent

Older ACR runtime configuration examples use an `acr.xml` file with fields such as:

```text
capture_method
capture_format
<video capture="..."/>
<video-capture-max-input-resolution .../>
```

One observed configuration had `capture_method="SOURCE"`, `capture_format="YUV420"`, but `<video capture="false"/>`.

Therefore:

- presence of capture code does not mean video capture is active;
- ACR may be audio-only under a given regional/server configuration;
- the live TV's current ACR configuration must be inspected before treating ACR as an active frame source.

Do not record or publish provisioning tokens, device IDs, advertising IDs, or other identifiers from that config.

## ACR LS2 read-only surface worth probing

Public reverse-engineering projects reference these read-style methods on `com.webos.service.acr`:

```text
getACRstatus
getACRAppStatus
getACRLaunchFlag
getACRSolutionStatus
getAudioCaptureStatus
getVideoCaptureStatus
getCaptureCondition
getCaptureSpeed
getCurrentChannelInfo
getProductVersion
```

For the W20O 04.64.00 binary, the particularly useful confirmed method-name strings are:

```text
getVideoCaptureStatus
getCaptureSpeed
getACRSolutionStatus
getAudioCaptureStatus
getACRstatus
```

`getCaptureCondition` appears in other reverse-engineering material but was not found in the W20O `acr2` string set collected here, so it should not be treated as confirmed on this generation.

Do **not** call state-changing methods such as `startAcr`, `setACRsetting`, `setCaptureSpeed`, `setVideoPig`, or consent-related setters if present. The exact W20O string scan did not surface all of those setters; this investigation remains read-only.

## Important distinction: private one-shot capture is already reachable

The bundled Node.js `palmbus` module can open a local bus handle from the official Developer Mode SSH environment and reach `com.webos.service.capture`.

That path has already been benchmarked in `docs/capture-service-benchmarks.md`.

Even with persistent handles and small JPEG frames, one-shot `execute` bottoms out around 190–250 ms/frame and parallel handles serialize. It is therefore not the target high-FPS route.

Issue #7 is specifically looking for a **different continuous/internal path** used by privileged first-party consumers.

## Next live-TV checks — read only

When Developer Mode SSH is available again, perform these in order.

### A. Read ACR status over the local bus

Use the same proven local `palmbus` mechanism as the private capture benchmark and call only:

```text
luna://com.webos.service.acr/getACRSolutionStatus
luna://com.webos.service.acr/getVideoCaptureStatus
luna://com.webos.service.acr/getAudioCaptureStatus
luna://com.webos.service.acr/getCaptureSpeed
luna://com.webos.service.acr/getACRstatus
```

Record full return structure, but redact any identifiers if present.

### B. Find and inspect current ACR config

Check the known config-path preference first:

```text
/mnt/lg/cmn_data/acr/data/config_file_path
```

If it points to an accessible file such as `/tmp/acr.xml`, extract only capture-related fields:

```text
capture_method
capture_format
audio capture
video capture
video-capture-max-input-resolution
capture cadence / interval fields
```

Do not copy credentials, client tokens, advertising identifiers, or provisioning data into the repository.

### C. Inspect the running ACR process

For the live `acr2` PID, where permissions allow:

```text
/proc/<pid>/maps
/proc/<pid>/fd
/proc/<pid>/cmdline
```

Search mappings for:

```text
libvtcapture
libdile_vt
libalphonso
libsamba
libnielsen
capture
```

Search FD targets for:

```text
/dev/video60
/dev/video*
anon_inode
socket:
memfd
/tmp
/dev/shm
```

The critical observation is whether a root-owned `acr2` process can hold `/dev/video60` even though the node is absent from the Developer Mode mount namespace.

### D. Correlate anonymous IPC

Take two read-only snapshots during Live TV:

1. `/proc/<acr2-pid>/fd`
2. `/proc/net/unix`

Resolve `socket:[inode]` ownership and compare changes over a short interval.

If Ad Overlay is running, repeat the same mapping/FD inspection for `adoverlay-service`.

## Working interpretation

The strongest current model is:

```text
video source / display pipeline
        |
        v
privileged capture implementation
        |
        v
acr2 capture/orchestration
        |
        +--> Solution::VideoFrame --> recognition plugin
        |
        +--> recognition result / metadata
```

For the same W20O generation, the left-hand edge is now confirmed to include direct `libvtcapture` linkage and direct `vtCapture_*` calls inside `acr2`. What remains to confirm on the Japanese OLED65CXPJA runtime is whether its regional `AFABJAAA` build behaves identically and whether the root-owned process exposes any buffer/IPC object that an unprivileged Developer Mode process can legitimately consume.

There is currently **no evidence of an officially exposed raw-frame or network-stream API** from ACR or Ad Overlay. The next useful branch is therefore the process boundary: file descriptors, shared memory, Unix sockets, and any exported client interface around the already-confirmed continuous VT buffer.

## Sources used for cross-version comparison

- webosbrew/dev-toolbox-cli firmware symbol/dependency metadata:
  - `04.30.90.01-HE_DTV_W20L_AFAAJAAA/libsambasolution.so.1.0.0.json`
  - `04.30.90.01-HE_DTV_W20L_AFAAJAAA/libnielsensolution.so.1.0.0.json`
  - `06.19.80.01-HE_DTV_W17H_AFADABAA/libalphonsosolution.so.1.0.0.json`
- https://scriptbase.org/lg-tv/webos-24.html
- https://gathering.tweakers.net/forum/list_messages/2344542

Cross-version evidence is supporting evidence only; OLED65CXPJA runtime observation remains authoritative for this repository.

## Reproducible firmware analysis

The repository contains:

```text
.github/workflows/analyze-cx-firmware.yml
```

Successful same-generation analysis run:

```text
GitHub Actions run 36675788393
commit fbda7a39de50420abffe3cc52d5cbc55cfb02d61
artifact cx-firmware-acr-analysis
```

Only text reports are uploaded; the LG firmware and extracted binaries are not published as artifacts.
