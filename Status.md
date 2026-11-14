# Main Status

- Device: Xiaomi Mi 10 (umi, Snapdragon 865 / SM8250), running iOS 16.7.16 as an iPhone X (`iPhone10,3` / `D22AP`)

> [!IMPORTANT]
> **This is a bring-up project.** The status below is for reference only and changes often; the details of every
> item (how it works, what was tried, what failed) are in [`NOTES.md`](NOTES.md). Security is off on this build:
> do not put anything personal on the phone.

✅ works · ⚠️ partly works / needs testing · ❌ does not work yet

| Feature | Notes | Status |
|---|---|---|
| 🥾 Boot | Stock bootloader → `fastboot boot` of our shim → XNU | ✅ |
| 🧠 CPU | All 8 cores; clocks follow the load (EPSS governor, 75 °C limit) | ✅ |
| 🧮 RAM | 6 GB of the 8 GB for iOS (0x100000000-0x280000000). 6.66 GB (from 0xd6000000) booted but the persistent log stopped writing, so it is off for now | ✅ |
| 💾 UFS | Own host driver, HFS+ root in `userdata`, read/write | ✅ |
| 🖥️ Display | 1080×2340 AMOLED, presented as the iPhone X panel; the whole 1125×2436 frame is scaled to the panel by the display pipe (kernel 0296), so the status bar and dock fit inside the rounded corners | ✅ |
| 📱 Status bar / camera hole | Clock moved right of the front camera hole, right icons and Control Center carrier in place (`libsweet-sb`, kernel 0297) | ✅ |
| 🖼️ Wallpaper | Stock iOS 16 wallpapers and posters (Collections, Gradient, Emoji, Photos, Weather & Astronomy, Unity, Pride); the chosen wallpaper stays after a reboot (user-verified 2026-11-14) | ✅ |
| ⌨️ Keyboard | Keys kept 12 pt off the curved sides (`libsweet-sb`); emoji / dictation layouts and landscape not checked | ⚠️ |
| 🔆 Brightness | Slider, Control Center, auto-brightness, Night Shift | ✅ |
| 👆 Touch | ST FTM5 | ✅ |
| 🔘 Buttons | Power, volume up/down | ✅ |
| 🔋 Battery | Real percentage, voltage, current, charging state | ✅ |
| 🔌 Type-C Charge | Charger control (kernel 0252): 4.40 V, 2.0 A charge current (Android's value for this phone), input limit from the charger type (computer 500 mA, wall / other 1.5 A, Type-C 3 A), QC 2.0 / 3.0 at 9 V (untested). A USB-PD charger charges at ~+0.6 A; a computer port still can't keep up with iOS. No USB-PD, no 30 W charge pump | ⚠️ |
| ⚡ Wireless charge | | ❌ |
| 🔊 Audio | Speaker works (ringtones, system sounds); no microphones, earpiece not used | ⚠️ |
| 🛜 WiFi | 2.4 GHz + 5 GHz, open + WPA2-PSK, HT/VHT rates, Block Ack; **270 Mbit/s down on fast.com at 5 GHz**, kernel meter 168 Mbit/s mean / 330 Mbit/s best second (80 MHz, 2 streams; kernel 0286; 0284 was too slow to run a speed test). Joins the saved network by itself after a reboot (0290, user) and after Wi-Fi off / on. Rejoin after a lost link, hidden networks, dual-band names and sleep still to test; no WPA3 | ⚠️ |
| 🦷 Bluetooth | QCA6390 over UART (921600 baud): scan, connect and pair in Settings, Control Center button, off / on (slow); see the tests below | ⚠️ |
| 📶 Cellular Data | Snapdragon X55 brought up by the kernel (`sweetmdm=10 sweetmdmonline=1`): SIM, LTE registration on Sunrise, a new network search after 3 min without service; status bar shows **Sunrise, signal bars and LTE**; **mobile data works** (Safari over LTE with Wi-Fi off) through the kernel's `pdp_ip0` and `sweet-telephonyd` (the CommCenter stand-in, publishes the service to configd); the session survives a short loss of signal; **Control Center's cellular data button works**. IPv4 only (Sunrise has no IPv6). Not done: Settings > Cellular is incomplete (PIN, SIM lock, carrier settings, data usage, manual network selection); the switch stopping data in the kernel (0280) is untested; calls and SMS not planned | ✅ |
| 📍 Location | Works through Wi-Fi positioning (Weather shows the city, Maps asks for permission) | ✅ |
| 🛰️ GPS | No GPS receiver driver yet; location comes from Wi-Fi | ❌ |
| 🪵 USB | Device mode (USB Ethernet + kernel log) for development; no host / iTunes | ⚠️ |
| 🧭 Sensor | Ambient light only | ⚠️ |
| 🎆 GPU | Adreno 650 draws the UI and every app that uses Metal (through turnip); Safari on the GPU; per-app GPU memory caps | ✅ |
| 🎬 Video decode | No hardware decoder, videos do not play | ❌ |
| 📷 Camera | | ❌ |
| 🔦 Camera Flash | | ❌ |
| 📳 Vibration motor | | ❌ |
| 🫆 Fingerprint | In-display sensor | ❌ |
| 🔐 Face ID / SEP | | ❌ |
| 💳 NFC | | ❌ |
| 🪪 Activation | Local "Hactivation" path, no Apple servers | ✅ |

# Detailed status

## 🔊 Audio

### Various functions of Audio

| Feature | Notes | Status |
|---|---|---|
| 🎛️ ADSP | Booted by the kernel ~200 s after boot; APR to its AFE / ASM / ADM services | ✅ |
| 🔈 Bottom speaker | TERT_MI2S (SD1) → CS35L41 at I²C 0x40; iOS reaches it through `SweetAudio.driver` + `/dev/sweet-audio` | ✅ |
| 🔔 Ringtones / system sounds | Full ringtones play; the ringer slider preview stops after ~2 s, as on a real iPhone | ✅ |
| 🎵 Music / media apps | No media app session reached the audio system in tests | ⚠️ |
| 📞 Earpiece speaker | CS35L41 at 0x42 is enabled but not used as a separate channel | ⚠️ |
| 🔉 Volume | iOS software volume + our device volume control | ✅ |
| 🎙️ Microphones | Codec / Hawking devices are silent placeholders | ❌ |
| 🎧 Headphones (USB-C) | | ❌ |
| 🦷 Bluetooth audio | | ❌ |
| 📳 Haptics (Actuator) | The device exists for iOS's routing, its data is dropped | ❌ |
| ♻️ ADSP crash recovery | One crash seen (`SFR Init: wdog or kernel error suspected`); audio is gone until a reboot | ❌ |

## 🧭 Sensors

### Various functions of Sensors

| Feature | Notes | Status |
|---|---|---|
| 💡 Ambient light | SLPI firmware booted by the kernel, read through QRTR / SEE; drives auto-brightness | ✅ |
| 🌈 True Tone (color) | The light sensor's color channels are not read | ❌ |
| 📐 Accelerometer | | ❌ |
| 🌀 Gyroscope | | ❌ |
| 🧲 Magnetometer (compass) | | ❌ |
| 👂 Proximity | | ❌ |
| 🌡️ Barometer | | ❌ |
| 🛰️ GPS | | ❌ |

## 🪵 USB

### Various functions of USB

| Feature | Notes | Status |
|---|---|---|
| 🧪 Device mode (ECM) | USB Ethernet with the live kernel log (UDP 5140) and a kernel file server | ✅ |
| 💻 iTunes / Finder sync | | ❌ |
| 🪵 USB-Host (OTG) | | ❌ |

## 🎆 GPU

### Various functions of GPU

| Feature | Notes | Status |
|---|---|---|
| ⚙️ Power-up | GMU / RPMh, zap shader authenticated by TrustZone | ✅ |
| 🧾 Command streams | The command processor runs our streams | ✅ |
| 🎨 Metal | Over Mesa's turnip (`sweetmtl`) and the metal2vulkan bridge (ios-apollo's series), fence interrupts; a hung GPU is reset and goes on (0282); submits ~2 ms of kernel time (was ~8) | ✅ |
| 📱 Metal for every app | `/var/local/mtl2-clients` = `*,-FitnessWidget,-chronod`: SpringBoard, posters, Safari and any app get a Metal device; widgets (chronod, FitnessWidget) stay on the CPU, they went black on Metal | ✅ |
| 🧭 Safari | Pages draw on the GPU (WebKit.GPU); the address bar glitches, the status bar is white, a link tap sometimes does nothing | ⚠️ |
| 🧮 GPU memory | Wired RAM, reported per app every 10 s; caps (kernel 0251): 768 MB per app, 1536 MB for all apps, 1 GB of RAM kept free; backboardd and SpringBoard uncapped. GPU address windows given back when a process exits (0250) | ✅ |
| 🖼️ CoreAnimation on the GPU | Composites through Metal on the GPU and scans out directly; only the App Library background blur still glitches; backboardd idle at rest (0283: no more 10 Hz redraw), 29-37 fps while animating | ✅ |

