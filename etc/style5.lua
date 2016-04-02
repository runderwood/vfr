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

county_counter = 1

function vfrFeatureStyle(ftr)
    fstyle = {
        fgcolor = {
            r = 0,
            g = 0,
            b = 0
        },
        bgcolor = {
            r = 255,
            g = 255,
            b = 255 
        },
        size = 1,
        label_place = 0,
        label_field = "name",
        label_color = { r=255, g=0, b=0 }
    }


    fstyle.label_place = 0
    fstyle.label_size = 30.0
    fstyle.label_text = ""..county_counter
    county_counter = county_counter + 1
    
    pctchg = ((ftr.pop2010-ftr.pop1930)/ftr.pop1930)*100.0

    if pctchg < 0.0 then
        bgcolor = 0
    elseif pctchg < 50.0 then
        bgcolor = 32
    elseif pctchg < 100.0 then
        bgcolor = 96
    elseif pctchg < 500.0 then
        bgcolor = 192
    elseif pctchg < 1000.0 then
        bgcolor = 224
    else
        bgcolor = 255
    end

    bgcolor = 255 - bgcolor

    --bgcolor = (1.0-math.min(((ftr.pop2010-ftr.pop1930)/ftr.pop1930), 1.0))*255.0
    --bgcolor = math.min(255.0, bgcolor)

    print(string.format("%2d\t%s\t%d\t%d\t%2d%%\t%d",
        fstyle.label_text, ftr.name, ftr.pop1930,
        ftr.pop2010, pctchg, bgcolor))

    fstyle.bgcolor.r = bgcolor
    fstyle.bgcolor.g = bgcolor
    fstyle.bgcolor.b = bgcolor


    return fstyle
end
