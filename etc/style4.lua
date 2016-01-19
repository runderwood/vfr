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

start_year = 2010
end_year = 1850

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

    recent = nil
    previous = nil
    consec = 255
    stop = false
    for c=start_year,1850,-10 do
        if(stop == false) then
            if(recent == nil) then
                recent = ftr["pop"..c]
            else
                previous = ftr["pop"..c]
                if(recent < previous) then
                    consec = consec - 24
                else
                    stop = true
                end
                recent = previous
            end
        end
    end

    if(consec < 64) then
        fstyle.label_place = 1
        fstyle.label_size = 20.0
        -- this is because label drawing is a hack
        -- such that the script is evaluated twice (once for
        -- features, once for labels) :(
        fstyle.label_text = ""..(county_counter-13)
        print("##\t"..ftr.name.."\t"..fstyle.label_text)
        county_counter = county_counter + 1
        if consec > 160 then
            fstyle.label_color.r = 255.0 - consec
            fstyle.label_color.g = 255.0 - consec
            fstyle.label_color.b = 255.0 - consec
        elseif consec > 100 then
            fstyle.label_color.r = 0
            fstyle.label_color.g = 0
            fstyle.label_color.b = 0
        else
            fstyle.label_color.r = 255
            fstyle.label_color.g = 255
            fstyle.label_color.b = 255
        end
        print(consec..", "..fstyle.label_color.r)
    end

    fstyle.bgcolor.r = consec
    fstyle.bgcolor.g = consec
    fstyle.bgcolor.b = consec


    return fstyle
end
