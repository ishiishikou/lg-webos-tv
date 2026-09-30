# Root / Homebrew feasibility for OLED65CXPJA 04.64.00

Date checked: 2026-09-30

## Device mapping

Current `webosbrew/caniroot` model data maps the Japanese CX model family as:

```text
model family : OLEDCXPJA
broadcast    : arib
machine      : o20
codename     : jhericurl
otaId        : HE_DTV_W20O_AFABJAAA
region       : JP
sizes        : 48 / 55 / 65 / 77
```

The investigated TV is OLED65CXPJA running firmware 04.64.00.

## Known software-root status

For `HE_DTV_W20O_AFABJAAA`, current caniroot data records these boundaries:

| Method | Latest known rootable | Patched since | 04.64.00 |
|---|---:|---:|---|
| RootMy.TV | 04.30.10 | 04.50.53 | Patched |
| crashd | 04.50.53 | 04.50.56 | Patched |
| WTA | 04.50.53 | 04.50.56 | Patched |
| ASM | 04.50.53 | 04.50.56 | Patched |
| DejaVuln | 04.50.56 | 04.50.90 | Patched |
| faultmanager | 04.60.65 | 04.63.15 | Patched |

`04.64.00` is newer than every listed patched boundary.

Current caniroot data for this OTA ID does not list a newer applicable software-root method.

## Newer exploit families

Current webOS root projects point to newer exploit families such as dangbro/jsbro for later webOS generations. Those do not target this webOS 5 / o20 CX combination in the current compatibility data.

Therefore there is currently no supported/known software-root path identified for this exact firmware.

## Homebrew Channel vs root

Homebrew Channel itself can also be used in Developer Mode, but that does not remove the Developer Mode jail. The functionality relevant to this project is root/elevation, because PicCap requires root and uses elevated native services.

PicCap's documented requirements include:

```text
- Root access
- webOS 3.4+
- Homebrew Channel with elevate service
```

Therefore installing Homebrew Channel without root would not solve the `/dev/video60` / LS2 privilege boundary already observed.

## Consequence for high-FPS capture

Technically, PicCap/hyperion-webos demonstrates that webOS 5 TVs can capture decoded video at high frame rates through `vtCapture`.

But on this TV:

```text
firmware 04.64.00
  -> all currently catalogued software-root methods patched
  -> no root elevation
  -> Developer Mode jail remains
  -> /dev/video60 remains hidden
  -> PicCap cannot use its required elevated backend
```

## Risk / decision notes

- Do not downgrade firmware as part of this investigation. RootMyTV documentation states that firmware downgrades are no longer generally possible without already having root.
- Do not run historical exploits merely to see what happens: the compatibility database already classifies this firmware as patched.
- Do not remove Developer Mode or alter boot/service files unless a currently supported root route is first identified.
- Hardware/NVM/debug approaches are a separate, invasive research category and are not part of the current plan.

## Current recommendation

Keep the TV unchanged and continue two safe tracks:

1. Watch `webosbrew/caniroot` / webOS Homebrew for a newly disclosed method supporting `HE_DTV_W20O_AFABJAAA` on >= 04.64.00.
2. Continue non-root analysis of official/system services and alternate stream paths.

If a future supported root method appears, re-evaluate PicCap/hyperion-webos as the shortest route to ~30 fps decoded video.
