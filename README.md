# 🛡️ ArchWarden
![Version](https://img.shields.io/badge/version-0.0.15Beta-red.svg)
![Platform](https://img.shields.io/badge/platform-Arch%20Linux-1793d1.svg?logo=arch-linux)
![License](https://img.shields.io/badge/license-MIT-green.svg)

---
> ⚠️ **Notice:** This release (0.0.15 Beta) is in the early stages of development and has not yet reached full stability. Bugs may occur, and it is strictly not recommended for production environments.

**ArchWarden** is a modern, fast, and secure system dashboard and package manager designed for Arch Linux-based distributions. Combining a highly polished graphical interface (GTK3) with a robust multi-threaded backend, it makes system administration an enjoyable and secure experience.

---

### 👁️ The Warden (System Guardian)

The beating heart of this software is **The Warden**—a smart background analyzer that actively ensures your system's security and stability:

* **Package Stability Analysis:** Before you install anything, The Warden scans package descriptions and version strings. It actively warns you if a package is marked as experimental, unstable (Alpha/Beta/Git), or deprecated.
* **Network Hunter:** Constantly monitors your network traffic. If a hidden process attempts to transmit data, or if a connection is established to a suspicious port, The Warden instantly flags it with an Alert.

---

### ✨ Current Features (v0.0.15)

**📦 Smart Package Management:**

* Unified, seamless support for `Pacman`, `AUR`, and `Flatpak`.
* **Modern Selection System:** Traditional checkboxes have been removed. Simply **Right-Click** or **Long-Press** a package to select it (indicated by a glowing blue aura).
* **Batch Operations:** Install, update, remove, or rebuild multiple selected packages simultaneously.
* **Smart Orphan Detection:** Unused orphaned packages are highlighted in **Red**, while useful orphaned build tools (like `cmake`, `go`, `rust`, `make`) are highlighted in **Orange** to prevent accidental system breakage.

**🌐 Network Monitor & Analyzer:**

* Live graphical chart of upload and download speeds (RX/TX).
* Real-time tracking of Ping, Local IP, Public IP, and ISP Name (including country flags).
* **App-Based Grouping:** Network connections are intelligently grouped by **Process/App Name** (e.g., Firefox, Python) rather than just raw IPs.
* **Kill App Network:** A dedicated quick-action button to instantly kill processes establishing suspicious or unwanted connections.

**🎨 Premium User Interface:**

* Modern floating dock-style sidebar.
* Glowing, laser-style progress bars for active tasks.
* Fully threaded background task manager with a Notification Bell system, ensuring the UI never freezes during heavy syncs or installations.

---

### 🚀 Roadmap

We are just getting started. Look forward to the following features in upcoming stable releases:

* [ ] **100% Stability:** Patching edge-case bugs and hardening memory management.
* [ ] **System Optimizer:** One-click cleaner for excess cache, old logs, and unused dependencies.
* [ ] **Systemd Manager:** Control, enable, and disable Linux services straight from the dashboard.
* [ ] **Warden Expansion:** Deeper malware behavior analysis and Flatpak permission management.
* [ ] **Theming Engine:** Custom color palettes and UI customization.

---

### 🛠️ Build & Run Instructions

Written in pure `C` and `GTK3`, compilation is incredibly fast and simple.

**Prerequisites:**
Ensure you have the base development tools and GTK3 installed:

```bash
sudo pacman -S base-devel gtk3

```

**Build:**

```bash
# Clone the repository
git clone https://github.com/your-username/archwarden.git
cd archwarden

# Compile the code
make

# Run ArchWarden
./ArchWarden

```
