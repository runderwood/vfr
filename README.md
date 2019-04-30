## About
This program renders shapefiles (and other OGR-compatible datasources) as vector graphics (SVG). Features
can be styled programmatically using Lua. I've used it as a tool for quickly generating simple 
maps for use in educational and research settings.

## Requirements
To build vfr, you need the following dev packages:
- Cairo
- Lua-5.1
- OGR/GDAL

On Debian/Ubuntu you can do:

    apt-get install libcairo2-dev libgdal-dev liblua5.1-0-dev

## Usage
    vfr: the command line vector feature renderer

    usage:
      ./vfr fonts
      ./vfr inform <source>
      ./vfr render [-out outfile] -ht INT | -wd INT path [-fg 0x000000] [-bg 0x000000] [-lua luafile] <source>
      ./vfr version

    example:
      vfr render -out mymap.svg -wd 400 -lua myluafile.lua /home/johnsmith/geodata/myshapefile

## Embedded Lua

- Style features using embedded Lua. See ./etc/style0.lua  and ./etc/style1.lua (or the example below).
- Default styles are drawn from a global variable named `vfr_feature_style`.
- Feature styles may also be programmatically altered using a global function called `vfrFeatureStyle` which takes one argument, a table with fields from the feature (obtain these using `ogrinfo` or a similar tool).
- When using multilayer datasources (e.g. via an OGR VRT file), use the special feature table member `_vfr_layer` to find out which layer a feature belongs to (see example below).

Example:

1. The following is a Lua file for use with VFR. When used with a multilayer OGR datasource including Nebraska school district boundaries, it plots the districts with white fill and light gray stroke, cleans up the district names, and labels each (see [the map](https://raw.github.com/runderwood/vfr/master/out/nesd0.png)).
```lua
vfr_style = {
    stroke = {
        r = 127,
        g = 127,
        b = 127
    },
    fill = nil,
    size = 1
}

function vfrFeatureStyle(ftr)
    fstyle = {
        stroke = {
            r = 0,
            g = 0,
            b = 0
        },
        fill = {
            r = 255,
            g = 255,
            b = 255 
        },
        size = 1,
        label_place = 0,
        label_fill = { r=255, g=0, b=0 }
    }

    if(ftr._vfr_layer == "schooldistricts") then
        fstyle.fill = { r=255, g=255, b=255 }
        fstyle.fill_opacity = 100
        fstyle.stroke = { r=96, g=96, b=96 }
        fstyle.stroke_opacity = 50
        fstyle.size = 1
        fstyle.label_fill = { r=127, g=127, b=127 }
        fstyle.label_place = 1
        fstyle.label_text = ftr.NAME:gsub("(%s+Public Schools)", ""):gsub("(%s+Community Schools)", ""):gsub("(%s+Schools)", "")
        fstyle.label_fill = { r=32, g=32, b=32 }
        fstyle.label_fontdesc = "Cabin Semibold 16"
        fstyle.label_opacity = 80
    elseif(ftr._vfr_layer == "blocks") then
        fstyle.stroke = { r=33, g=64, b=110 }
        fstyle.fill = { r=66, g=127, b=221 }
        fstyle.fill_opacity = 80
        fstyle.size = 1
    else
        fstyle.stroke = { r=127, g=127, b=127}
        fstyle.label_fill = { r=127, g=127, b=127}
        fstyle.label_place = 1
        require 'pl.pretty'.dump(ftr)
    end
    
    return fstyle
end
``` 
*Note the font description (`Cabin Semibold 16`). For a list of font families and faces available to `vfr` on your system, use the `fonts` command.*

## Output

For now, `vfr` outputs SVG, which can be rasterized by programs like [`rsvg-convert`](https://wiki.gnome.org/action/show/Projects/LibRsvg?action=show&redirect=LibRsvg).

## Coming Soon
- Interpolation and other spatial tools (in lua)
- Legends
- More epimap options (bgcolor, etc.)

...

## Examples

### Multilayer with Labels (using Pango and OGR's VRT facilities)
![Multilayer](https://raw.github.com/runderwood/vfr/master/out/neco-sch.png)

### Opacity, Labels

![Labels](https://raw.github.com/runderwood/vfr/master/out/nesd0.png)

### Labels (using Pango)
![Labels](https://raw.github.com/runderwood/vfr/master/out/txcopop.png)

### Randomly Colored Polygons
![Random Colors](https://raw.github.com/runderwood/vfr/master/out/tx_co_rand.png)

### Demographics
![Demographics](https://raw.github.com/runderwood/vfr/master/out/tx_co_decline1930.png)

### Mollweide
![Mollweide](https://raw.github.com/runderwood/vfr/master/out/moll.png)

### Robinson
![Robinson](https://raw.github.com/runderwood/vfr/master/out/robinson.png)

### Spherical Mercator
![Spherical Mercator](https://raw.github.com/runderwood/vfr/master/out/sphmerc.png)

### Geographic
![Geographic](https://raw.github.com/runderwood/vfr/master/out/vfr_out.png)

### Hydrology
![Hydrology](https://raw.github.com/runderwood/vfr/master/out/tx_res.png)

