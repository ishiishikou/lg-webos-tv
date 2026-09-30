# Tools

## Decoded audio capture

`capture_audio.py` streams the TV's internal ALSA `MixerCapture` PCM through the official Developer Mode SSH path exposed by `ares-novacom`.

Example:

```sh
python3 tools/capture_audio.py --device tv --duration 5 --output sample.s32le
```

Live playback on a PC with ffplay:

```sh
python3 tools/capture_audio.py --device tv \
  | ffplay -f s32le -ar 48000 -ac 2 -
```

Current verified format on OLED65CXPJA / webOS 5:

- ALSA PCM: `hw:1,2` (`MixerCapture`)
- format: S32_LE
- sample rate: 48 kHz
- channels: 2

The script deliberately contains no TV address, pairing key, Developer Mode passphrase, or account information.

## Read-only ACR status probe

`acr-status-probe.js` calls only read-style methods on `com.webos.service.acr` using the TV-bundled Node.js `palmbus` module.

Methods:

```text
getACRstatus
getACRAppStatus
getACRLaunchFlag
getACRSolutionStatus
getAudioCaptureStatus
getVideoCaptureStatus
getCaptureSpeed
```

The script does not enable ACR, change consent, start capture, change capture speed, or dump frames. It is intended for the official Developer Mode SSH environment after copying the script to the TV.

Example on the TV:

```sh
node acr-status-probe.js
```

Access-denied or unknown-method results are recorded per method instead of aborting the whole probe.
