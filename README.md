# Delta

Delta is a river layout generator that is able to swap out the current
layout on the fly (since delta is frequently used to represent change
mathematically).

## Installation

Simply clone the repository, then install with make:

```{bash}
git clone https://github.com/Braden-Griebel/delta.git
cd delta
sudo make install
```

If you would rather install into another directory,
you can pass in alternate PREFIX and/or BINDIR variables.
Delta is installed at BINDIR/delta, where BINDIR defaults to
$PREFIX/bin. Additionally, the default is to build in a build directory,
which can be modified by changing the BUILDDIR variable.
If you change the install directory, you will likely need to add the
new directory to your path if you haven't already.
For example:

```{bash}
make PREFIX=/desired/install/dir install # Install into /desired/install/dir/bin
make BINDIR=/desired/install/dir install # Install into /desired/install/dir
make BUILDDIR=/desired/build/dir # Build in desired prefix
```

## Usage

To use this layout generator, first start the program in your river init file

```{bash}
delta &
```

then the send-layout-cmd can be used in keymaps (not the layout is called
swapable here, not delta), e.g.

```{bash}
# Super+H and Super+L to decrease/increase the main ratio of delta
riverctl map normal Super H send-layout-cmd swapable "main_ratio -0.05"
riverctl map normal Super L send-layout-cmd swapable "main_ratio +0.05"

# Super+Shift+H and Super+Shift+L to increment/decrement the main count of delta
riverctl map normal Super+Shift H send-layout-cmd swapable "main_count +1"
riverctl map normal Super+Shift L send-layout-cmd swapable "main_count -1"

# Super+W to swap layout
riverctl map normal Super W send-layout-cmd swapable "swap_layout"
```

## Licensing

This code (in delta.c, and the Makefile) is licensed under the GPL-3.0-only
license. The layout generator is a modified version of the example layout generator
from river found here: [https://codeberg.org/river/river/src/branch/0.3.x/contrib/layout.c](https://codeberg.org/river/river/src/branch/0.3.x/contrib/layout.c),
which is licensed under the GPL-3.0-only license. The river-layout-v3.xml
protocol file is from
[https://codeberg.org/river/river/src/branch/0.3.x/protocol/river-layout-v3.xml](https://codeberg.org/river/river/src/branch/0.3.x/protocol/river-layout-v3.xml),
and is licensed under the MIT license.
