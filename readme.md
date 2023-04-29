# Jacknife
### (formely known as "Total Commander .ST/.MSA packer plugin v0.03")

This little plugin enables [Total Commander](https://www.ghisler.com) and compatible programs ([Double Commander](https://doublecmd.sourceforge.io), [TC4Shell](https://www.tc4shell.com)) to open and extract Atari ST .ST and .MSA disk images that have a valid file system in it.

## Feature list

### What's already there

- Opening `.ST` disk images
- Opening `.MSA` disk iamges
- Opening `.DIM` disk images (Fastcopy & E-Copy)
- Hard disk image support
- Extracting files from images
- Adding files to images
- Deleting files from images
- Deleting source files while adding to image (i.e. "move to archive")

### What's missing (but planned)

- Creating new disk images (Resizing image until the files fit, or maximum length reached. .ini setting to specify if creating a HD disk image is allowed)
- Deleting empty folders
- Writing .DIM images (only opening and extracting is possible at the moment)
- Support PK_PACK_SAVE_PATHS
- Stand-alone command line version for scripting (adding files, deleting files, creating new disk images, extract deleted files -because tIn insisted-, monitoring directory and if it changes sync the differences with the image)
- Integrating into other programs (for example PiSCSI, zeST etc)
- Extended (XGM) partition support

## Installation

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
- Original plugin code by @tin-nl
- Extended and replaced FAT library by @ggnkua
- Linux/Mac fix by @tattlemuss
