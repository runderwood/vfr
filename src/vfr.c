#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"
#include "cairo-svg.h"

#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__linux__)
#define VFRSYSNAME "Linux"
#else
#define VFRSYSNAME "Unknown"
#endif

#define VFRLABEL_CENTER 0x00000001 // center (varies)
#define VFRLABEL_OFFSET 0x00000002 // offset plcmt (line)
#define VFRLABEL_TANGNT 0x00000004 // tangential plcmt (line)
#define VFRLABEL_HORIZN 0x00000008 // horizontal plcmt (line)
#define VFRLABEL_CURVED 0x00000010 // curved plcmt (line)
#define VFRLABEL_PERPDI 0x00000020 // perpendicular plcmt (line)
#define VFRLABEL_PREFNW 0x00000040 // prefer northwest plcmt (point)
#define VFRLABEL_PREFNE 0x00000080 // " northeast
#define VFRLABEL_PREFSE 0x00000100 // " southeast
#define VFRLABEL_PREFSW 0x00000200 // " southwest
#define VFRLABEL_SPREAD 0x00000400 // spread to fill (poly)
#define VFRLABEL_UPCASE 0x00000800 // text transform to uppercase
#define VFRLABEL_OVRLAP 0x00001000 // overlap (for poly. bounds behavior, etc.)
#define VFRLABEL_NOCROS 0x00002000 // no cross (for shorelines, etc.)
#define VFRLABEL_FLDFIR 0x00004000 // use field first, if exists. text val sec.
#define VFRLABEL_WRPNON 0x00008000 // nowrap
#define VFRLABEL_WRPWRD 0x00010000 // wrap at word
#define VFRLABEL_WRPCHR 0x00020000 // wrap at character
#define VFRLABEL_WRPWCH 0x00040000 // wrap at word, then char (tolerance?)

#define VFRHATCH_NONE 0x00
#define VFRHATCH_NONE_S "none"
#define VFRHATCH_LINE 0x01    // hatch w/ lines
#define VFRHATCH_LINE_S "lines"
#define VFRHATCH_CROSS 0x02    // hatch w/ crosses
#define VFRHATCH_CROSS_S "crosses"
#define VFRHATCH_DOT  0x03    // hatch w/ dots
#define VFRHATCH_DOT_S "dots"
#define VFRHATCH_DOTLINE 0x04 // hatch w/ dotted lines
#define VFRHATCH_DOTLINE_S "dotline"
#define VFRHATCH_DOTCROS 0x05 // hatch w/ dotted crosses

#define VFRDEFAULT_FONTDESC "Courier New 12"

typedef enum {VFRPLACE_NONE, VFRPLACE_AUTO, VFRPLACE_CENTER, VFRPLACE_POINT,
    VFRPLACE_LINE} vfr_label_place_t;

// TODO: map config object (w/h, bgcolor...) w/ access to datasrc and then available in ftr style fn
// TODO: check srs'es
// TODO: hatches, other fills
// TODO: explicitly reset default in synch
// TODO: style dashes, hashes (patterns)
// TODO: ranges on numeric fields? (e.g. _vfr_layer_ranges...)
typedef struct vfr_style_s {
    uint64_t fill;
    int fill_opacity;
    char *hatch_pattern;
    double hatch_rotate;
    double hatch_scale;
    uint64_t stroke;
    int stroke_opacity;
    int size;
    vfr_label_place_t label_place;
    char *label_field;
    uint64_t label_fill;
    int label_opacity;
    char *label_text;
    char *label_fontdesc;
    uint32_t label_flags;
    double label_xoff;
    double label_yoff;
    double label_halo_size;
    uint64_t label_halo_fill;
    double label_rotate;
    double label_width; // width for wrapping (in ems? or points?)
} vfr_style_t;

/*typedef struct vfr_list_s {
    void *dat;
    struct vfr_list_s *next;
    struct vfr_list_s *prev;
} vfr_list_t;*/

typedef double param_t;

typedef struct {
    cairo_path_t *path;
    param_t *params;
    double length;
} paramd_path_t;

const char *g_progname;

static void usage(void);

static int runrender(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runversion(int argc, char **argv);
static int runfonts(int argc, char **argv);

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm);
static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext);
static int vfr_draw_geom(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_label(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int eval_feature_style(lua_State *L, OGRFeatureH f, vfr_style_t *style);
static int synch_style_table(lua_State *L, vfr_style_t *style);


static double euclid_dist(cairo_path_data_t *pt1, cairo_path_data_t *pt2);
static param_t* parametrize_path(cairo_path_t *path, double *plen);
static cairo_path_t* get_linear_label_path(cairo_t *cr, paramd_path_t *parpath,
        double txtwidth, double placeat);
static void transform_label_points(cairo_path_t *lyopath, paramd_path_t *paramd_lblpath);
static void transform_label_point(paramd_path_t *paramd_lblpath, double *xptr, double *yptr);
static cairo_pattern_t* make_fill_pattern(vfr_style_t *style);
static void make_label_halo(cairo_t *cr, cairo_path_t* plyopath, vfr_style_t *style);
//static int spline_knots(int knots, double *tx, double *ty, double *cpx, double *cpy);

static double vfr_color_compextr(uint64_t color, char c);
// static vfr_list_t* vfr_list_new(void *dat);

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
    } else if(!strcmp(argv[1], "fonts")) {
        rv = runfonts(argc, argv);
    } else {
        usage();
    }
    return rv;
}

