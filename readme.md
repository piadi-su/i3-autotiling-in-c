##AUTO TILING FOR I3 (C VERSION)

A lightweight, fast auto-tiling tool for the i3 window manager, written in C. Inspired by the old Python auto-tiling script, but much faster and more efficient.

##FEATURES

* Automatically tiles windows in i3.

* Written in C for speed and low overhead.

* Easy to install and use.

##INSTALLATION

Copy and paste these commands in your terminal to install:

#clone the repo

* git clone https://github.com/piadi-sudo/i3-autotiling-in-c.git

#go in the repo directory

* cd i3-autotiling-in-c

#make the file

* make

#move autotiling to path

* mv autotiling ~/.local/bin/

#add this to your i3 config

* exec_always --no-startup-id autotiling

