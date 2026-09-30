# Alternative high-FPS path audit

Date: 2026-09-30

This document records alternative paths that were not covered by the initial SSAP / vtCapture / TS investigation.

## Summary

| Path | Result | Status |
|---|---|---|
| ACR (`acr2`) privileged capture client | Explicitly allowed to register as `com.webos.service.capture.client*` | **Promising internal path; output not exposed publicly** |
| Ad Overlay privileged capture client | Same capture-client privilege as ACR | **Promising internal path; output not exposed publicly** |
| Linux framebuffer (`/dev/fb0`..`fb3`) | Read-only mmap works as `prisoner` | **OSD only; not live video** |
| uMediaServer (`com.webos.media`) | Public role exists; media pipeline service is root-resident | **Worth understanding, but no export endpoint found** |
| UPnP/DLNA | `upnpd` + DMC/DMR libraries present | **Renderer/control-point direction; no MediaServer/ContentDirectory found** |
| Second-screen gateway | App2app/pairing/service-list APIs exist | **No video-stream API identified; Developer Mode caller denied** |
| DVR playback IPC | Compared Unix sockets/FIFOs before/after recording playback | **No named plaintext media pipe/file found** |
| Live-TV timeshift buffer | PAUSE/PLAY tested with attached HDD | **No new visible ordinary file found** |
| Remote support / customer support apps | UI apps with broad outbound LS2 permission | **No direct capture-client privilege identified** |
| VDEC `/dev/video20` | Visible in Developer Mode | **Not a normal capture V4L2 device; standard query/format ioctls fail** |

## 1. Existing privileged capture clients

Two root/system services have explicit permission to register names matching:

```text
com.webos.service.capture.client*
```

### ACR

`/usr/share/ls2/roles/pub/com.webos.service.acr.json` and the private role both allow:

```text
com.webos.service.acr
com.webos.service.acr.client.sync
com.webos.service.acr.client.async
com.webos.rm.client.*
com.webos.service.capture.client*
```

The package contains `/usr/sbin/acr2`.

### Ad Overlay

`adoverlay-service` has the same capture-client registration privilege.

This is important because it proves LG ships more than one first-party service that consumes the privileged capture service on this TV.

Neither service currently exposes a public raw-frame method in the LS2 API metadata. Their start methods are restricted to internal activity/configurator callback groups.

## 2. Framebuffer path

Developer Mode can open and mmap:

```text
/dev/fb0  osd0_fb  1920x1080, 32-bit
/dev/fb1  osd1_fb
/dev/fb2  osd2_fb
/dev/fb3  crsr_fb
```

`read()` is blocked, but read-only `mmap()` succeeds.

A 20x12 pixel grid sampled from the active `/dev/fb0` page every 50 ms for 2 seconds while Live TV was playing produced zero changes across 39 intervals.

Therefore the live broadcast video plane is not composited into the readable OSD framebuffer. This rules out framebuffer mmap as the high-FPS TV-video path.

## 3. uMediaServer

`/usr/sbin/umediaserver` runs as root and registers public role `com.webos.media` with inbound `*`.

Some media pipeline methods are still caller-restricted. No method providing decoded-frame export or a network stream has been identified yet.

## 4. UPnP / DLNA

The TV runs root `/usr/sbin/upnpd` and includes:

```text
libdmcplus_lunabus.so
libdmrplus_lunabus.so
libdtcpip_dlna.so
```

The visible stack clearly supports a DLNA control point and media renderer. Searches for MediaServer / ContentDirectory implementation did not produce a corresponding server component.

The DTCP-IP library includes `video/vnd.dlna.mpeg-tts`, but its presence alone does not establish that the TV exports its tuner/recordings as a DTCP-IP server.

## 5. Second screen

`com.webos.service.secondscreen.gateway` exposes pairing and app-to-app channel concepts internally, but no video stream endpoint was found in its API metadata. Developer Mode calls to its service-list/paired-device/power-state methods are denied by LS2 policy.

## 6. DVR / timeshift IPC

Unix sockets and named FIFO/socket paths were compared before and after starting HDD recording playback.

No new named media FIFO or obvious plaintext TS pathname appeared. Process churn created many anonymous Unix sockets, so anonymous IPC remains possible.

A normal Live TV PAUSE -> PLAY timeshift test also produced no newly visible ordinary file on the USB HDD or `/tmp`.

## Current priority

1. Reverse engineer how `acr2` / `adoverlay-service` configure and consume `com.webos.service.capture` without impersonating their privileged identity.
2. Continue read-only observation of uMediaServer and anonymous media IPC during Live TV / DVR playback.
3. Keep vtCapture `/dev/video60` as the known direct high-FPS implementation path.
4. Treat framebuffer, SSAP screenshot, UPnP renderer, and simple timeshift-file approaches as ruled out or fallback paths.
