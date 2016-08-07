#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__linux__)
#define VFRSYSNAME "Linux"
#else
#define VFRSYSNAME "Unknown"
#endif

typedef enum {VFRLABEL_NONE, VFRLABEL_AUTO, VFRLABEL_CENTER, VFRLABEL_POINT,
    VFRLABEL_LINE, VFR_LABEL} vfr_label_place_t;

typedef enum {VFRORIENT_NONE, VFRORIENT_AUTO, VFRORIENT_NORTH, VFRORIENT_SOUTH,
    VFRORIENT_EAST, VFRORIENT_WEST, VFRORIENT_NORTHEAST, VFRORIENT_SOUTHWEST,
    VFRORIENT_NORTHWEST, VFRORIENT_SOUTHEAST} vfr_label_orient_t;

typedef enum {VFRWRAP_NONE, VFRWRAP_AUTO, VFRWRAP_WORD, VFRWRAP_CHAR,
    VFRWRAP_WORD_CHAR} vfr_label_wrap_t;

typedef struct vfr_style_s {
    uint64_t bgcolor;
    uint64_t fgcolor;
    int size;
    vfr_label_place_t label_place;
    char *label_field;
    uint64_t label_color;
    char *label_text;
    double label_size;
} vfr_style_t;

typedef struct vfr_label_style_s {
   char *text; // label text, possibly marked up
   //vfr_label_wrap_t wrap; // wrap mode, maps to Pango wrap modes
   //int width; // width, for wrapping, in ems
   //uint64_t fill; // fill color, default: #000000
   //uint64_t halo_fill; // halo fill, if halo_radius is gt 0, this color is used
   //double halo_radius; // radius in points (?)
   //int xoff; // custom offset, effect depends upon placement/orientation
   //int yoff; // custom offset, effect depends upon placement/orientation
   int x; 
   int y; 
   uint64_t color; 
   double size; // size in points
   //double rotate; // angle to rotate text
   //vfr_label_place_t place; // placement mode
   //vfr_label_orient_t orient; // orientation, diff. meanings for diff. settings
} vfr_label_style_t;

typedef struct vfr_list_s {
    void *dat;
    struct vfr_list_s *next;
    struct vfr_list_s *prev;
} vfr_list_t;

const char *g_progname;

static void usage(void);

static int runrender(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runversion(int argc, char **argv);

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm);
static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext);
static int vfr_draw_geom(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style, vfr_list_t **llist);
static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
    double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
    double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
    double pxw, double pxh, vfr_style_t *style);
static int eval_feature_style(lua_State *L, OGRFeatureH f, vfr_style_t *style);
static int synch_style_table(lua_State *L, vfr_style_t *style);
static int vfr_draw_label(cairo_t *cr, vfr_label_style_t *lstyle);
static vfr_label_style_t* vfr_label_style_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr, 
    double pxw, double pxh, vfr_style_t *style);
static vfr_label_style_t* vfr_label_style_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr,
    double pxw, double pxh, vfr_style_t *style);
static vfr_label_style_t* vfr_label_style_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr,
    double pxw, double pxh, vfr_style_t *style);

static vfr_list_t* vfr_list_new(void *dat);

int main(int argc, char **argv) {
    g_progname = argv[0];
    if(argc < 2) usage();
    int rv = 0;
    OGRRegisterAll();
    if(!strcmp(argv[1], "render")) {
        rv = runrender(argc, argv);
    } else if(!strcmp(argv[1], "inform")) {
        rv = runinform(argc, argv);
    } else if(!strcmp(argv[1], "version")) {
        rv = runversion(argc, argv);
    } else {
        usage();
    }
    return rv;
}

