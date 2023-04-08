# Total Commander .ST/.MSA packer plugin v0.03

This little plugin enables Total Commander to open and extract Atari ST .ST and .MSA disk images that have a valid file system in it.

## Installation

There are _two_ ways of installing this extension depending on your use case:

### "Open file directly" use case
If you want to open .ST/.MSA disk images directly by pressing Return on a selected disk image: just open **msast_wcx.zip** in Total Commander and follow the install procedure. The plugin will be associated with .ST and .MSA extensions. Enter disk images like folders/ZIP files.

### "Open file with CTRL+Pagedown" use case
Say you already have e.g. an emulator associated with the .ST/.MSA extensions and you want to start them in that emulator by simply pressing return like you're used to by now - then you'll need another way to enter the images in Total Commander.
Just open **msast_wcx_ctrlpagedownonly.zip** in Total Commander and follow the install procedure. Now the plugin is associated with some gibberish extension you'll probly never use and that keeps the extension out of your way in TC when .ST and .MSA filesare shown; but *at the same time* you can simply open .ST/.MSA files by pressing **CTRL+Page Down** on a selected disk image. Magic!

## Credits
FAT12/16/32 reader "PetitFAT" taken from here: http://www.elm-chan.org/fsw/ff/00index_p.html (slightly modified) 

