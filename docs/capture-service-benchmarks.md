# Private capture service benchmark

## Summary

Official Developer Mode cannot call the private capture methods with `luna-send-pub`, but the TV's bundled Node.js `palmbus` module can open a private bus handle from the Developer Mode SSH environment:

```js
var palmbus = require('palmbus');
var bus = new palmbus.Handle('', true);
bus.call('luna://com.webos.service.capture/getCapability', '{}');
```

On OLED65CXPJA / webOS 5 this returned `{"returnValue":true}`. Therefore the private capture service itself is reachable without root.

## Persistent-handle benchmark

A handle was created once, configured with `setProperties` / `setOptions` / `setOutput`, and then `execute` was called repeatedly. This removes SSAP and HTTPS image-URI overhead.

| Mode | Resolution | Average execute time | Approx max FPS |
|---|---:|---:|---:|
| DISPLAY | 960x540 | 357.9 ms | 2.8 |
| DISPLAY | 1280x720 | 481.8 ms | 2.1 |
| DISPLAY | 1920x1080 | 825.1 ms | 1.2 |
| VIDEO_ONLY | 1280x720 | 475.0 ms | 2.1 |
| BLENDED | 1280x720 | 452.0 ms | 2.2 |
| VIDEO_ONLY/JPEG | 640x360 | ~250 ms | ~4.0 |
| VIDEO_ONLY/JPEG | 480x270 | 218.5 ms | 4.6 |
| VIDEO_ONLY/JPEG | 320x180 | 190.4 ms | 5.3 |

JPEG quality 90/60/30 at 640x360 made little difference (~250-265 ms), so JPEG output size is not the primary bottleneck.

Two and four simultaneous capture handles did not improve aggregate throughput. Calls were effectively serialized by the capture service:

```text
2 x 640x360: ~503 ms total
4 x 640x360: ~993 ms total
```

## Format tests

File output worked for JPEG/JPG/PNG/BMP. RGB/RGBA/YUV422 were accepted by `setProperties` but failed during `execute` on this firmware. Supplying guessed `memory`, `buffer`, `sharedMemory`, `stream`, or `socket` output objects was accepted by `setOutput` but ignored; `execute` then failed with `ERROR_NOT_ENOUGH_MEMORY`, indicating no valid destination.

## Conclusion

The private one-shot service is useful because it honors requested resolution and is callable through official Developer Mode, but it is still far too slow for normal TV viewing. The ~185-200 ms floor remains even for an invalid/no-output execute, so the high-FPS path must bypass the per-frame one-shot pipeline.

The remaining high-FPS candidates are:

1. continuous `libvtcapture` / `vt-capture-dev`
2. direct TS/PVR/SDEC/TE path

Low-FPS SSAP or private one-shot capture should remain fallback/debug paths only.