static void usage(void){
    fprintf(stderr, "%s: the command line vector feature renderer\n", g_progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s render [-out outfile] -ht INT | -wd INT [-lua luafile] datasrc\n", g_progname);
    fprintf(stderr, "  %s inform dsrc\n", g_progname);
    fprintf(stderr, "  %s version\n", g_progname);
    fprintf(stderr, "\n");
    exit(1);
}

static int runrender(int argc, char **argv) {
    
    if(argc < 3) {
        usage();
        return(1);
    }

    char *path = NULL;
    char *outfilenm = NULL;
    char *luafilenm = NULL;
    vfr_style_t style = {
        0xffffff,0x000000,1, //fgcolor, bgcolor, size
        VFRLABEL_NONE,NULL,0xffffff,NULL,25.0 // label placement, field, color, text
    };
    int iw, ih, i;
    iw = 0;
    ih = 0;
    char *end;
    unsigned long long ullval;
    int ival;
    for(i=2; i<argc; i++) {
        if(!path && argv[i][0] == '-') {
            if(!strcmp(argv[i], "-ht")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                ih = atoi(argv[i]);
            } else if(!strcmp(argv[i], "-wd")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                iw = atoi(argv[i]);
            } else if(!strcmp(argv[i], "-out")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                outfilenm = argv[i];
            } else if(!strcmp(argv[i], "-fg")) {
                ullval = strtoull(argv[++i], &end, 16);
                if(ullval == 0 && end == argv[i]) {
                    usage();
                    return 1;
                } else if(ullval == ULLONG_MAX && errno) {
                    usage();
                    return 1;
                } else if(*end) {
                    usage();
                    return 1;
                } else {
                    style.fgcolor = ullval;
                }
            } else if(!strcmp(argv[i], "-bg")) {
                ullval = strtoull(argv[++i], &end, 16);
                if(ullval == 0 && end == argv[i]) {
                    usage();
                    return 1;
                } else if(ullval == ULLONG_MAX && errno) {
                    usage();
                    return 1;
                } else if(*end) {
                    usage();
                    return 1;
                } else {
                    style.bgcolor = ullval;
                }
            } else if(!strcmp(argv[i], "-sz")) {
                ival = atoi(argv[++i]);
                style.size = ival;
            } else if(!strcmp(argv[i], "-lua")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                luafilenm = argv[i];
            } else {
                usage();
                return 1;
            }
        } else if(!path) {
            path = argv[i];
        } else {
            usage();
            return 1;
        }
    }

    if(iw <= 0 && ih <= 0) {
        usage();
        return 1;
    } 

    int rv;

    if(outfilenm == NULL) {
        rv = implrender(path, iw, ih, "vfr_out.png", &style, luafilenm);
    } else {
        rv = implrender(path, iw, ih, outfilenm, &style, luafilenm);
    }

    return rv;
}

static int runinform(int argc, char **argv) {
    if(argc < 3) {
        usage();
        return 1;
    }
    OGRDataSourceH src;
    OGRSFDriverH drvr;
    src = OGROpen(argv[2], FALSE, &drvr);
    if(src == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[2], CPLGetLastErrorMsg());
        return 1;
    }
    const char *srcnm = OGR_DS_GetName(src);
    int srclcount = OGR_DS_GetLayerCount(src);
    printf("%s: %d layer(s)\n", srcnm, srclcount);
    int i = 0;
    int lfcount;
    const char *lgeomtype = "UKNOWN";
    OGRLayerH layer;
    OGRFeatureH ftr;
    char *srswkt;
    OGREnvelope ext;
    for(i =0; i<srclcount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, TRUE);
        OGR_L_ResetReading(layer);
        if((ftr = OGR_L_GetNextFeature(layer)) != NULL) {
            lgeomtype = OGR_G_GetGeometryName(OGR_F_GetGeometryRef(ftr));
        }
        printf("\tlayer %d: %s [%s] - %d feature(s)\n", i, OGR_L_GetName(layer), 
            lgeomtype, lfcount);
        OGR_L_GetExtent(layer, &ext, 0);
        printf("\textent: %0.2f, %0.2f, %0.2f, %0.2f\n", 
            ext.MinX, ext.MinY, ext.MaxX, ext.MaxY);
        OSRExportToWkt(OGR_L_GetSpatialRef(layer), &srswkt);
        printf("\t%s\n", srswkt);
    }
    OGR_DS_Destroy(src);
    return 0;
}

