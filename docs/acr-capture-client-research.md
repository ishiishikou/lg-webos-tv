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

## New cross-version static evidence

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

This is consistent with the already-reversed `libvtcapture` / `/dev/video60` high-FPS implementation on the CX, but does **not** prove that the CX generation uses the exact same linkage. That still needs a live `/proc/<acr2-pid>/maps` check.

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

The particularly useful methods for Issue #7 are:

```text
getVideoCaptureStatus
getCaptureCondition
getCaptureSpeed
getACRSolutionStatus
```

Do **not** call state-changing methods such as `startAcr`, `setACRsetting`, `setCaptureSpeed`, `setVideoPig`, or consent-related setters during this investigation.

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
luna://com.webos.service.acr/getCaptureCondition
luna://com.webos.service.acr/getCaptureSpeed
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

What remains unproven on the CX is the exact left-hand edge:

- private `com.webos.service.capture` continuous client,
- direct `libvtcapture`,
- or a generation-specific wrapper around the same VT driver.

There is currently **no evidence of an officially exposed raw-frame or network-stream API** from ACR or Ad Overlay. The value of this branch is to identify a reusable local buffer/IPC path or a legitimate continuous capture interface, not to assume ACR itself is a viewer API.

## Sources used for cross-version comparison

- webosbrew/dev-toolbox-cli firmware symbol/dependency metadata:
  - `04.30.90.01-HE_DTV_W20L_AFAAJAAA/libsambasolution.so.1.0.0.json`
  - `04.30.90.01-HE_DTV_W20L_AFAAJAAA/libnielsensolution.so.1.0.0.json`
  - `06.19.80.01-HE_DTV_W17H_AFADABAA/libalphonsosolution.so.1.0.0.json`
- https://scriptbase.org/lg-tv/webos-24.html
- https://gathering.tweakers.net/forum/list_messages/2344542

Cross-version evidence is supporting evidence only; OLED65CXPJA runtime observation remains authoritative for this repository.