## 🧠 CPU and performance

### Various functions of CPU

| Feature | Notes | Status |
|---|---|---|
| 🧩 SMP | 4× A55, 3× A77, 1× A77 prime via PSCI | ✅ |
| 📈 Frequency scaling | EPSS governor (kernel 0238): load-following per cluster, thermal cap at 75 °C, touch boost; idle at 288 / 710 / 844 MHz | ✅ |
| 🔥 Idle CPU load | Home screen idle 7-9 % of one core once settled (kernel 0259; 0238: 65-120 %). CommCenter replaced by sweet-telephonyd (no crash loop, no "Cellular issue detected"); the TTS synthesizers still crash in the first minutes | ✅ |
| 🎯 Thread placement | UI / render threads (priority ≥ 46 and realtime) kept on the A77s (kernel 0248, `sweetbig`): backboardd + SpringBoard 93 % on big cores, starts on a slow core with a big one idle 245 → 26 per 10 s | ✅ |

## 🌄 Future Development

> [!NOTE]
> Roughly in priority order. Each item gets a `NOTES.md` entry when work starts.

* **Sensors**: accelerometer, gyroscope, magnetometer, proximity, barometer through the SLPI; True Tone color channels
* **Camera**: the camera stack (CAMSS / ISP / sensors) and the flash
* **Bluetooth: still to test** (working since 2026-11-04, kernel 0234-0237 + `/usr/local/lib/libsweet-bt.dylib`):
  * Pairing kept after a reboot (apollo needed a file system sync for it, their xnu 0144)
  * Music to headphones (A2DP): smooth or stuttering at 921600 baud
  * Calls / microphone over Bluetooth (HFP) and headphone buttons (AVRCP)
  * Off / on in Control Center and Settings: it comes back, but slowly (bluetoothd: "stack is busy")
  * A boot with kernel 0237 alone (Bluetooth 1 s after UFS by default, no `sweetbtdelay=1` on the line)
  * BLE devices (watch, tracker, keyboard) and a second device paired at the same time
