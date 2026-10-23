# STT co-processor service

Runs on the power-gated Linux SoM (ADR-0002). The ESP32-S3 switches the SoM on (STT_EN), waits for `READY`, streams 16 kHz PCM, receives `FINAL`, sends `SHUTDOWN` and cuts power.

```
ESP32-S3                      SoM (stt_server.py)
   |--- power on (STT_EN) ----->|  boot (target ≤ 6 s)
   |<-------- READY ------------|  sent unsolicited at start
   |--- AUDIO_BEGIN{16000,"en"}>|
   |--- AUDIO_CHUNK × N ------->|  ≤ 4096 B each
   |--- AUDIO_END ------------->|  transcribe with configured engine
   |<-------- FINAL{conf,text} -|  or ERROR{code,msg}
   |--- SHUTDOWN -------------->|  optional clean poweroff, then STT_EN low
```

Framing is shared with `firmware/components/stt_link/stt_proto.c`; both are tested against the same vectors (`make test`).

## Install on the SoM
```bash
sudo cp -r tools/stt-coproc tools/stt-bench /opt/nomiq/
sudo tee /etc/systemd/system/nomiq-stt.service >/dev/null <<'UNIT'
[Unit]
Description=Nomiq offline STT
After=local-fs.target
[Service]
ExecStart=/usr/bin/python3 /opt/nomiq/stt-coproc/stt_server.py --port /dev/ttyS0 --baud 921600 \
  --engines /opt/nomiq/stt-bench/engines.json --engine vosk-small-en --poweroff-on-shutdown
Restart=always
[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl enable nomiq-stt
```
Boot-time tips: disable Wi-Fi/Bluetooth/HDMI services, use a read-only rootfs, pre-load the model into page cache with `vmtouch`, `systemd-analyze` to find slow units.
