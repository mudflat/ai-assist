# ESP32-S3 Voice Assistant

An offline-capable, AI-driven personal voice assistant built on the ESP32-S3 SuperMini. This project captures audio from a button-activated I2S microphone, stores it in SPI flash, uploads it to OpenAI's Whisper API for transcription, sends the query to ChatGPT, and plays the response via an I2S speaker using Google TTS.

## 🚀 Features

* Wake-on-button voice capture using INMP441 I2S microphone
* Audio buffering and flash storage with wear leveling
* Playback via MAX98357A I2S DAC/speaker
* Wi-Fi-enabled HTTPS communication with Whisper and ChatGPT APIs
* Full-text transcription and AI-generated responses
* Modular C codebase built on ESP-IDF v6.0.0

## 🧠 Project Flow

1. **Button Press** → Start I2S microphone audio capture
2. **Audio Buffering** → Write raw 32-bit PCM to external SPI NOR flash
3. **Button Release** → Stop recording, flush buffers
4. **Wi-Fi Upload** → Send audio as `multipart/form-data` to OpenAI Whisper API
5. **Transcription → ChatGPT** → Send Whisper output to ChatGPT
6. **TTS Output** → Play ChatGPT response using Google TTS over I2S speaker

## 📦 Hardware

* ESP32-S3 SuperMini (8MB Flash / 2MB PSRAM)
* INMP441 I2S Microphone

  * `SD` → GPIO2
  * `WS` → GPIO4
  * `SCK` → GPIO5
* MAX98357A I2S Amplifier/Speaker

  * `DIN` → GPIO15
  * `BCLK` → GPIO17
  * `LRC` → GPIO16
* Momentary Pushbutton (record trigger) → GPIO1
* W25Q128 16MB SPI Flash

  * `MISO` → GPIO10
  * `MOSI` → GPIO11
  * `SCK`  → GPIO12
  * `CS`   → GPIO13

## 🔧 Software Stack

* **ESP-IDF** v6.0.0
* **I2S driver** for audio capture and playback
* **SPI Flash (Wear Levelling)** via `esp_partition`, `wear_levelling`, and `fatfs`
* **HTTPS** client for API interaction
* **JSON parsing** using cJSON
* **OpenAI Whisper** for transcription
* **ChatGPT API** for conversational response
* **Google TTS API** for voice synthesis

## ✅ Status

| Step | Description                          | Status         |
| ---- | ------------------------------------ | -------------- |
| 1    | GPIO trigger input                   | ✅ Complete     |
| 2    | I2S mic capture                      | ✅ Complete     |
| 3    | Flash storage using wear leveling    | ✅ Complete     |
| 4    | I2S speaker playback                 | ✅ Complete     |
| 5    | Wi-Fi upload + Whisper transcription | 🔄 In Progress |
| 6    | ChatGPT query/response               | ⏳ Pending      |
| 7    | TTS response playback                | ⏳ Pending      |
| 8    | Wake word detection (Skainet)        | ⏳ Pending      |

## 📂 Usage

1. Press and hold the button to begin recording
2. Release the button to stop and upload
3. Audio is transcribed and responded to by ChatGPT
4. Assistant speaks the response aloud

## 📁 File Structure

```
main/
├── main.c
├── flash_store.h / .c    # Flash read/write helpers
├── i2s_mic.h / .c        # I2S mic driver
├── i2s_speaker.h / .c    # I2S speaker driver
├── wifi_connect.h / .c   # Wi-Fi setup
├── whisper_api.c         # Upload + transcription logic
├── chatgpt_api.c         # ChatGPT query/response
├── tts_api.c             # Google TTS audio synthesis
components/
├── hardware_driver/      # Custom ESP-Skainet board config (optional)
```

## 🔐 Notes

* OpenAI and Google TTS API keys must be securely stored and injected at build or via `sdkconfig`.
* Use secure boot and flash encryption for production devices.
* Wake word support will be integrated via ESP-Skainet in future steps.

## 📜 License

MIT License. Contributions welcome.
