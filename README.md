# ArchWarden 🛡️
![Version](https://img.shields.io/badge/version-0.0.1-blue.svg)
![Platform](https://img.shields.io/badge/platform-Arch%20Linux-1793d1.svg?logo=arch-linux)
![License](https://img.shields.io/badge/license-MIT-green.svg)

**ArchWarden** is a lightning-fast, ultra-lightweight unified graphical system manager designed specifically for Arch Linux. Written in pure C and GTK3, it acts as a true "Warden" for your system, providing seamless control over your packages while actively protecting your OS from unstable updates.

## ✨ Core Features
* 🛡️ **Safe-Update Engine:** Actively prevents system breakage by detecting and warning you about unstable, beta, or known broken package updates (especially from the AUR) before they compromise your system.
* 📦 **Unified Package Management:** Search, install, update, and remove packages across **Pacman, AUR (yay), and Flatpak** simultaneously.
* ⚡ **Ultra-Fast & Smart Caching:** Uses progressive loading, network timeouts, and local smart caching to load and filter over 100,000 packages in fractions of a second without locking up your CPU or disk.
* 📊 **Live Task Manager:** Watch your installations, updates, and removals happen in real-time with a built-in progress tracker and precise status reporting.
* 🧹 **System Cleanup:** One-click removal of orphaned packages (`pacman -Qdtq`) to keep your system debloated.
* 🔍 **Smart Filtering:** Instantly filter applications by Categories (GUI/CLI, Games, Dev, etc.), Package Source, and Update Status.

## 🚀 Upcoming Features (Roadmap)
- [ ] **System Dashboard:** Real-time monitoring of CPU, GPU, and RAM usage.
- [ ] **Memory & Cache Management:** Deep clean system cache (`pacman -Scc`, `yay -Scc`) and optimize RAM usage dynamically.
- [ ] **Transaction Queue:** Queue multiple installations/removals and apply them all at once (Pamac/Synaptic style).
- [ ] **PKGBUILD Inspector:** Read and review AUR scripts directly within the app for maximum security.
- [ ] **i18n Support:** Full RTL layout and language translations.

## 🛠️ Installation

### Dependencies
Make sure you have the following installed on your Arch system:
```
sudo pacman -S base-devel gtk3 polkit curl flatpak
```
Note: An AUR helper like yay is required for AUR functionality.
Build and Run

Clone the repository and compile the source code:
```
git clone [https://github.com/YOUR_USERNAME/ArchWarden.git](https://github.com/YOUR_USERNAME/ArchWarden.git)
cd ArchWarden
make
./ArchWarden
```
⚙️ Configuration & Debugging

    Settings: ArchWarden saves its configuration and cache securely in ~/.config/archwarden/. You can manage enabled repositories and Auto-Sync behavior directly from the in-app settings menu.

    Logging: The app runs a background debugger. If you encounter any issues, check the log file at /tmp/archwarden_debug.log.
