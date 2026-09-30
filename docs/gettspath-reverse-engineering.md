# Reverse engineering `legacybroadcast.frontend/getTSPath`

## Why this matters

The method name initially suggested that `getTSPath` might return a filesystem path, FIFO, or socket carrying MPEG-TS. Static analysis of the actual OLED65CXPJA frontend library shows that interpretation is incorrect.

## Evidence

The device contains:

```text
/usr/lib/element/libelement-frontend.so.1.0.0
```

Relevant strings include:

```text
tunerNo
luna://com.webos.service.legacybroadcast.frontend/getTSPath
portType
inputType
demodType
```

The exported `AdapterFrontend::getDemodConfig(...)` function constructs a JSON request containing `tunerNo`, calls `getTSPath`, checks `returnValue`, and extracts numeric `portType` and `inputType`. On paths supporting ATSC3 it also handles `demodType`.

## Conclusion

`getTSPath` is best understood as **transport-stream hardware routing/configuration metadata**, not a path to readable TS bytes.

The likely role is approximately:

```text
tuner
  -> portType / inputType
  -> demod / frontend pipeline
  -> SDEC/PVR/decoder pipeline
```

Therefore the direct-TS investigation should focus on the userspace ioctl/control path around the LG PVR/SDEC/TE devices rather than trying to open the value returned by `getTSPath` as a file.

## Current privilege boundary

The method remains a private Luna API. Calling it from the Developer Mode public bus is denied. The conclusion above comes from static analysis of the frontend client library and does not require bypassing that boundary.