static int runversion(int argc, char **argv) {
    printf("VFR Vector Feature Renderer version %s for %s\n",
      "0.0.0", VFRSYSNAME);
    return 0;
}

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm) {
    
    // open shapefile
    OGRDataSourceH src;
    OGRSFDriverH drvr;
    src = OGROpen(datpath, FALSE, &drvr);
    if(src == NULL) {
        fprintf(stderr, "could not open %s: %s\n", datpath, CPLGetLastErrorMsg());
        return 1;
    }

    // init lua, load luafile if available
    lua_State *L;
    if(luafilenm != NULL) {
        fprintf(stderr, "opening lua file: %s\n", luafilenm);
        L = lua_open();
        luaL_openlibs(L);
        if(luaL_loadfile(L, luafilenm) || lua_pcall(L, 0, 0, 0)) {
            fprintf(stderr, "could not load luafile %s: %s\n", luafilenm, lua_tostring(L, -1));
            lua_close(L);
            return 1;
        }

        // load default style (if available)
        // this should call a fxn, something like synch_style_table(*style, lua_state)
        lua_getglobal(L, "vfr_style");
        synch_style_table(L, style);
    }

    // get max extent for all layers
    int i, layercount, lfcount;
    long j;
    OGREnvelope ext;
    vfr_ds_extent(src, &ext);
    fprintf(stderr, "got extents: max = (%f, %f), min = (%f, %f)\n", ext.MaxX, ext.MaxY, ext.MinX, ext.MaxY);

    // get pixel-to-map unit ratio
    double pxw, pxh;
    if(iw == 0 && ih == 0) {
        usage();
        return 1;
    } else if(iw == 0) {
        iw = ih * ((ext.MaxX - ext.MinX)/(ext.MaxY - ext.MinY));
    } else if(ih == 0) {
        ih = iw * ((ext.MaxY - ext.MinY)/(ext.MaxX - ext.MinX));
    }
    pxw = (ext.MaxX - ext.MinX)/iw;
    pxh = (ext.MaxY - ext.MinY)/ih;

    // draw
    cairo_surface_t *surface;
    cairo_t *cr;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    cr = cairo_create(surface);
    cairo_rectangle(cr, 0, 0, iw, ih);
    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_fill(cr);
    OGRGeometryH geom;
    OGRFeatureH ftr;
    OGRLayerH layer;
    layercount = OGR_DS_GetLayerCount(src);

    vfr_list_t *llist = NULL;
    vfr_list_t *curnode = NULL;
    fprintf(stderr, "loading layers (%d)\n", layercount);
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, 0);
        fprintf(stderr, "- %d feature(s)\n", lfcount);
        for(j=0; j<lfcount; j++) {
            ftr = OGR_L_GetFeature(layer, j);
            geom = OGR_F_GetGeometryRef(ftr);
            if(geom == NULL) {
                fprintf(stderr, "skipping null geometry w/ fid = %ld\n", 
                    OGR_F_GetFID(ftr));
                continue;
            }
            if(luafilenm != NULL) {
                eval_feature_style(L, ftr, style);
            }
            vfr_draw_geom(cr, ftr, geom, &ext, pxw, pxh, style, &llist);
            OGR_F_Destroy(ftr);
        }
        curnode = llist;
        while(curnode->prev != NULL) {
            curnode = curnode->prev;
        }
        while(curnode != NULL) {
            vfr_draw_label(cr, (vfr_label_style_t *)(curnode->dat));
            curnode = curnode->next;
        }
    }
    fprintf(stderr, "writing to %s\n", outfilenm);
    cairo_surface_write_to_png(surface, outfilenm);
    if(luafilenm != NULL) {
        lua_close(L);
    }
    return 0;
}

