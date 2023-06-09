# Jacknife
### (formely known as "Total Commander .ST/.MSA packer plugin v0.03")

This little plugin enables [Total Commander](https://www.ghisler.com) and compatible programs ([Double Commander](https://doublecmd.sourceforge.io), [TC4Shell](https://www.tc4shell.com)) to open and extract Atari ST .ST and .MSA disk images that have a valid file system in it.

## Feature list

### What's already there

|                                                           |`.ST`  | `.MSA` | `.DIM`<sup>[1](#f1)</sup> | `.AHD`<sup>[2](#f1)</sup> |
|---                                                        |:---:  |:---:   |:---:                      |:---:                      |
Opening image (directory listing)                           |&check;|&check; |&check;                    |&check;                    |
Extracting files<sup>[3](#f3)</sup>                         |&check;|&check; |&check;                    |&check;                    |
Adding files<sup>[3](#f3)</sup>                             |&check;|&check; |&check;                    |&check;                    |
Creating new folders<sup>[3](#f3)</sup>                     |&check;|&check; |&check;                    |&check;                    |
Deleting files<sup>[3](#f3)</sup>                           |&check;|&check; |&cross;                    |&check;                    |
Deleting folders<sup>[3](#f3)</sup>                         |&check;|&check; |&cross;                    |&check;                    |
Deleting source files (move to archive)                     |&check;|&check; |&cross;                    |&check;                    |
Creating new image<sup>[3](#f3)</sup> <sup>[4](#f4)</sup>   |&check;|&check; |&cross;                    |&cross;                    |
Progress bar info during operations                         |&check;|&check; |&check;                    |&check;                    |

<a name=f1><sup>1</sup></a>Fastcopy & E-Copy

<a name=f2><sup>2</sup></a>Hard disk images, AHDI 3.00 compatible primary partitions only for now

<a name=f3><sup>3</sup></a> Recursively

<a name=f4><sup>4</sup></a> Resizing image until the files fit, or maximum length reached (in which case an error is generated)

### What's missing, but planned (in order of severity/implementation likelihood)

- Extended (XGM) partition support
- `.ini` file with various settings (specify if creating a HD disk image is allowed, customisable hard disk image extension, etc)
- Writing .DIM images (only opening and extracting is possible at the moment)
- Support PK_PACK_SAVE_PATHS
- Stand-alone command line version for scripting (adding files, deleting files, creating new disk images, extract deleted files -because tIn insisted-, monitoring directory and if it changes sync the differences with the image)
- Integrating into other programs (for example PiSCSI, zeST etc)

## Installation

### Windows
Pre-built binaries are supplied.

### Linux/OSX
No binaries for these Operating Systems are provided. Use the `build_linux_mac.sh` script to build the plugin and follow the instructions below for Double Commander.

### Total Commander

There are _two_ ways of installing this extension depending on your use case:

#### "Open file directly" use case
If you want to open .ST/.MSA disk images directly by pressing Return on a selected disk image: just open **jacknife.zip** in Total Commander and follow the install procedure. The plugin will be associated with .ST and .MSA extensions. Enter disk images like folders/ZIP files.

#### "Open file with CTRL+Pagedown" use case
Say you already have e.g. an emulator associated with the .ST/.MSA extensions and you want to start them in that emulator by simply pressing return like you're used to by now - then you'll need another way to enter the images in Total Commander.
Just open **jacknife_ctrlpagedownonly.zip** in Total Commander and follow the install procedure. Now the plugin is associated with some gibberish extension you'll probly never use and that keeps the extension out of your way in TC when .ST and .MSA filesare shown; but *at the same time* you can simply open .ST/.MSA files by pressing **CTRL+Page Down** on a selected disk image. Magic!

### Double Commander

Unlike Total Commander, Double Commander doesn't support automatic installation of plugins. Therefore a manual procedure is described below:

- Extract `jacknife.wcx` or `jacknife.wcx64` from `jacknife.zip` or `jacknife64.zip` to a location which is going to be permanent, i.e. not in your *Downloads* folder. Preferably inside the Double Commander install folder
- Open Double Commander
- Navigate to menu *Configuration*->*Options*
- Go to subsection *Plugins*->*Plugins WCX*
- Click *Add*
- Navigate to the location where the extracted `jacknife.wcx` or `jacknife.wcx64` exists and select that
- In the pop-up dialog that asks which extensions to associate plugin, type `st msa dim img` and press Ok. Or If you wish to open images using CTRL+Pagedown (as described in the Total Commander section above) then just type any nonsense text, like `atariextension`

### TC4Shell

Again, this program requires a manual installation:

- Extract `jacknife.wcx` or `jacknife.wcx64` from `jacknife.zip` or `jacknife64.zip` to a location which is going to be permanent, i.e. not in your *Downloads* folder. Preferably inside "\Program Files\TC4Shell"
- After installing TC4Shell, navigate to Control Panel (for Windows 10 onwards just press the Start button and type `control` and enter). There should be a *TC4Shell plugins* button, click that
- Right click on the empty space of the window below the installed plugins. A context menu should pop up
- Select *Install plugin*
- Select *WCX plugins* in the file extension drop down menu
- Navigate to the location where the extracted `jacknife.wcx` or `jacknife.wcx64` exists and select that
- Click *Install*
- You can now right click on disk image files on Windows Explorer and select "Open as folder". A new window should open with the disk contents (assuming a valid image)

## Credits
- FAT12/16/32 reader "PetitFAT" used in older versions taken from here: http://www.elm-chan.org/fsw/ff/00index_p.html (slightly modified) 
- DOSFS library obtained from http://www.larwe.com/zws/products/dosfs/index.html (appears to be Public Domain licensed), modified and debugged (especially for FAT12)
- Original plugin code by <a href=https://github.com/tin-nl>@tin-nl</a>
- Extended and replaced FAT library by <a href=https://github.com/ggnkua>@ggnkua</a>
- Linux/Mac fix by <a href=https://github.com/tattlemuss>@tattlemuss</a>
- Uses *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / <a href=https://www.pcg-random.org>pcg-random.org</a>. Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)
- Some code borrowed from [SdFat libary](https://github.com/greiman/SdFat), which carries the following license:
`MIT License

Copyright (c) 2011..2020 Bill Greiman

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.`
