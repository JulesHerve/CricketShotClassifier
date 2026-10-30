// =============================================================================
// storage.h  -  microSD helpers (shared SPI bus aware)
//
// Thin wrapper around the Arduino SD library. All reads/writes claim the SPI
// bus for the SD card first, so they can never collide with a radio or NFC
// transaction. If no card is present, isReady() returns false and apps fall
// back to showing results on screen only.
// =============================================================================
#pragma once
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <string>
#include "spi_bus.h"
#include "../config.h"

namespace core {

class Storage {
public:
    static bool begin() {
        SpiBus::claim(SpiBus::Owner::Sd);
        _ready = SD.begin(cfg::PIN_SD_CS, SPI, 20000000);
        if (_ready) {
            ensureDir(cfg::DIR_ROOT);
            ensureDir(cfg::DIR_SUBGHZ);
            ensureDir(cfg::DIR_NFC);
            ensureDir(cfg::DIR_IR);
            ensureDir(cfg::DIR_BADUSB);
        }
        return _ready;
    }

    static bool isReady() { return _ready; }

    static void ensureDir(const char* path) {
        SpiBus::claim(SpiBus::Owner::Sd);
        if (!SD.exists(path)) SD.mkdir(path);
    }

    // Append text to a file, creating it if needed. Returns success.
    static bool append(const std::string& path, const std::string& text) {
        if (!_ready) return false;
        SpiBus::claim(SpiBus::Owner::Sd);
        File f = SD.open(path.c_str(), FILE_APPEND);
        if (!f) return false;
        f.print(text.c_str());
        f.close();
        return true;
    }

    // Overwrite a file with text.
    static bool write(const std::string& path, const std::string& text) {
        if (!_ready) return false;
        SpiBus::claim(SpiBus::Owner::Sd);
        File f = SD.open(path.c_str(), FILE_WRITE);
        if (!f) return false;
        f.print(text.c_str());
        f.close();
        return true;
    }

    // Read a whole file into a string.
    static bool read(const std::string& path, std::string& out) {
        if (!_ready) return false;
        SpiBus::claim(SpiBus::Owner::Sd);
        File f = SD.open(path.c_str(), FILE_READ);
        if (!f) return false;
        out.clear();
        while (f.available()) out.push_back((char)f.read());
        f.close();
        return true;
    }

    // List the files directly inside a directory (names only).
    static std::vector<std::string> listDir(const char* path) {
        std::vector<std::string> names;
        if (!_ready) return names;
        SpiBus::claim(SpiBus::Owner::Sd);
        File dir = SD.open(path);
        if (!dir) return names;
        for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
            if (!e.isDirectory()) names.push_back(std::string(e.name()));
            e.close();
        }
        dir.close();
        return names;
    }

    // Build a timestamp-ish unique filename inside a dir (no RTC => uptime ms).
    static std::string uniqueName(const char* dir, const char* prefix, const char* ext) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s/%s_%lu.%s", dir, prefix,
                 (unsigned long)millis(), ext);
        return std::string(buf);
    }

private:
    static inline bool _ready = false;
};

}  // namespace core