static int eval_feature_style(lua_State *L, OGRFeatureH ftr, vfr_style_t *style) {
    lua_getglobal(L, "vfrFeatureStyle");
    if(!lua_isfunction(L, -1)) {
        fprintf(stderr, "vfrFeatureStyle is not a lua function");
        lua_pop(L, 1);
        return 1;
    }
    lua_newtable(L);
    int fldcount = OGR_F_GetFieldCount(ftr);
    int i;
    OGRFieldDefnH fdef;
    for(i=0; i<fldcount; i++) {
        fdef = OGR_F_GetFieldDefnRef(ftr, i);
        lua_pushstring(L, OGR_Fld_GetNameRef(fdef));
        switch(OGR_Fld_GetType(fdef)) {
            case OFTString:
            case OFTDate:
            case OFTTime:
                lua_pushstring(L, OGR_F_GetFieldAsString(ftr, i));
                break;
            case OFTInteger:
            case OFTReal:
                lua_pushnumber(L, OGR_F_GetFieldAsDouble(ftr, i));
                break;
            default:
                fprintf(stderr, "skipping unimplemented field type (pushing nil) for field named '%s'\n",
                    OGR_Fld_GetNameRef(fdef));
                lua_pushnil(L);
                break;
        }
        lua_settable(L, -3);
    }
    if(lua_pcall(L, 1, 1, 0) != 0) {
        fprintf(stderr, "error calling vfrFeatureStyle: %s\n", lua_tostring(L, -1));
    }
    if(!lua_istable(L, -1)) {
        fprintf(stderr, "vfrFeatureStyle did not return a table\n");
        return 1;
    }
    synch_style_table(L, style);
    return 0;
}

// assumes a valid style table is pushed on the lua stack
static int synch_style_table(lua_State *L, vfr_style_t *style) {

    int objlen;

    if(!lua_istable(L, -1)) {
        fprintf(stderr, "style is not a valid lua table");
    } else {
        lua_pushstring(L, "fgcolor");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fgcolor = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fgcolor |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fgcolor |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
        lua_pushstring(L, "bgcolor");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->bgcolor = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->bgcolor |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->bgcolor |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
        lua_pushstring(L, "size");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            style->size = (int)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_place");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            style->label_place = (int)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_size");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            style->label_size = (double)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_field");
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            // we need to copy this string, not juse use the pointer...
            objlen = lua_objlen(L, -1);
            // free old string
            if(style->label_field != NULL) {
                free(style->label_field);
            }
            style->label_field = malloc(objlen+1);
            if(style->label_field == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(style->label_field, lua_tostring(L, -1), objlen);
            style->label_field[objlen] = '\0';
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_text");
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            // we need to copy this string, not juse use the pointer...
            objlen = lua_objlen(L, -1);
            // free old string
            if(style->label_text != NULL) {
                free(style->label_text);
            }
            style->label_text = malloc(objlen+1);
            if(style->label_text == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(style->label_text, lua_tostring(L, -1), objlen);
            style->label_text[objlen] = '\0';
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_color");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_color = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_color |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_color |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
    }
    return 0;
}

static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext) {
    int i;
    int layercount = OGR_DS_GetLayerCount(ds);
    OGRLayerH layer;
    OGREnvelope lext = {};
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(ds, i);
        OGR_L_GetExtent(layer, &lext, 1); // arg 3 is bForce (exp. but nec.)
        if(i == 0 || lext.MinX < ext->MinX) {
            ext->MinX = lext.MinX;
        }
        if(i == 0 || lext.MinY < ext->MinY) {
            ext->MinY = lext.MinY;
        }
        if(i == 0 || lext.MaxX > ext->MaxX) {
            ext->MaxX = lext.MaxX;
        }
        if(i == 0 || lext.MaxY > ext->MaxY) {
            ext->MaxY = lext.MaxY;
        }
    }
    return 0;
}

