# lg-webos-tv

LG webOS TV を、PC から制御・観測し、最終的に **ネットワーク経由で地デジ/録画映像を視聴できるか** を実機検証するリポジトリです。

対象実機:

- TV: LG OLED65CXPJA
- webOS: webOSTV 5.0
- Firmware: 04.64.00
- Receiver: ARIB
- CPU architecture: aarch64
- Developer Mode: 有効化済み

> このリポジトリは公開前提です。TV の client key、Developer Mode passphrase、MAC アドレス、LAN 内 IP、アカウント情報などの秘密情報はコミットしません。

## 現在までに確認できたこと

### 1. SSAP 接続と TV 制御

TLS WebSocket の SSAP 接続に成功し、以下を確認済みです。

- 電源状態取得
- 音量/ミュート制御
- チャンネル一覧取得
- 現在チャンネル取得
- チャンネル変更
- リモコン相当の key input
- アプリ起動
- `ssap://tv/executeOneShot` による画面キャプチャ

地デジ視聴中の実映像を `executeOneShot` で PC 側へ取得できました。

実測:

- JPEG: 960x540
- 約 0.37〜0.40 秒 / frame
- 約 2.5 fps
- 連続取得時もフレーム内容が変化することを確認

録画アプリをネットワーク経由で起動し、録画再生中の復号済み映像も同じ方法で取得できました。

### 2. USB HDD 録画

TV に接続された USB HDD は Developer Mode SSH から参照できました。

録画データは概ね以下の構成です。

```text
LG Smart TV/lg_dvr/
  000000xxREC/
    *.STR
    *.IDX
    *.DIF
    *.PIF
    *.NPI
    *.SNF
```

`.STR` は 192 byte 周期で、4 byte prefix + 188 byte MPEG-TS packet に見える構造を確認しました。

一方、TS payload は scrambling 状態であり、単純に VLC 等で再生できる形式ではありません。

### 3. legacybroadcast の内部 API

TV 内部には以下の private API が存在します。

```text
com.webos.service.legacybroadcast.frontend/getTSPath

com.webos.service.legacybroadcast.broadcast/getCurrentChannel
com.webos.service.legacybroadcast.broadcast/getVideoInfo
com.webos.service.legacybroadcast.broadcast/getVideoStatus
com.webos.service.legacybroadcast.broadcast/setSink
com.webos.service.legacybroadcast.broadcast/getBroadcastState

com.webos.service.legacybroadcast.dvr/record/startManualRecord
com.webos.service.legacybroadcast.dvr/record/startTimeshiftBuffering
com.webos.service.legacybroadcast.dvr/play/openRecordingPlayInstance
com.webos.service.legacybroadcast.dvr/play/openTimeshiftPlayInstance
com.webos.service.legacybroadcast.dvr/play/getPlayingContentInfo
```

ただし `getTSPath` を Developer Mode の public Luna bus から呼ぶと permission denied になります。

追加の静的解析で、`libelement-frontend.so` の `AdapterFrontend::getDemodConfig()` が `getTSPath` を呼び、返却値から `portType` / `inputType` / `demodType` を数値として読み取ることを確認しました。したがって **`getTSPath` は TS ファイル/FIFO の pathname を返す API ではなく、チューナーから demod/TS 入力へ接続するハードウェアルーティング情報を返す API と判断できます**。

### 4. デコード済み映像 capture pipeline

地デジ視聴中の `videooutput/getStatus` では、内部映像 pipeline を確認できました。

```text
contentType : dtv
source      : VDEC
source size : 1440x1080
frameRate   : 約29.97 fps
scanType    : interlaced
sink        : MAIN
display     : 3840x2160
```

さらに実機には以下が存在します。

```text
/usr/lib/libvtcapture.so.1
/usr/lib/libdile_vt.so.0
/usr/lib/libhalgal.so.2
/usr/bin/vtCaptureTestSuite
```

`vtCaptureTestSuite` の内部文字列から、

