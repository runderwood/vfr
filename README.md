## About
This program renders shapefiles (and other OGR-compatible datasources) as PNG images. Features
can be styled programmatically using Lua. I've used it as a tool for quickly generating simple 
maps for use in educational settings.

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
      ./vfr render [-out outfile] -ht INT | -wd INT path [-fg 0x000000] [-bg 0x000000] [-lua luafile] <source>
      ./vfr inform <source>
      ./vfr version

    example:
      vfr render -out mymap.png -wd 400 -lua myluafile.lua /home/johnsmith/geodata/myshapefile

## Embedded Lua

- Style features using embedded Lua. See ./etc/style0.lua  and ./etc/style1.lua
- Default styles are drawn from a global variable named "vfr_feature_style"
- Feature styles may also be programmatically altered using a global function called "vfrFeatureStyle" which takes one argument, a table with fields from the feature (obtain these using ogrinfo or some such tool)

Example:

1. The following takes a shapefile of Texas county boundaries with census data attached and fills black the counties with steady population decline since the 1920 census (the generated map is visible below):
```lua
vfr_style = {
    fgcolor = {
        r = 127,
        g = 127,
        b = 127
    },
    bgcolor = {
        r = 255,
        g = 255,
        b = 255
    },
    size = 1
}

function vfrFeatureStyle(ftr)
    fstyle = {
        fgcolor = {
            r = 64,
            g = 64,
            b = 64
        },
        bgcolor = {
            r = 255,
            g = 255,
            b = 255 
        },
        size = 1
    }

    if(ftr.pop2010 < ftr.pop2000 and ftr.pop2000 < ftr.pop1990 and ftr.pop1990 < ftr.pop1980 and
        ftr.pop1980 < ftr.pop1970 and ftr.pop1970 < ftr.pop1960 and ftr.pop1960 < ftr.pop1950 and
            ftr.pop1950 < ftr.pop1940 and ftr.pop1940 < ftr.pop1930) then
        fstyle.bgcolor = {r = 0, g = 0, b = 0}
        fstyle.fgcolor = {r = 0, g = 0, b = 0}
    end
    return fstyle
end
```

## Coming Soon
- Layers
- Pango label typesetting
- SVG (and more) output options
- Interpolation and other spatial tools (in lua)
- Legends
- More epimap options (bgcolor, etc.)

...

## Examples

### Multilayer with Labels (using Pango and OGR's VRT facilities)
![Multilayer](https://raw.github.com/runderwood/vfr/master/out/neco-sch.png)

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