static int vfr_draw_geom(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style, vfr_list_t **llist) {

    vfr_list_t *newnode = NULL;
    vfr_label_style_t *lstyle = NULL;
    OGRGeometryH geom2;

    int g, gcount;
    
    switch(OGR_G_GetGeometryType(geom)) {
        case wkbPoint:
            fprintf(stderr, "rendering point\n");
            vfr_draw_point(cr, geom, ext, pxw, pxh, style);
            lstyle = vfr_label_style_point(cr, geom, ext, ftr, pxw, pxh, style);
            if(lstyle != NULL) {
                newnode = vfr_list_new(lstyle);
                if((*llist) == NULL) {
                    *llist = newnode;
                } else {
                    newnode->prev = *llist;
                    (*llist)->next = newnode;
                    *llist = (*llist)->next;
                }
            }
            break;
        case wkbPolygon:
            fprintf(stderr, "rendering polygon\n");
            vfr_draw_polygon(cr, geom, ext, pxw, pxh, style);
            lstyle = vfr_label_style_polygon(cr, geom, ext, ftr, pxw, pxh, style);
            if(lstyle != NULL) {
                newnode = vfr_list_new(lstyle);
                if((*llist) == NULL) {
                    *llist = newnode;
                } else {
                    newnode->prev = *llist;
                    (*llist)->next = newnode;
                    *llist = (*llist)->next;
                }
            }
            break;
        case wkbLineString:
            fprintf(stderr, "rendering linestring\n");
            vfr_draw_linestring(cr, geom, ext, pxw, pxh, style);
            lstyle = vfr_label_style_linestring(cr, geom, ext, ftr, pxw, pxh, style);
            if(lstyle != NULL) {
                newnode = vfr_list_new(lstyle);
                if((*llist) == NULL) {
                    *llist = newnode;
                } else {
                    newnode->prev = *llist;
                    (*llist)->next = newnode;
                    *llist = (*llist)->next;
                }
            }
            break;
        case wkbLinearRing:
            printf("ring!\n");
            break;
        case wkbMultiPolygon:
            fprintf(stderr, "rendering multipolygon\n");
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_polygon(cr, geom2, ext, pxw, pxh, style);
            }
            break;
        case wkbMultiLineString:
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_linestring(cr, geom2, ext, pxw, pxh, style);
            }
            break;
        case wkbGeometryCollection:
            fprintf(stderr, "geomcollection\n");
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_geom(cr, ftr, geom2, ext, pxw, pxh, style, llist);
            }
            break;
        default:
            printf("unknown!\n");
            break;
    }

    return 0;
}

static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style) {
    double x, y, z;
    OGR_G_GetPoint(geom, 0, &x, &y, &z);
    double pxx = (x - ext->MinX)/pxw;
    double pxy = (ext->MaxY - y)/pxh;
    cairo_arc(cr, pxx, pxy, style->size, 0, 2*M_PI);
    cairo_set_source_rgb(cr, ((style->bgcolor & 0xff0000) >> 16)/256.0, 
        ((style->bgcolor & 0x00ff00) >> 8)/256.0, 
        (style->bgcolor & 0x0000ff)/256.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, ((style->fgcolor & 0xff0000) >> 16)/256.0, 
        ((style->fgcolor & 0x00ff00) >> 8)/256.0, 
        (style->fgcolor & 0x0000ff)/256.0);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    return 0;
}

static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style) {
    int pcount = OGR_G_GetPointCount(geom);
    if(!pcount) return 0;
    double x, y, z, pxx, pxy;
    int i;
    cairo_set_line_width(cr, style->size);
    cairo_set_source_rgb(cr, (style->fgcolor & 0xff0000)/0xff0000, 
        (style->fgcolor & 0x00ff00)/0x00ff00, (style->fgcolor & 0x0000ff)/0x0000ff);
    for(i = 0; i < pcount; i++) {
        OGR_G_GetPoint(geom, i, &x, &y, &z);
        pxx = (x - ext->MinX)/pxw;
        pxy = (ext->MaxY - y)/pxh;
        if(!i) {
            cairo_move_to(cr, pxx, pxy);
            continue;
        }
        cairo_line_to(cr, pxx, pxy);
    }
    cairo_stroke(cr);
    return 0;
}