static void usage(void){
    fprintf(stderr, "%s: the command line vector feature renderer\n", g_progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s fonts\n", g_progname);
    fprintf(stderr, "  %s inform dsrc\n", g_progname);
    fprintf(stderr, "  %s render [-out outfile] -ht INT | -wd INT [-lua luafile] datasrc\n", g_progname);
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
    // init style struct
    vfr_style_t style = {
        0xffffff, 100, NULL, 0.0, 1.0, // fill, fopacity, fill pattern, pattern rotate, pattern scale  
        0x000000, 100, 1, // stroke, sopacity, size
        VFRPLACE_NONE, NULL, 0xffffff, 100, NULL, NULL, // label placement, field, color, text, fontdesc
        0, 0, 0, 0, 0x01000000, 0, -1 // flags, xoff, yoff, halo rad, halo fill, label rot, label w
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
            } else if(!strcmp(argv[i], "-st")) {
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
                    style.stroke = ullval;
                }
            } else if(!strcmp(argv[i], "-fl")) {
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
                    style.fill = ullval;
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
        rv = implrender(path, iw, ih, "vfr_out.svg", &style, luafilenm);
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

static int runfonts(int argc, char **argv) {
    printf("coming soon\n");

    PangoFontMap* fmap = pango_cairo_font_map_get_default();
    PangoFontFamily** fams;
    PangoFontFamily* fam;
    PangoFontFace** faces;
    PangoFontFace* face;
    int numfams, numfaces, i, j;
    const char* famname;
    const char* facename;

    pango_font_map_list_families(fmap, &fams, &numfams);
    for(i=0; i<numfams; i++) {
        fam = fams[i];
        famname = pango_font_family_get_name(fam);
        printf("%s:", famname);
        pango_font_family_list_faces(fam, &faces, &numfaces);
        for(j=0; j<numfaces; j++) {
            face = faces[j];
            facename = pango_font_face_get_face_name(face);
            printf(" %s", facename);
            if(j<(numfaces-1)) printf(",");
        }
        printf("\n");
        g_free(faces);
    }
    g_free(fams);
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
        lua_getglobal(L, "vfr_style");
        synch_style_table(L, style);
    }

    // get max extent for all layers
    int i, layercount, lfcount;
    long j;
    OGREnvelope ext;
    vfr_ds_extent(src, &ext);
    fprintf(stderr, "got extents: \n\tmax = (%f, %f)\n\tmin = (%f, %f)\n", ext.MaxX, ext.MaxY, ext.MinX, ext.MinY);

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
    // surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    surface = cairo_svg_surface_create(outfilenm, iw, ih);
    cairo_svg_surface_restrict_to_version(surface, CAIRO_SVG_VERSION_1_2);
    cr = cairo_create(surface);
    // cairo_rectangle(cr, 0, 0, iw, ih);
    // cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_fill(cr);
    // make label surface/context
    cairo_surface_t *lsurface;
    cairo_t *lcr;
    cairo_rectangle_t surfext = {0.0, 0.0, iw, ih};
    lsurface = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &surfext);
    lcr = cairo_create(lsurface);
    // geom/feature/layer variables
    OGRGeometryH geom;
    OGRFeatureH ftr;
    OGRLayerH layer;
    layercount = OGR_DS_GetLayerCount(src);

    fprintf(stderr, "loading layers (%d)\n", layercount);
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, 0);
        fprintf(stderr, "layer \"%s\": %d feature(s)\n", OGR_L_GetName(layer), lfcount);
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
            if(j && !(j % 200)) {
                fprintf(stderr, ".");
                if(!(j % 10000)) {
                    fprintf(stderr, " (%ld/%d)\n", j, lfcount);
                }
            } else if(j == (lfcount-1)) {
                fprintf(stderr, "done.\n");
            }
            vfr_draw_geom(cr, ftr, geom, &ext, pxw, pxh, style);
            vfr_draw_label(lcr, ftr, geom, &ext, pxw, pxh, style);
            OGR_F_Destroy(ftr);
        }
    }
    fprintf(stderr, "painting labels over shapes...\n");
    cairo_set_source_surface(cr, lsurface, 0.0, 0.0);
    cairo_paint(cr);
    fprintf(stderr, "writing to %s...", outfilenm);
    // fprintf(stderr, "writing to %s\n", outfilenm);
    // cairo_surface_write_to_png(surface, outfilenm);
    // cairo_surface_write_to_png(lsurface, "test.png");
    cairo_destroy(lcr);
    cairo_surface_destroy(lsurface);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
    if(luafilenm != NULL) {
        lua_close(L);
    }
    fprintf(stderr, "done.\n");
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
    OGRFeatureDefnH ftrdef;
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

    // add metadata for layer name, geom type, etc.
    ftrdef = OGR_F_GetDefnRef(ftr);
    lua_pushstring(L, "_vfr_layer");
    lua_pushstring(L, OGR_FD_GetName(ftrdef));
    lua_settable(L, -3);
    lua_pushstring(L, "_vfr_geomtype");
    lua_pushstring(L, OGR_G_GetGeometryName(OGR_F_GetGeometryRef(ftr)));
    lua_settable(L, -3);

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
        fprintf(stderr, "style is not a valid lua table\n");
        return -1;
    }

    lua_pushstring(L, "stroke");
    lua_gettable(L, -2);
    if(lua_istable(L, -1)) {
        lua_pushstring(L, "r");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->stroke = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "g");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->stroke |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "b");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->stroke |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
        }
        lua_pop(L, 2);
    } else {
        style->stroke = 0x01000000;
        lua_pop(L, 1);
    }
    lua_pushstring(L, "fill");
    lua_gettable(L, -2);
    if(lua_istable(L, -1)) {
        lua_pushstring(L, "r");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->fill = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "g");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->fill |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "b");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->fill |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
        }
        lua_pop(L, 2);
    } else {
        style->fill = 0x01000000;
        lua_pop(L, 1);
    }
    lua_pushstring(L, "fill_opacity");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->fill_opacity = (int)lua_tonumber(L, -1);
        if(style->fill_opacity < 0) {
            style->fill_opacity = 0;
        } else if(style->fill_opacity > 100) {
            style->fill_opacity = 100;
        }
    }
    lua_pop(L, 1);
    lua_pushstring(L, "stroke_opacity");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->stroke_opacity = (int)lua_tonumber(L, -1);
        if(style->stroke_opacity < 0) {
            style->stroke_opacity = 0;
        } else if(style->stroke_opacity > 100) {
            style->stroke_opacity = 100;
        }
    }
    lua_pop(L, 1);
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
    lua_pushstring(L, "label_fontdesc");
    lua_gettable(L, -2);
    if(lua_isstring(L, -1)) {
        objlen = lua_objlen(L, -1);
        if(style->label_fontdesc != NULL) {
            free(style->label_fontdesc);
        }
        style->label_fontdesc = malloc(objlen+1);
        if(style->label_fontdesc == NULL) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        memcpy(style->label_fontdesc, lua_tostring(L, -1), objlen);
        style->label_fontdesc[objlen] = '\0';
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
    lua_pushstring(L, "label_fill");
    lua_gettable(L, -2);
    if(lua_istable(L, -1)) {
        lua_pushstring(L, "r");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_fill = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "g");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_fill |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "b");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_fill |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushstring(L, "label_opacity");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->label_opacity = (int)lua_tonumber(L, -1);
        if(style->label_opacity < 0) {
            style->label_opacity = 0;
        } else if(style->label_opacity > 100) {
            style->label_opacity = 100;
        }
    }
    lua_pop(L, 1);
    lua_pushstring(L, "fill_pattern");
    lua_gettable(L, -2);
    if(lua_isstring(L, -1)) {
        objlen = lua_objlen(L, -1);
        if(style->hatch_pattern != NULL) {
            free(style->hatch_pattern);
        }
        style->hatch_pattern = malloc(objlen+1);
        if(style->hatch_pattern == NULL) {
            exit(1);
        }
        memcpy(style->hatch_pattern, lua_tostring(L, -1), objlen);
        style->hatch_pattern[objlen] = '\0';   
    } else {
        if(style->hatch_pattern != NULL) {
            free(style->hatch_pattern);
        }
        style->hatch_pattern = NULL;
    }
    lua_pop(L, 1);
    lua_pushstring(L, "fill_rotate");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->hatch_rotate = lua_tonumber(L, -1)*(180.0/M_PI); // lua val is in degrees, make rads
    } else {
        style->hatch_rotate = 0.0;
    }
    lua_pop(L, 1);
    lua_pushstring(L, "fill_scale");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->hatch_scale = lua_tonumber(L, -1);
    } else {
        style->hatch_scale = 1.0;
    }
    lua_pop(L, 1);
    lua_pushstring(L, "label_halo_fill");
    lua_gettable(L, -2);
    if(lua_istable(L, -1)) {
        lua_pushstring(L, "r");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_halo_fill = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "g");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_halo_fill |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
        }
        lua_pop(L, 1);
        lua_pushstring(L, "b");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
            style->label_halo_fill |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushstring(L, "label_halo_size");
    lua_gettable(L, -2);
    if(lua_isnumber(L, -1)) {
        style->label_halo_size = lua_tonumber(L, -1);
    } else {
        style->label_halo_size = 1.0;
    }
    lua_pop(L, 1);
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
        double pxw, double pxh, vfr_style_t *style) {

    OGRGeometryH geom2;

    int g, gcount;
    //fprintf(stderr, "rendering %s\n", OGR_G_GetGeometryName(geom));
    
    switch(wkbFlatten(OGR_G_GetGeometryType(geom))) {
        case wkbPoint:
            vfr_draw_point(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbPolygon:
            vfr_draw_polygon(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLineString:
            vfr_draw_linestring(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLinearRing:
            printf("ring!\n");
            break;
        case wkbMultiPolygon:
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
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_geom(cr, ftr, geom2, ext, pxw, pxh, style);
            }
            break;
        default:
            printf("unknown geometry type (%s)- ignoring!\n", OGR_G_GetGeometryName(geom));
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
    if(style->fill <= 0xffffff) {
        cairo_set_source_rgba(cr, 
                vfr_color_compextr(style->fill, 'r'), 
                vfr_color_compextr(style->fill, 'g'), 
                vfr_color_compextr(style->fill, 'b'),
                ((float)style->fill_opacity)/100.0);
        if(style->stroke <= 0xffffff) {
            cairo_fill_preserve(cr);
        } else {
            cairo_fill(cr);
        }
    }
    if(style->stroke <= 0xffffff) {
        cairo_set_source_rgba(cr, 
                vfr_color_compextr(style->stroke, 'r'),
                vfr_color_compextr(style->stroke, 'g'),
                vfr_color_compextr(style->stroke, 'b'),
                ((float)style->stroke_opacity)/100.0);
        cairo_set_line_width(cr, style->size);
        cairo_stroke(cr);
    }
    return 0;
}

static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style) {
    int pcount = OGR_G_GetPointCount(geom);
    if(!pcount) return 0;
    double x, y, z, pxx, pxy;
    int i;
    cairo_set_line_width(cr, style->size);
    cairo_set_source_rgba(cr,
            vfr_color_compextr(style->stroke, 'r'),
            vfr_color_compextr(style->stroke, 'g'),
            vfr_color_compextr(style->stroke, 'b'),
            ((float)style->stroke_opacity)/100.0);
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
    cairo_pattern_t* hatchpat = NULL;
    
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
    if(style->fill <= 0xffffff) {
        hatchpat = make_fill_pattern(style);
        if(hatchpat != NULL) {
            cairo_set_source(cr, hatchpat);
        } else {
            cairo_set_source_rgba(cr, 
                    vfr_color_compextr(style->fill, 'r'), 
                    vfr_color_compextr(style->fill, 'g'), 
                    vfr_color_compextr(style->fill, 'b'),
                    ((float)style->fill_opacity)/100.0);
        }
        if(style->stroke <= 0xffffff) {
            cairo_fill_preserve(cr);
        } else {
            cairo_fill(cr);
        }
    }
    if(hatchpat != NULL) {
        cairo_pattern_destroy(hatchpat);
    }
    if(style->stroke <= 0xffffff) {
        cairo_set_source_rgba(cr, 
                vfr_color_compextr(style->stroke, 'r'), 
                vfr_color_compextr(style->stroke, 'g'), 
                vfr_color_compextr(style->stroke, 'b'),
                ((float)style->stroke_opacity)/100.0); 
        cairo_set_line_width(cr, style->size);
        cairo_stroke(cr);
    }
    return 0;
}


static int vfr_draw_label(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom,
        OGREnvelope *ext, double pxw, double pxh, vfr_style_t *style) {
    
    if(!style->label_place) return 0;
    
    //fprintf(stderr, "label feature #%ld\n", OGR_F_GetFID(ftr));

    PangoFontDescription *fdesc;
    PangoLayout *plyo;
    int i, pcount, fieldidx;
    OGRGeometryH centroid;
    OGREnvelope envelope;
    cairo_path_t *ftrpath, *lblpath, *lyopath;
    PangoLayoutLine *line;
    double x, y, z, pxx, pxy, wrap_width;
    int lyow, lyoh;
    paramd_path_t paramd_ftrpath, paramd_lblpath;
    param_t* params;
    double pathlen, lblwidth;

    cairo_set_source_rgba(cr,
            vfr_color_compextr(style->label_fill, 'r'),
            vfr_color_compextr(style->label_fill, 'g'),
            vfr_color_compextr(style->label_fill, 'b'),
            ((float)style->label_opacity)/100.0);

    // layout
    plyo = pango_cairo_create_layout(cr);
    if(style->label_text != NULL) {
        pango_layout_set_text(plyo, style->label_text, -1);
    } else if(style->label_field) {
        fieldidx = OGR_F_GetFieldIndex(ftr, style->label_field);
        if(fieldidx < 0) {
            fprintf(stderr, "invalid label field name '%s'\n", style->label_field);
            return -1;
        } else {
            pango_layout_set_text(plyo, OGR_F_GetFieldAsString(ftr, fieldidx), -1);
        }
    }
    if(style->label_fontdesc == NULL) {
        fdesc = pango_font_description_from_string(VFRDEFAULT_FONTDESC);
    } else {
        fdesc = pango_font_description_from_string(style->label_fontdesc);
    }
    pango_layout_set_font_description(plyo, fdesc);

    // position
    switch(wkbFlatten(OGR_G_GetGeometryType(geom))) {
        case wkbPoint:
            OGR_G_GetPoint(geom, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx += style->size/2.0;
            pxy = (ext->MaxY - y)/pxh;
            cairo_move_to(cr, pxx, pxy);
            pango_cairo_show_layout(cr, plyo);
            break;
        case wkbMultiLineString:
            fprintf(stderr, "multilinestring labelling not implemented\n");
            break;
        case wkbLineString:
            // trace line path
            cairo_new_path(cr);
            pcount = OGR_G_GetPointCount(geom);
            if(!pcount) return 0;
            for(i = 0; i < pcount; i++) {
                OGR_G_GetPoint(geom, i, &x, &y, &z);
                pxx = (x - ext->MinX)/pxw;
                pxy = (ext->MaxY - y)/pxh;
                if(!i) {
                    cairo_move_to(cr, pxx, pxy);
                    continue;
                } else {
                    cairo_line_to(cr, pxx, pxy);
                }
            }
            // save and copy line path
            cairo_save(cr);
            ftrpath = cairo_copy_path_flat(cr);
            // parametrize path
            params = parametrize_path(ftrpath, &pathlen);
            paramd_ftrpath.params = params;
            paramd_ftrpath.path = ftrpath;
            paramd_ftrpath.length = pathlen;
            // get middle segments as new path (.5 = midpoint)
            pango_layout_get_size(plyo, &lyow, &lyoh);
            lblwidth = (double)lyow/PANGO_SCALE;
            lblpath = get_linear_label_path(cr, &paramd_ftrpath, lblwidth, 0.5);
            cairo_path_destroy(ftrpath);
            ftrpath = NULL;
            paramd_ftrpath.path = NULL;
            free(params);
            paramd_ftrpath.params = NULL;
            if(lblpath) {
                params = parametrize_path(lblpath, &pathlen);
                paramd_lblpath.params = params;
                paramd_lblpath.path = lblpath;
                paramd_lblpath.length = pathlen;
                // get layout path
                line = pango_layout_get_line_readonly(plyo, 0);
                pxx = (&lblpath->data[0])[1].point.x;
                pxy = (&lblpath->data[0])[1].point.y;
                cairo_move_to(cr, pxx, pxy);
                cairo_new_path(cr);
                pango_cairo_layout_line_path(cr, line);
                lyopath = cairo_copy_path_flat(cr);
                cairo_new_path(cr);
                // put layout points on path
                transform_label_points(lyopath, &paramd_lblpath);
                cairo_append_path(cr, lyopath);
                cairo_path_destroy(lyopath);
                lyopath = NULL;
                cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
                cairo_fill(cr);
            } else {
                centroid = OGR_G_CreateGeometry(wkbPoint);
                if(OGR_G_Centroid(geom, centroid) == OGRERR_FAILURE) {
                    fprintf(stderr, "could not get centroid\n");
                }

                if(style->label_width > 0) {
                    wrap_width = style->label_width/pxw*PANGO_SCALE;
                } else {
                    OGR_G_GetEnvelope(geom, &envelope); 
                    wrap_width = (envelope.MaxX-envelope.MinX)/pxw*PANGO_SCALE;
                }

                OGR_G_GetPoint(centroid, 0, &x, &y, &z);
                pxx = (x - ext->MinX)/pxw;
                pxx -= (wrap_width/PANGO_SCALE)/2.0;
                pxy = (ext->MaxY - y)/pxh;

                pango_layout_set_alignment(plyo, PANGO_ALIGN_CENTER);
                pango_layout_set_width(plyo, -1);
                pango_layout_get_size(plyo, &lyow, &lyoh);
                pxy -= lyoh/PANGO_SCALE/2.0;
                cairo_move_to(cr, pxx, pxy);
                pango_cairo_show_layout(cr, plyo);
            }
            break;
        case wkbPolygon:
        default:
            centroid = OGR_G_CreateGeometry(wkbPoint);
            if(OGR_G_Centroid(geom, centroid) == OGRERR_FAILURE) {
                fprintf(stderr, "could not get centroid\n");
            }

            if(style->label_width > 0) {
                wrap_width = style->label_width/pxw*PANGO_SCALE;
            } else {
                OGR_G_GetEnvelope(geom, &envelope); 
                wrap_width = (envelope.MaxX-envelope.MinX)/pxw*PANGO_SCALE;
            }

            OGR_G_GetPoint(centroid, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx -= (wrap_width/PANGO_SCALE)/2.0;
            pxy = (ext->MaxY - y)/pxh;

            pango_layout_set_alignment(plyo, PANGO_ALIGN_CENTER);
            pango_layout_set_width(plyo, wrap_width);
            pango_layout_set_wrap(plyo, PANGO_WRAP_WORD_CHAR);
            pango_layout_get_size(plyo, &lyow, &lyoh);
            pxy -= lyoh/PANGO_SCALE/2.0;
            cairo_move_to(cr, pxx, pxy);
            pango_cairo_layout_path(cr, plyo);
            cairo_path_t *plyopath = cairo_copy_path_flat(cr);
            if(style->label_halo_fill <= 0xffffff) {
                make_label_halo(cr, plyopath, style);
            }
            cairo_set_source_rgba(cr,
                vfr_color_compextr(style->label_fill, 'r'),
                vfr_color_compextr(style->label_fill, 'g'),
                vfr_color_compextr(style->label_fill, 'b'),
                ((float)style->label_opacity)/100.0);
            cairo_move_to(cr, pxx, pxy);
            pango_cairo_show_layout(cr, plyo);
            break;
    }
   
    pango_font_description_free(fdesc);
    
    g_object_unref(plyo);
    
    return 0;
}

static double vfr_color_compextr(uint64_t color, char c) {

    double comp = -1.0;
    
    switch(c) {
        case 'r':
            comp = ((color & 0xff0000) >> 16)/255.0;
            break;
        case 'g':
            comp = ((color & 0x00ff00) >> 8)/255.0;
            break;
        case 'b':
            comp = (color & 0x0000ff)/255.0;
            break;
        default:
            break;
    }

    return comp;
}

static double euclid_dist(cairo_path_data_t *pt1, cairo_path_data_t *pt2) {
    double dx, dy;
    dx = pt2->point.x - pt1->point.x;
    dy = pt2->point.y - pt1->point.y;
    return sqrt(dx*dx + dy*dy);    
}

static param_t* parametrize_path(cairo_path_t *path, double *plen) {
    
    int i;
    cairo_path_data_t *pdat, lastmove, curpt;
    param_t *params;

    params = malloc(path->num_data*sizeof(param_t));
    if(!params) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    *plen = 0.0;
    
    for(i=0; i < path->num_data; i += path->data[i].header.length) {
        pdat = &path->data[i];
        params[i] = 0.0;
        switch(pdat->header.type) {
            case CAIRO_PATH_MOVE_TO:
                lastmove = pdat[1];
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CLOSE_PATH:
                // close path by simulating line_to to
                // most recent move_to
                pdat = (&lastmove-1);
            case CAIRO_PATH_LINE_TO:
                params[i] = euclid_dist(&curpt, &pdat[1]);
                *plen += params[i];
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CURVE_TO:
            default:
                fprintf(stderr, "labelling on curves not supported\n");
                exit(1);
                break;
        }
    }

    return params;
}

static cairo_path_t* get_linear_label_path(cairo_t *cr, paramd_path_t *parpath, double txtwidth, double placeat) {

    int i,j;
    cairo_path_t *src, *path;
    cairo_path_data_t curpt, nxtpt;
    param_t *params;
    double plen, dx, dy, rat, stx, sty, enx, eny;

    if(txtwidth > parpath->length) {
        return NULL;
    }

    src = parpath->path;
    params = parpath->params;
    plen = parpath->length*placeat;

    cairo_save(cr);
    cairo_new_path(cr);

    // walk to beginning of label region
    for(i=0; i<src->num_data && ((plen-params[i])>(txtwidth/2.0)); 
            i += src->data[i].header.length) {
        plen -= params[i];
        curpt = (&src->data[i])[1];
    }
    nxtpt = (&src->data[i])[1];
    dx = nxtpt.point.x - curpt.point.x;
    dy = nxtpt.point.y - curpt.point.y;
    rat = (plen-txtwidth/2.0)/params[i];
    stx = curpt.point.x+rat*dx;
    sty = curpt.point.y+rat*dy;
    cairo_move_to(cr, stx, sty);
    plen += txtwidth/2.0;

    // walk to end of label region
    for(j=i; j<src->num_data && ((plen-params[j])>0.0);
            j += src->data[j].header.length) {
        plen -= params[j];
        curpt = (&src->data[j])[1];
        cairo_line_to(cr, curpt.point.x, curpt.point.y);
    }
    nxtpt = (&src->data[j])[1];
    rat = plen/params[j];
    dx = nxtpt.point.x - curpt.point.x;
    dy = nxtpt.point.y - curpt.point.y;

    enx = curpt.point.x+rat*dx;
    eny = curpt.point.y+rat*dy;
    cairo_line_to(cr, enx, eny);

    path = cairo_copy_path_flat(cr);

    cairo_restore(cr);

    return path;
}

static void transform_label_points(cairo_path_t *lyopath, paramd_path_t *paramd_lblpath) {

    cairo_path_data_t *pdat;
    int i;
    double *xptr, *yptr;

    for(i=0; i<lyopath->num_data;
            i+=lyopath->data[i].header.length) {
        // walk segments...
        pdat = &lyopath->data[i];
        switch(pdat->header.type) {
            case CAIRO_PATH_CURVE_TO:
                // TODO: approximate w/ segments
                fprintf(stderr, "curves not supported\n");
                exit(1);
                break;
            case CAIRO_PATH_MOVE_TO:
            case CAIRO_PATH_LINE_TO:
                // transform point
                xptr = &pdat[1].point.x;
                yptr = &pdat[1].point.y;
                transform_label_point(paramd_lblpath, xptr, yptr);
                break;
            case CAIRO_PATH_CLOSE_PATH:
                break;
            default:
                fprintf(stderr, "invalid path data type\n");
                exit(1);
                break;
        }
    }

    return;
}

static void transform_label_point(paramd_path_t *paramd_lblpath, double *xptr, double *yptr) {

    int i;
    double rat, x=*xptr, y=*yptr, dx, dy;
    cairo_path_data_t *pdat, lastmoveto, curpt;
    cairo_path_t *lblpath;
    param_t *params;

    lblpath = paramd_lblpath->path;
    params = paramd_lblpath->params;

    for (i=0; i + lblpath->data[i].header.length < lblpath->num_data &&
            (x > params[i] ||
             lblpath->data[i].header.type == CAIRO_PATH_MOVE_TO);
            i += lblpath->data[i].header.length) {
        x -= params[i];
        pdat = &lblpath->data[i];
        switch (pdat->header.type) {
            case CAIRO_PATH_MOVE_TO:
                curpt = pdat[1];
                lastmoveto = pdat[1];
                break;
            case CAIRO_PATH_LINE_TO:
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CURVE_TO:
                curpt = pdat[3];
                break;
            case CAIRO_PATH_CLOSE_PATH:
                break;
            default:
                fprintf(stderr, "invalid path data type\n");
                exit(1);
                break;
        }
    }

    pdat = &lblpath->data[i];

    switch (pdat->header.type) {
        case CAIRO_PATH_MOVE_TO:
            break;
        case CAIRO_PATH_CLOSE_PATH:
            pdat = (&lastmoveto) - 1;
        case CAIRO_PATH_LINE_TO:

            // modelled on Behdad Esfahbod's technique in cairo examples:
            // https://github.com/phuang/pango/blob/master/examples/cairotwisted.c
            // http://mces.blogspot.com/2008/11/text-on-path-with-cairo.html)
            
            rat = x/params[i];

            /* Line polynomial */
            *xptr = curpt.point.x*(1-rat)+pdat[1].point.x*rat;
            *yptr = curpt.point.y*(1-rat)+pdat[1].point.y*rat;

            /* Line gradient */
            dx = -(curpt.point.x-pdat[1].point.x);
            dy = -(curpt.point.y-pdat[1].point.y);

            /*optimization for: ratio = the_y / sqrt (dx * dx + dy * dy);*/
            rat = y/params[i];
            *xptr += -dy*rat;
            *yptr +=  dx*rat;
            break;
        case CAIRO_PATH_CURVE_TO:
            fprintf(stderr, "labelling curves not supported\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "invalid path data type\n");
            exit(1);
            break;
    }

    return;
}

cairo_pattern_t* make_fill_pattern(vfr_style_t* style) {
    
    cairo_surface_t* psurf;
    cairo_pattern_t* hatchpat;
    cairo_t* cr2;
    cairo_matrix_t pmatrix;

    if(!style->hatch_pattern || !strcmp(style->hatch_pattern, VFRHATCH_NONE_S)) {
        return NULL;
    }

    psurf = cairo_svg_surface_create(NULL, 9, 9);
    cr2 = cairo_create(psurf);
    if(!strcmp(style->hatch_pattern, VFRHATCH_LINE_S)) {
        cairo_move_to(cr2, 4, -1);
        cairo_line_to(cr2, 4, 9);
    } else if(!strcmp(style->hatch_pattern, VFRHATCH_CROSS_S)) {
        cairo_move_to(cr2, 4, -1);
        cairo_line_to(cr2, 4, 9);
        cairo_move_to(cr2, -1, 4);
        cairo_line_to(cr2, 9, 4);
    } else if(!strcmp(style->hatch_pattern, VFRHATCH_DOT_S)) {
        cairo_arc(cr2, 4, 4, 1, 0, 2*M_PI);
    } else {
        cairo_destroy(cr2);
        cairo_surface_destroy(psurf);
        return NULL;
    }
    cairo_set_source_rgba(cr2,
            vfr_color_compextr(style->fill, 'r'), 
            vfr_color_compextr(style->fill, 'g'), 
            vfr_color_compextr(style->fill, 'b'),
            ((float)style->fill_opacity)/100.0);
    if(!strcmp(style->hatch_pattern, VFRHATCH_DOT_S)) {
        cairo_fill_preserve(cr2);
    }
    cairo_set_line_width(cr2, 1);
    cairo_set_line_cap(cr2, CAIRO_LINE_CAP_SQUARE);
    cairo_stroke(cr2);
    hatchpat = cairo_pattern_create_for_surface(psurf);
    cairo_destroy(cr2);
    cairo_surface_destroy(psurf);
    cairo_matrix_init_scale(&pmatrix, 1.0/style->hatch_scale, 1.0/style->hatch_scale);
    cairo_matrix_rotate(&pmatrix, style->hatch_rotate);
    cairo_pattern_set_matrix(hatchpat, &pmatrix);
    cairo_pattern_set_extend(hatchpat, CAIRO_EXTEND_REPEAT);
    return hatchpat;
}

/*int spline_knots(int knotsnum, double *tx, double *ty, double *cpx, double *cpy) {

    // modelled on https://www.particleincell.com/wp-content/uploads/2012/06/bezier-spline.js
    int n = knotsnum-1;

    double *K;
    double *Ks[2];
    Ks[0] = tx;
    Ks[1] = ty;

    double *CP;
    double *CPs[2];
    CPs[0] = cpx;
    CPs[1] = cpy;
    
    double *data = malloc(6*n*sizeof(double));
    double *p1 = data;
    double *p2 = &data[n];
    double *a = &data[2*n];
    double *b = &data[3*n];
    double *c = &data[4*n];
    double *r = &data[5*n];

    double m;

    int i, ki, cpnum;

    cpnum = 0;

    for(ki=0; ki<2; ki++) {
        K = Ks[ki];

        a[0] = 0.0;
        b[0] = 2.0;
        c[0] = 1.0;
        r[0] = K[0]+2.0*K[1];

        for(i=1; i<n-1; i++) {
            a[i] = 1.0;
            b[i] = 4.0;
            c[i] = 1.0;
            r[i] = 4 * K[i] + 2.0 * K[i+1];
        }

        a[n-1] = 2;
        b[n-1] = 7;
        c[n-1] = 0;
        r[n-1] = 8*K[n-1]+K[n];

        // solves AX=b w/ Thomas Algorithm
        for(i=1; i < n; i++) {
            m = a[i]/b[i-1];
            b[i] = b[i] - m * c[i-1];
            r[i] = r[i] - m * r[i-1];
        }

        p1[n-1] = r[n-1]/b[i-1];
        for(i=n-2; i >= 0; --i) {
            p1[i] = (r[i] - c[i] * p1[i+1]) / b[i];
        }
        // compute p2 uing p1
        for(i=0; i<n-1; i++) {
            p2[i] = 2 * K[n] - p1[i+1];
        }

        p2[n-1] = 0.5*(K[n] + p1[n-1]);
        CP = CPs[ki];
        for(i=0; i<n; i++) {
            CP[i*2] = p1[i];
            CP[i*2+1] = p2[i];
            cpnum += 2;
        }
    }
    free(data);

    return cpnum/2;
}*/


void point_on_bezier(
                    double x0, double y0,
                    double x1, double y1, 
                    double x2, double y2,
                    double x3, double y3,
                    double t, 
                    double *Bx, double *By)
{
    *Bx = (pow(1.0-t, 3.0)*x0) + (3*pow(1.0-t, 2.0)*t*x1) + 
        (3*(1-t)*pow(t, 2.0)*x2) + (pow(t, 3.0)*x3);
    *By = (pow(1.0-t, 3.0)*y0) + (3*pow(1.0-t, 2.0)*t*y1) + 
        (3*(1-t)*pow(t, 2.0)*y2) + (pow(t, 3.0)*y3);
    return;
}

OGRGeometryH path_convex_hull(cairo_path_t *path) {
    int i;
    double x, y, p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y;
    double bx, by;
    cairo_path_data_t *pdat;

    OGRGeometryH pgeom = OGR_G_CreateGeometry(wkbLineString);
    OGRGeometryH hgeom;

    for(i=0; i<path->num_data; i+=path->data[i].header.length) {
        pdat = &path->data[i];
        switch(pdat->header.type) {
            case CAIRO_PATH_MOVE_TO:
                break;
            case CAIRO_PATH_LINE_TO:
                x = pdat[1].point.x;
                y = pdat[1].point.y;
                OGR_G_AddPoint_2D(pgeom, x, y);
                break;
            case CAIRO_PATH_CURVE_TO:
                p0x = x;
                p0y = y;
                p1x = pdat[1].point.x;
                p1y = pdat[1].point.y;
                p2x = pdat[2].point.x;
                p2y = pdat[2].point.y;
                p3x = pdat[3].point.x;
                p3y = pdat[3].point.y;
                point_on_bezier(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y, 0.5, &bx, &by);
                OGR_G_AddPoint_2D(pgeom, bx, by);
                x = p3x;
                y = p3y;
                OGR_G_AddPoint_2D(pgeom, x, y);
                break;
            case CAIRO_PATH_CLOSE_PATH:
            default:
                break;
        }
    }
    hgeom = OGR_G_ConvexHull(pgeom);
    OGR_G_DestroyGeometry(pgeom);
    return hgeom;
}

void make_label_halo(cairo_t *cr, cairo_path_t *plyopath, vfr_style_t *style) {
        int i, hullptnum;
        double x, y;
        OGRGeometryH hullgeom = path_convex_hull(plyopath);
        OGRGeometryH hullext = OGR_G_GetGeometryRef(hullgeom, 0);
        hullptnum = OGR_G_GetPointCount(hullext);
        cairo_new_path(cr);
        x = OGR_G_GetX(hullext, 0);
        y = OGR_G_GetY(hullext, 0);
        cairo_move_to(cr, x, y);
        for(i=1; i<hullptnum; i++) {
            x = OGR_G_GetX(hullext, i);
            y = OGR_G_GetY(hullext, i);
            cairo_line_to(cr, x, y);
        }
        cairo_close_path(cr);
        cairo_set_source_rgba(cr,
            vfr_color_compextr(style->label_halo_fill, 'r'),
            vfr_color_compextr(style->label_halo_fill, 'g'),
            vfr_color_compextr(style->label_halo_fill, 'b'),
            ((float)style->label_opacity)/100.0);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, style->label_halo_size);
        cairo_stroke(cr);
        OGR_G_DestroyGeometry(hullgeom);
        return;
}
