#papillon

Changing steelseries headset's led color based on the music playing

Only tested with **Siberia Raw Prism**.

Example:

![headset](http://i.imgur.com/GtXsDu4.gif)

## Requirements
* libalsa-dev, user-space sound lib
* libfftw3-dev, lib computing DFT

## Installation
```
$ make
```

## Usage
As papillon cannot be run as root, the HID device of the headset has to be writable by your user.
```
$ ./papillon [path to the HID device] [sound device]
```
### Example
```
$ ./papillon /dev/hidraw0 default
```
And listen some music.