static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style) {
    OGRGeometryH geom2 = OGR_G_GetGeometryRef(geom, 0);
    int p, ptcount = OGR_G_GetPointCount(geom2);
    double x, y, z, pxx, pxy;
    for(p=0; p<ptcount; p++) {
        OGR_G_GetPoint(geom2, p, &x, &y, &z);
        pxx = (x - ext->MinX)/pxw;
        pxy = (ext->MaxY - y)/pxh;
        if(!p) {
            cairo_move_to(cr, pxx, pxy);
        } else {
            cairo_line_to(cr, pxx, pxy);
        }
    }
    OGR_G_GetPoint(geom2, 0, &x, &y, &z);
    pxx = (x - ext->MinX)/pxw;
    pxy = (ext->MaxY - y)/pxh;
    cairo_line_to(cr, pxx, pxy);
    cairo_set_source_rgb(cr, ((style->bgcolor & 0xff0000) >> 16)/256.0, 
        ((style->bgcolor & 0x00ff00) >> 8)/256.0, 
        (style->bgcolor & 0x0000ff)/256.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, ((style->fgcolor & 0xff0000) >> 16)/256.0, 
        ((style->fgcolor & 0x00ff00) >> 8)/256.0, 
        (style->fgcolor & 0x0000ff)/256.0);
    cairo_set_line_width(cr, style->size);
    cairo_stroke(cr);
    return 0;
}

static vfr_label_style_t* vfr_label_style_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr,
    double pxw, double pxh, vfr_style_t *style) {

    double x, y, z, pxx, pxy;
    const char *txt;
    int fieldidx;

    vfr_label_style_t *lstyle = NULL;

    // if label_place set to "none", bail
    if(style->label_place == VFRLABEL_NONE) {
        return NULL;
    }

    if(style->label_text != NULL) {
        txt = style->label_text;
    } else if(style->label_field != NULL) {
        fieldidx = OGR_F_GetFieldIndex(ftr, style->label_field);
        if(fieldidx < 0) {
            fprintf(stderr, "label field '%s' not found\n", style->label_field);
            return NULL;
        }
        txt = OGR_F_GetFieldAsString(ftr, fieldidx);
    } else {
        fprintf(stderr, "labels turned on, but no field or text specified\n");
        return NULL;
    }

    lstyle = malloc(sizeof(vfr_label_style_t));
    
    lstyle->text = malloc(strlen(txt)+1);
    if(lstyle->text == NULL) {
        fprintf(stderr, "out of memory\n");
        return NULL;
    }
    memcpy(lstyle->text, txt, strlen(txt));
    lstyle->text[strlen(txt)] = '\0';

    // get centroid
    OGRGeometryH centrd = OGR_G_CreateGeometry(wkbPoint);
    if(OGR_G_Centroid(geom, centrd) == OGRERR_FAILURE) {
        fprintf(stderr, "could not get centroid for feature label\n");
        return NULL;
    }
    OGR_G_GetPoint(centrd, 0, &x, &y, &z);

    lstyle->size = style->label_size;
    // for now, just set fontface. in the future add this to the style
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, lstyle->size);

    cairo_text_extents_t textext;
    cairo_text_extents(cr, txt, &textext);
    
    // center all labels for now.
    pxx = (x - ext->MinX)/pxw - textext.width/2.0;
    pxy = (ext->MaxY - y)/pxh + textext.height/2.0;
    lstyle->x = pxx;
    lstyle->y = pxy;

    lstyle->color = style->label_color;
    OGR_G_DestroyGeometry(centrd);
    return lstyle;
}

static vfr_label_style_t* vfr_label_style_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr,
    double pxw, double pxh, vfr_style_t *style) {

    double x, y, z, pxx, pxy;
    const char *txt;
    int fieldidx;
    vfr_label_style_t *lstyle = NULL;

    // if label_place set to "none", bail
    if(style->label_place == VFRLABEL_NONE) {
        return NULL;
    }

    if(style->label_text != NULL) {
        txt = style->label_text;
    } else if(style->label_field != NULL) {
        fieldidx = OGR_F_GetFieldIndex(ftr, style->label_field);
        if(fieldidx < 0) {
            fprintf(stderr, "label field '%s' not found\n", style->label_field);
            return NULL;
        }
        txt = OGR_F_GetFieldAsString(ftr, fieldidx);
    } else {
        fprintf(stderr, "labels turned on, but no field or text specified\n");
        return NULL;
    }
    
   lstyle = malloc(sizeof(vfr_label_style_t));
    
    lstyle->text = malloc(strlen(txt)+1);
    if(lstyle->text == NULL) {
        fprintf(stderr, "out of memory\n");
        return NULL;
    }
    memcpy(lstyle->text, txt, strlen(txt));
    lstyle->text[strlen(txt)] = '\0';

    // get xyz
    OGR_G_GetPoint(geom, 0, &x, &y, &z);

    lstyle->size = style->label_size;
    // for now, just set fontface. in the future add this to the style
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, lstyle->size);

    cairo_text_extents_t textext;
    cairo_text_extents(cr, txt, &textext);
    // color scaling?
    // center all labels for now.
    pxx = (x - ext->MinX)/pxw + textext.height;
    pxy = (ext->MaxY - y)/pxh + textext.height;
    lstyle->x = pxx;
    lstyle->y = pxy;

    lstyle->color = style->label_color;
    return lstyle;
}