* **CPU frequency scaling, next steps**: memory bus (DDR) voting with the load (apollo's 0153 `sweet_rpmh_ddr_set`); a CPU budget for runaway background processes (apollo's 0160); an idle-temperature comparison against the bootloader's clocks (`sweetcpufreq=0`)
* **Cellular**: Settings > Cellular (answers for PIN attempts, SIM lock, carrier bundle values, data usage; manual network selection = NAS network scan + selection); boot and test the switch kernel 0280 (`umi-0280-data-switch.img`) and the reconnect after a dropped session (0279c); the USB Ethernet gadget that stops mid-boot and never recovers; the `baseband` ADT node (opt-in, `mkadt.py --baseband`) once the GPU's 2-frame loop is understood. Calls / SMS not planned (data-only SIM; calls need VoLTE)
* **CPU load**: the TTS synthesizers' crashes in the first minutes (KonaSynthesizer / SiriTTSSynthesizerAU / MacinTalkAUSP)
* **fairplayd**: `fairplayd.H2` is out of launchd's job list (that unblocked location on 2026-10-31); still to do:
  put `CoreLocationMigrator` back, check `fairplaydeviceidentityd`, and make it part of the image build
* **Audio**: microphones, the earpiece as the second channel, an earlier ADSP start, ADSP restart after a crash,
  haptics on the Actuator
* **Video**: hardware decode (VideoToolbox / AVD)
* **GPU**: why the core sometimes reads a junk address (the hang that 0282 now recovers from); the 10 Hz redraw at rest; GPU frequency scaling and clock gating (`sweetgpuhwcg=1`, untested); Safari's address bar / white status bar and link taps; the App Library blur (back at rest since 0283's vsync fix, still breaks while the App Library scrolls); apps that hit the memory caps (check what they do with a nil buffer); drop the per-submit cache walk (make Turnip's write-combined command memory coherent without it), less CPU per frame
* **RAM**: ~7 GB by moving our DSP / Wi-Fi buffers into free low RAM (0x92700000-0xb03fffff) and starting iOS at 0xc0000000
* **Charging**: why a 3 A USB-PD charger only gives ~+0.6 A (AICL?); QC 3.0 test with a QC charger; USB-PD and the SMB1390 charge pump for 30 W
* **Audio**: check that the speaker still plays with the plug-in's hardware volume control removed (installed, not yet loaded)
* **Keychain**: on 0289 (`umi-0289-keychain.img`; 0288 panics at boot) check that saved Wi-Fi networks, Bluetooth pairings and accounts survive, then that a newly saved network survives a reboot
* **Wi-Fi**: boot 0289 (`umi-0289-keychain.img`, includes 0287) on a dual-band network (one name on 2.4 + 5 GHz) and say what goes wrong, if anything still does; then test: upload rate, rejoin after a lost link, reconnect after reboot / sleep, hidden networks, Forget + rejoin, roaming; WPA3-SAE (needs P-256 ECC + PMF)
* **GPS**, **Fingerprint**, **Face ID / SEP**, **NFC**, **Wireless charging**
