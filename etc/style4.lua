vfr_style = {
    stroke = {
        r = 127,
        g = 127,
        b = 127
    },
    fill = {
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
        label_field = "name",
        label_fill = { r=255, g=0, b=0 }
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
        fstyle.label_size = 30.0
        fstyle.label_text = ""..county_counter
        print(string.format("%2d&%s&%d&%d&%2d",
            fstyle.label_text, ftr.name, ftr.pop1930,
            ftr.pop2010, ((ftr.pop2010-ftr.pop1930)/ftr.pop1930)*100.0))
        --print(fstyle.label_text.."\t"..ftr.name.."\t"..ftr.pop1930.."\t"..ftr.pop2010.."\t"..((ftr.pop2010-ftr.pop1930)/ftr.pop1930))
        county_counter = county_counter + 1
        if consec > 160 then
            fstyle.label_fill.r = 255.0 - consec
            fstyle.label_fill.g = 255.0 - consec
            fstyle.label_fill.b = 255.0 - consec
        elseif consec > 100 then
            fstyle.label_fill.r = 0
            fstyle.label_fill.g = 0
            fstyle.label_fill.b = 0
        else
            fstyle.label_fill.r = 255
            fstyle.label_fill.g = 255
            fstyle.label_fill.b = 255
        end
        -- print(consec..", "..fstyle.label_fill.r)
    end

    fstyle.fill.r = consec
    fstyle.fill.g = consec
    fstyle.fill.b = consec


    return fstyle
end