static vfr_label_style_t* vfr_label_style_linestring(cairo_t *cr, 
        OGRGeometryH geom, OGREnvelope *ext, OGRFeatureH ftr,
        double pxw, double pxh, vfr_style_t *style) {

    vfr_label_style_t *lstyle = NULL;
    double x, y, z, pxx, pxy;
    const char *txt;
    int fieldidx;


    if(style->label_place == VFRLABEL_NONE) {
        return NULL;
    }
    
    if(style->label_text != NULL) {
        txt = style->label_text;
    } else if(style->label_field != NULL) {
        fieldidx = OGR_F_GetFieldIndex(ftr, style->label_field);
        if(fieldidx < 0) {
            fprintf(stderr, "label field '%s' not found\n", style->label_field);
            return NULL;
        }
        txt = OGR_F_GetFieldAsString(ftr, fieldidx);
    } else {
        fprintf(stderr, "labels turned on, but no field or text specified\n");
        return NULL;
    }

    lstyle = malloc(sizeof(vfr_label_style_t));

    lstyle->text = malloc(strlen(txt)+1);
    if(lstyle->text == NULL) {
        fprintf(stderr, "out of memory\n");
        return NULL;
    }
    memcpy(lstyle->text, txt, strlen(txt));
    lstyle->text[strlen(txt)] = '\0';

    // get centroid
    OGRGeometryH centrd = OGR_G_CreateGeometry(wkbPoint);
    if(OGR_G_Centroid(geom, centrd) == OGRERR_FAILURE) {
        fprintf(stderr, "could not get centroid for feature label\n");
        return NULL;
    }
    OGR_G_GetPoint(centrd, 0, &x, &y, &z);

    lstyle->size = style->label_size;
    // for now, just set fontface. in the future add this to the style
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, lstyle->size);

    cairo_text_extents_t textext;
    cairo_text_extents(cr, txt, &textext);
    
    // center all labels for now.
    pxx = (x - ext->MinX)/pxw - textext.width/2.0;
    pxy = (ext->MaxY - y)/pxh + textext.height/2.0;
    lstyle->x = pxx;
    lstyle->y = pxy;

    lstyle->color = style->label_color;
    OGR_G_DestroyGeometry(centrd);

    return lstyle;
}

static int vfr_draw_label(cairo_t *cr, vfr_label_style_t *lstyle) {
    // lstyle: description, size, family, face
    // lstyle: fill, halo_radius, halo_fill...
    // lstyle: placement, orientation, xoff, yoff, rotate...
    // wrapping? markup (for lua)?
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, lstyle->size);
    cairo_set_source_rgb(cr, 
        ((lstyle->color & 0xff0000) >> 16)/256.0, 
        ((lstyle->color & 0x00ff00) >> 8)/256.0,
        (lstyle->color & 0x0000ff)/256.0);
    cairo_move_to(cr, lstyle->x, lstyle->y);
    cairo_show_text(cr, lstyle->text);
    return 0;
}

static vfr_list_t* vfr_list_new(void *dat) {
    vfr_list_t *ptr = malloc(sizeof(vfr_list_t));
    if(ptr == NULL) fprintf(stderr, "out of memory\n");
    ptr->dat = dat;
    ptr->next = NULL;
    ptr->prev = NULL;
    return ptr;
}
