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
