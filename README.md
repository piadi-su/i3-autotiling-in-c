# AUTO TILING FOR I3 (C VERSION)

A lightweight, fast auto-tiling tool for the **i3 window manager**, written in **C**.
Inspired by the old Python auto-tiling script, but much faster and more efficient.

---

## Features

* Automatically tiles windows in i3
* Written in C for speed and low overhead
* Easy to install and use

---

## Installation

Copy and paste the following commands into your terminal.
Each block can be copied with a single click on GitHub.

### Clone the repository

```bash
git clone https://github.com/piadi-sudo/i3-autotiling-in-c.git
```

### Enter the repository directory

```bash
cd i3-autotiling-in-c
```

### Build the project

```bash
make
```

### Move the binary to your PATH

```bash
mv autotiling ~/.local/bin/
```

---

## Configuration

### i3

Add the following line to your i3 config file:
`~/.config/i3/config`

```bash
exec_always --no-startup-id autotiling
```

### Sway

Add the following line to your Sway config file:
`~/.config/sway/config`

```bash
exec_always autotiling
```

---

## License

This project is released under the MIT License.
