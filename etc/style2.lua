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

math.randomseed(1234)

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

    fstyle.bgcolor = {r = math.random(0,255), g = math.random(0,255), b = math.random(0,255)}
    return fstyle
end