```text
0x01 : one-shot capture
0x02 : start continuous capture
0x03 : start continuous capture with policy action
0x04 : stop continuous capture
```

を確認しました。

また、`vtCapture_currentCaptureBuffInfo` 等を利用して Y/UV plane を直接扱う実装が存在します。

現時点では Developer Mode の `prisoner` 権限で `vtCaptureTestSuite` を実行すると、Luna Service の privileged 登録で失敗します。

さらに `libvtcapture.so` が内部で `/dev/video60` (`vt-capture-dev`) を使用することを確認しました。kernel/sysfs 上には `video60` (major 81, minor 6) が存在しますが、Developer Mode jail の `/dev` には公開されていません。`prisoner` は有効 capability も持たないため、通常の Developer Mode だけでこの high-FPS V4L2 capture device を直接開く経路は現時点で見つかっていません。

### 5. capture service

実機には次の service があります。

```text
com.webos.service.capture/createHandle
com.webos.service.capture/setOutput
com.webos.service.capture/execute
com.webos.service.capture/executeOneShot
```

ただし必要 group の `capture.client` は private です。

### 6. 音声 capture

Developer Mode の `prisoner` は `audio` group に所属しており、ALSA capture device にアクセスできます。

特に以下を確認しました。

```text
hw:1,2  MixerCapture      S32_LE / 2ch / 48kHz
hw:1,1  SpeakerFeedback   S32_LE / 8ch / 48kHz
hw:0,12 dsnoop capture    S16_LE / 2ch / 48kHz
```

`MixerCapture` から 48kHz stereo PCM を連続取得でき、TV を一時的に mute した状態でも十分な非ゼロ信号が残ることを確認しました。したがってスピーカー出力後のマイク録音ではなく、**内部 audio mixer からの pre-mute / pre-volume 系 PCM 取得経路である可能性が高い**です。番組音声との対応は引き続き検証します。

### 7. 生 TS 候補

実機には以下の device node があります。

```text
/dev/lg/pvr0
/dev/lg/pvr0up0
/dev/lg/pvr0dn0
/dev/lg/sdec0
/dev/lg/te0
/dev/lg/demod0
/dev/lg/arib2
```

一部 control device は Developer Mode ユーザーでも open できますが、単純 `read()` では TS は得られず、ioctl 等による初期化が必要と考えられます。

## 現在の到達点

| 目的 | 状態 |
|---|---|
| PC から TV 制御 | 成功 |
| PC からチャンネル変更 | 成功 |
| 地デジ画面の JPEG 取得 | 成功 |
| 録画再生画面の JPEG 取得 | 成功 |
| 960x540 簡易ライブビュー | 成功 |
| 高 FPS のデコード済み映像取得 | 調査中 |
| 音声取得 | **ALSA MixerCapture で PCM 取得成功、番組音声との対応を検証中** |
| `getTSPath` 呼び出し | private permission で停止 |
| 生 TS 取得 | 調査中 |
| HDD 録画の直接復号 | 未達 |

## 次の調査

優先順位は以下です。

1. ALSA `MixerCapture` が地デジ/録画の decoded program audio であることを確定
2. `/dev/lg/pvr*`, `sdec*`, `te0` の ioctl / userspace library 呼び出し調査
3. `libvtcapture` / hidden `/dev/video60` の Developer Mode 境界を追加解析
4. 成功済み `executeOneShot` + ALSA audio を利用した PC viewer の整理
5. 必要なら privileged/root 方式を別フェーズとして評価

## 参考実装

- [webosbrew/hyperion-webos](https://github.com/webosbrew/hyperion-webos)
- [TBSniller/piccap](https://github.com/TBSniller/piccap)
- [rhinoswirl/webostv-research](https://github.com/rhinoswirl/webostv-research)
- [secprog/webos-recording-decryptor](https://github.com/secprog/webos-recording-decryptor)

## 注意

この調査は所有する TV に対する実機検証を前提とします。公開リポジトリには認証情報、pairing key、Developer Mode passphrase 等を残さないでください。
