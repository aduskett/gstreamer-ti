/*
 * gsttic6xcolorspace.c
 *
 * This file defines the "TIC6xColorspace" element, which does the 
 * DSP accelerated colorspace coversion using TIC6Accel library.
 *
 * Example usage:
 *     gst-launch videotestsrc ! TIC6xColorspace engineName=codecServer ! 
 *       fbdevsink
 *
 * Original Author:
 *     Brijesh Singh, Texas Instruments, Inc.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation version 2.1 of the License.
 *
 * This program is distributed #as is# WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/BufTab.h>

#include <c6accelw.h>

#include "gsttic6xcolorspace.h"
#include "gsttidmaibuffertransport.h"
#include "gstticommonutils.h"

/* Declare variable used to categorize GST_LOG output */
GST_DEBUG_CATEGORY_STATIC (gst_tic6xcolorspace_debug);
#define GST_CAT_DEFAULT gst_tic6xcolorspace_debug

#define DEFAULT_NUM_OUTPUT_BUFS         2

/* Element property identifier */
enum {
  PROP_0,
  PROP_NUM_OUTPUT_BUFS,          /*  numOutputBufs    (gint)      */
  PROP_ENGINE_NAME,              /*  engineName  (gchar*) */ 
};

/* Define sink and src pad capabilities. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ( GST_VIDEO_CAPS_YUV("I420"))
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ( GST_VIDEO_CAPS_RGB_16 )
);

/* Declare a global pointer to our element base class */
static GstElementClass *parent_class = NULL;

/* Static Function Declarations */
static void
 gst_tic6xcolorspace_base_init(gpointer g_class);
static void
 gst_tic6xcolorspace_class_init(GstTIC6xColorspaceClass *g_class);
static void
 gst_tic6xcolorspace_init(GstTIC6xColorspace *object);
static gboolean gst_tic6xcolorspace_exit_colorspace(GstTIC6xColorspace *c6xcolorspace);
static void gst_tic6xcolorspace_fixate_caps (GstBaseTransform *trans,
 GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static gboolean gst_tic6xcolorspace_set_caps (GstBaseTransform *trans, 
 GstCaps *in, GstCaps *out);
static gboolean gst_tic6xcolorspace_parse_caps (GstCaps *cap, gint *width,
 gint *height, guint32 *fourcc);
static GstCaps * gst_tic6xcolorspace_transform_caps (GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps);
static GstFlowReturn gst_tic6xcolorspace_transform (GstBaseTransform *trans,
 GstBuffer *inBuf, GstBuffer *outBuf);
static ColorSpace_Type gst_tic6xcolorspace_get_colorSpace (guint32 fourcc);
static void gst_tic6xcolorspace_set_property(GObject *object, guint prop_id,
 const GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_tic6xcolorspace_prepare_output_buffer (GstBaseTransform *trans, GstBuffer *inBuf, gint size, GstCaps *caps, GstBuffer **outBuf);
static Buffer_Handle gst_tic6xcolorspace_gfx_buffer_create (gint width, 
 gint height, ColorSpace_Type colorSpace, gint size, gboolean is_reference);
static gboolean gst_tic6xcolorspace_transform_size(GstBaseTransform *trans,
 GstPadDirection direction, GstCaps *caps, guint size, GstCaps *othercaps, 
 guint *othersize);
static void gst_tic6xcolorspace_get_property(GObject *object, guint prop_id,
 GValue *value, GParamSpec *pspec);

/******************************************************************************
 * gst_tic6xcolorspace_init
 *****************************************************************************/
static void gst_tic6xcolorspace_init (GstTIC6xColorspace 
    *c6xcolorspace)
{
    gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM(c6xcolorspace),
     TRUE);

    c6xcolorspace->numOutputBufs =  DEFAULT_NUM_OUTPUT_BUFS;
    c6xcolorspace->hC6           =  NULL;
    c6xcolorspace->hEngine       =  NULL;
    c6xcolorspace->hCoeff        =  NULL;
    c6xcolorspace->engineName    =  NULL;
}

/******************************************************************************
 * gst_tic6xcolorspace_get_type
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Defines function pointers for initialization routines for this element.
 ******************************************************************************/
GType gst_tic6xcolorspace_get_type(void)
{
    static GType object_type = 0;

    if (G_UNLIKELY(object_type == 0)) {
        static const GTypeInfo object_info = {
            sizeof(GstTIC6xColorspaceClass),
            gst_tic6xcolorspace_base_init,
            NULL,
            (GClassInitFunc) gst_tic6xcolorspace_class_init,
            NULL,
            NULL,
            sizeof(GstTIC6xColorspace),
            0,
            (GInstanceInitFunc) gst_tic6xcolorspace_init
        };

        object_type = g_type_register_static(GST_TYPE_BASE_TRANSFORM,
                          "GstTIC6xColorspace", &object_info, 
                            (GTypeFlags)0);

        /* Initialize GST_LOG for this object */
        GST_DEBUG_CATEGORY_INIT(gst_tic6xcolorspace_debug, 
            "TIC6xColorspace", 0, "TI Image colorspace");

        GST_LOG("initialized get_type\n");
    }

    return object_type;
};

/******************************************************************************
 * gst_tic6xcolorspace_base_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes element base class.
 ******************************************************************************/
static void gst_tic6xcolorspace_base_init(gpointer gclass)
{
    static GstElementDetails element_details = {
        "TI Image colorconversion",
        "Filter/Conversion",
        "Colorcoversion using TIC6Accel ",
        "Brijesh Singh; Texas Instruments, Inc."
    };

    GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get (&src_factory));
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get (&sink_factory));
    gst_element_class_set_details(element_class, &element_details);
}

/******************************************************************************
 * gst_tic6xcolorspace_class_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes the TIC6xColorspace class.
 ******************************************************************************/
static void gst_tic6xcolorspace_class_init(GstTIC6xColorspaceClass 
    *klass)
{
    GObjectClass    *gobject_class;
    GstBaseTransformClass   *trans_class;

    gobject_class    = (GObjectClass*)    klass;
    trans_class      = (GstBaseTransformClass *) klass;

    gobject_class->set_property = gst_tic6xcolorspace_set_property;
    gobject_class->get_property = gst_tic6xcolorspace_get_property;

    gobject_class->finalize = 
        (GObjectFinalizeFunc)gst_tic6xcolorspace_exit_colorspace;

    trans_class->transform_caps = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_transform_caps);
    trans_class->set_caps  = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_set_caps);
    trans_class->transform = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_transform);
    trans_class->fixate_caps = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_fixate_caps);
    trans_class->passthrough_on_same_caps = TRUE;
    trans_class->prepare_output_buffer = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_prepare_output_buffer);
    trans_class->transform_size = 
        GST_DEBUG_FUNCPTR(gst_tic6xcolorspace_transform_size);

    parent_class = g_type_class_peek_parent (klass);

    g_object_class_install_property(gobject_class, PROP_NUM_OUTPUT_BUFS,
        g_param_spec_int("numOutputBufs",
            "Number of Output buffers",
            "Number of output buffers to allocate",
            1, G_MAXINT32, DEFAULT_NUM_OUTPUT_BUFS, G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_ENGINE_NAME,
        g_param_spec_string("engineName", "Engine Name",
            "Engine name used by Codec Engine", "codecServer",
            G_PARAM_READWRITE));

    GST_LOG("initialized class init\n");
}

/******************************************************************************
 * gst_tic6xcolorspace_gfx_buffer_create
 *  Helper function to create dmai graphics buffer
 *****************************************************************************/
static Buffer_Handle gst_tic6xcolorspace_gfx_buffer_create (gint width, 
    gint height, ColorSpace_Type colorSpace, gint size, gboolean is_reference)
{
    BufferGfx_Attrs gfxAttrs   = BufferGfx_Attrs_DEFAULT;
    Buffer_Handle   buf        = NULL;
                
    gfxAttrs.bAttrs.reference  = is_reference;
    gfxAttrs.colorSpace     = colorSpace;
    gfxAttrs.dim.width      = width;
    gfxAttrs.dim.height     = height;
    gfxAttrs.bAttrs.memParams.align = 128;
    gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(gfxAttrs.dim.width, 
                                gfxAttrs.colorSpace);
    buf = Buffer_create(Dmai_roundUp(size, 128), 
            BufferGfx_getBufferAttrs(&gfxAttrs));

    if (buf == NULL) {
        return NULL;
    }

    return buf;
}

/*****************************************************************************
 * gst_tic6xcolorspace_prepare_output_buffer
 *    Function is used to allocate output buffer
 *****************************************************************************/
static GstFlowReturn gst_tic6xcolorspace_prepare_output_buffer (
    GstBaseTransform *trans, GstBuffer *inBuf, gint size, GstCaps *caps, 
    GstBuffer **outBuf)
{
    GstTIC6xColorspace *c6xcolorspace = GST_TIC6XCOLORSPACE(trans);
    Buffer_Handle   hOutBuf;

    GST_LOG("begin prepare output buffer\n");

    if (c6xcolorspace->hOutBufTab == NULL) {
        goto exit;
    }

    /* Get free buffer from buftab */
    if (!(hOutBuf = gst_tidmaibuftab_get_buf(c6xcolorspace->hOutBufTab))) {
        GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, READ,
            ("failed to get free buffer\n"), (NULL));
        return GST_FLOW_ERROR;
    }

    /* Reset waitOnBufTab rendezvous handle to its orignal state */
    Rendezvous_reset(c6xcolorspace->waitOnBufTab);

    /* Create a DMAI transport buffer object to carry a DMAI buffer to
     * the source pad.  The transport buffer knows how to release the
     * buffer for re-use in this element when the source pad calls
     * gst_buffer_unref().
     */
    GST_LOG("creating dmai transport buffer\n");
    *outBuf = gst_tidmaibuffertransport_new(hOutBuf, c6xcolorspace->hOutBufTab);
    gst_buffer_set_data(*outBuf, (guint8*) Buffer_getUserPtr(hOutBuf), 
                        Buffer_getSize(hOutBuf));
    gst_buffer_set_caps(*outBuf, GST_PAD_CAPS(trans->srcpad));

exit:
    GST_LOG("end prepare output buffer\n");   

    return GST_FLOW_OK;
}

/******************************************************************************
 * gst_tividenc_get_property
 *     Get element properties when requested.
 ******************************************************************************/
static void gst_tic6xcolorspace_get_property(GObject *object, 
    guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstTIC6xColorspace *c6xcolorspace = GST_TIC6XCOLORSPACE(object);

    GST_LOG("begin get_property\n");

    switch (prop_id) {
        case PROP_ENGINE_NAME:
            g_value_set_string(value, c6xcolorspace->engineName);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    GST_LOG("end get_property\n");
}

/******************************************************************************
 * gst_tividenc_set_property
 *     Set element properties when requested.
 ******************************************************************************/
static void gst_tic6xcolorspace_set_property(GObject *object, 
    guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstTIC6xColorspace *c6xcolorspace = GST_TIC6XCOLORSPACE(object);

    GST_LOG("begin set_property\n");

    switch (prop_id) {
        case PROP_NUM_OUTPUT_BUFS:
            c6xcolorspace->numOutputBufs = g_value_get_int(value);
            GST_LOG("setting \"numOutputBufs\" to \"%d\"\n",
                c6xcolorspace->numOutputBufs);
            break;
        case PROP_ENGINE_NAME:
            if (c6xcolorspace->engineName) {
                g_free((gpointer)c6xcolorspace->engineName);
            }
            c6xcolorspace->engineName =
                (gchar*)g_malloc(strlen(g_value_get_string(value)) + 1);
            strcpy((gchar*)c6xcolorspace->engineName, 
                g_value_get_string(value));
            GST_LOG("setting \"engineName\" to \"%s\"\n", 
                c6xcolorspace->engineName);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    GST_LOG("end set_property\n");
}
       

/******************************************************************************
 * gst_tic6xcolorspace_transform 
 *    Transforms one incoming buffer to one outgoing buffer.
 *****************************************************************************/
static GstFlowReturn gst_tic6xcolorspace_transform (GstBaseTransform *trans,
    GstBuffer *src, GstBuffer *dst)
{
    GstTIC6xColorspace *c6xcolorspace  = GST_TIC6XCOLORSPACE(trans);
    Buffer_Handle  hInBuf = NULL, hOutBuf = NULL;
    GstFlowReturn  ret  = GST_FLOW_ERROR;
    unsigned char   *y, *cb, *cr;
    short  *coeff = NULL;
    unsigned short  *rgb;
    BufferGfx_Dimensions  dim;
    const short yuv2rgb_coeff[] = {0x2000, 0x2BDD, -0x0AC5, -0x1658, 0x3770};
    Buffer_Attrs bAttrs   = Buffer_Attrs_DEFAULT;

    GST_LOG("begin transform\n");

    /* create c6accel handle */
    if (c6xcolorspace->hC6 == NULL) {
        GST_LOG("creating C6Accel handle engineName=%s \n", 
             c6xcolorspace->engineName);

        c6xcolorspace->hEngine = Engine_open((Char*) 
      c6xcolorspace->engineName,  NULL, NULL);

        if (c6xcolorspace->hEngine == NULL) {
            GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, FAILED,
            ("failed to create engine handle \n"), (NULL));
            goto exit;  
        }

        c6xcolorspace->hC6 = C6accel_create((Char*) 
             c6xcolorspace->engineName, c6xcolorspace->hEngine,
             "c6accel", NULL);

        if (c6xcolorspace->hC6 == NULL) {
            GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, FAILED,
            ("failed to create c6accel handle \n"), (NULL));
            goto exit;  
        }

        bAttrs.memParams.align = 128;
        c6xcolorspace->hCoeff = Buffer_create(5*sizeof(short), &bAttrs);
        if (c6xcolorspace->hCoeff == NULL) {
            GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, NO_SPACE_LEFT,
            ("failed to create memory for coeff table \n"), (NULL));
            goto exit;  
        }

        memcpy(Buffer_getUserPtr(c6xcolorspace->hCoeff), yuv2rgb_coeff, 
            5*sizeof(short));
    }

    /* Get the output buffer handle */ 
    hOutBuf = GST_TIDMAIBUFFERTRANSPORT_DMAIBUF(dst);

    /* Get the input buffer handle */
    if (GST_IS_TIDMAIBUFFERTRANSPORT(src)) {
        GST_LOG("found dmai transport buffer\n");
        /* if we are recieving dmai transport buffer then get the handle */
        hInBuf = GST_TIDMAIBUFFERTRANSPORT_DMAIBUF(src);
    }
    else {
        GST_LOG("found non-dmai buffer, allocating dmai buffer\n");
        /* If we are recieving non dmai transport buffer then copy the 
         * input buffer in dmai buffer.
         */
        hInBuf = gst_tic6xcolorspace_gfx_buffer_create(
                c6xcolorspace->width, c6xcolorspace->height, 
                c6xcolorspace->srcColorSpace, GST_BUFFER_SIZE(src), 
                FALSE);
        if (hInBuf == NULL) {
            GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, NO_SPACE_LEFT,
            ("failed to create input dmai buffer \n"), (NULL));
            goto exit;
        }
        memcpy(Buffer_getUserPtr(hInBuf), GST_BUFFER_DATA(src), 
                GST_BUFFER_SIZE(src));
    }

    /* Execute C6Accel color coversion */
    BufferGfx_getDimensions(hInBuf, &dim);
    y = (unsigned char*) Buffer_getUserPtr(hInBuf);
    cb = y + Buffer_getSize(hInBuf) * 2 / 3;
    cr = y + Buffer_getSize(hInBuf) * 5 / 6;
    rgb = (unsigned short*) Buffer_getUserPtr(hOutBuf);
    coeff = (short*) Buffer_getUserPtr(c6xcolorspace->hCoeff);

    if (C6accel_IMG_yuv420pl_to_rgb565(c6xcolorspace->hC6, coeff, 
         dim.height, dim.width, y, cb, cr, rgb) < 0) {
        GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, FAILED,
        ("failed to execute colorspace coversion \n"), (NULL));
        goto exit;
    }

    ret = GST_FLOW_OK;

exit:

    if (hInBuf && !GST_IS_TIDMAIBUFFERTRANSPORT(src)) {
        Buffer_delete(hInBuf);
    }

    GST_LOG("end transform\n");
    return ret;
}

/******************************************************************************
 * gst_tic6xcolorspace_transform_caps
 *   Given the pad in this direction and the given caps, what caps are allowed 
 *   on the other pad in this element
 *****************************************************************************/
static GstCaps * gst_tic6xcolorspace_transform_caps (GstBaseTransform 
 *trans, GstPadDirection direction, GstCaps *from)
{
    GstTIC6xColorspace  *c6xcolorspace;
    GstCaps *result;
    GstPad *other;
    const GstCaps *templ;

    GST_LOG("begin transform caps (%s)\n",
        direction==GST_PAD_SRC ? "src" : "sink");

    c6xcolorspace   = GST_TIC6XCOLORSPACE(trans);
    g_return_val_if_fail(from != NULL, NULL);

    other = (direction == GST_PAD_SINK) ? trans->srcpad : trans->sinkpad;
    templ = gst_pad_get_pad_template_caps(other);

    result = gst_caps_copy(templ);


    GST_LOG("returing cap %" GST_PTR_FORMAT, result);
    GST_LOG("end transform caps\n");

    return result;
}

/******************************************************************************
 * gst_tic6xcolorspace_parse_caps
 *****************************************************************************/
static gboolean gst_tic6xcolorspace_parse_caps (GstCaps *cap, gint *width,
    gint *height, guint32 *format)
{
    GstStructure    *structure;
    structure = gst_caps_get_structure(cap, 0);
    
    GST_LOG("begin parse caps\n");

    if (!gst_structure_get_int(structure, "width", width)) {
        GST_ERROR("Failed to get width \n");
        return FALSE;
    }

    if (!gst_structure_get_int(structure, "height", height)) {
        GST_ERROR("Failed to get height \n");
        return FALSE;
    }
    
    if (!gst_structure_get_fourcc(structure, "format", format)) {
        GST_ERROR("failed to get fourcc from cap\n");
        return FALSE;
    }

    GST_LOG("end parse caps\n");
    return TRUE; 
}

/*****************************************************************************
 * gst_tic6xcolorspace_get_colorSpace
 ****************************************************************************/
ColorSpace_Type gst_tic6xcolorspace_get_colorSpace (guint32 fourcc)
{
    switch (fourcc) {
        case GST_MAKE_FOURCC('I', '4', '2', '0'):
            return ColorSpace_YUV420P;
        default:
            GST_ERROR("failed to get colorspace\n");
            return ColorSpace_NOTSET;
    }
}

/******************************************************************************
 * gst_tic6xcolorspace_transform_size
 * Given the size of a buffer in the given direction with the given caps, 
 * calculate the size in bytes of a buffer on the other pad with the given 
 * other caps. The default implementation uses get_unit_size and keeps the 
 * number of units the same
 *****************************************************************************/
static gboolean gst_tic6xcolorspace_transform_size(GstBaseTransform *trans,
 GstPadDirection direction, GstCaps *caps, guint size, GstCaps *othercaps, 
 guint *othersize)
{
    gboolean ret;
    gint width, height;

    GST_LOG("begin gst_tic6xcolorspace_transform_size\n");

    if (direction == GST_PAD_SINK) {
        ret = gst_video_format_parse_caps(caps, NULL, &width, &height);
        if (!ret) {
            GST_ERROR("failed to get input width/height\n");
            return FALSE;
        }
        *othersize = gst_ti_calc_buffer_size(width, height, 0, 
             ColorSpace_RGB565); 
        GST_LOG("size = %d\n", *othersize);
    }

    GST_LOG("end gst_tic6xcolorspace_transform_size\n");
    return TRUE;
}

/******************************************************************************
 * gst_tic6xcolorspace_set_caps
 *****************************************************************************/
static gboolean gst_tic6xcolorspace_set_caps (GstBaseTransform *trans, 
    GstCaps *in, GstCaps *out)
{
    GstTIC6xColorspace *c6xcolorspace  = GST_TIC6XCOLORSPACE(trans);
    BufferGfx_Attrs     gfxAttrs    = BufferGfx_Attrs_DEFAULT;
    Rendezvous_Attrs    rzvAttrs    = Rendezvous_Attrs_DEFAULT;
    gboolean            ret         = FALSE;
    guint32             fourcc;
    guint               outBufSize;

    GST_LOG("begin set caps\n");

    /* parse input cap */
    if (!gst_tic6xcolorspace_parse_caps(in, &c6xcolorspace->width,
             &c6xcolorspace->height, &fourcc)) {
        GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, FAILED,
        ("failed to get input resolution\n"), (NULL));
        goto exit;
    }

    GST_LOG("input fourcc %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));

    /* map fourcc with its corresponding dmai colorspace type */ 
    c6xcolorspace->srcColorSpace = 
        gst_tic6xcolorspace_get_colorSpace(fourcc);

    c6xcolorspace->width = c6xcolorspace->width;
    c6xcolorspace->height = c6xcolorspace->height;
    c6xcolorspace->dstColorSpace = ColorSpace_RGB565;

    /* calculate output buffer size */
    outBufSize = gst_ti_calc_buffer_size(c6xcolorspace->width,
        c6xcolorspace->height, 0, c6xcolorspace->dstColorSpace);

    /* create rendezvous handle for buffer release */
    c6xcolorspace->waitOnBufTab = Rendezvous_create (100, &rzvAttrs);
    if (c6xcolorspace->waitOnBufTab == NULL) {
        GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, FAILED,
        ("failed t create waitonbuftab handle\n"), (NULL));
        goto exit;
    }

    /* allocate output buffer */
    gfxAttrs.bAttrs.useMask = gst_tidmaibuffer_GST_FREE;
    gfxAttrs.colorSpace = c6xcolorspace->dstColorSpace;
    gfxAttrs.dim.width = c6xcolorspace->width;
    gfxAttrs.dim.height = c6xcolorspace->height;
    gfxAttrs.dim.lineLength =
        BufferGfx_calcLineLength (gfxAttrs.dim.width, gfxAttrs.colorSpace);

    if (c6xcolorspace->numOutputBufs == 0) {
        c6xcolorspace->numOutputBufs = 2;
    }

    gfxAttrs.bAttrs.memParams.align = 128;
    c6xcolorspace->hOutBufTab = 
    gst_tidmaibuftab_new(c6xcolorspace->numOutputBufs,
        outBufSize, BufferGfx_getBufferAttrs (&gfxAttrs));
    if (c6xcolorspace->hOutBufTab == NULL) {
        GST_ELEMENT_ERROR(c6xcolorspace, RESOURCE, NO_SPACE_LEFT,
        ("failed to create output bufTab\n"), (NULL));
        goto exit;
    }

    ret = TRUE;

exit:
    GST_LOG("end set caps\n");
    return ret;
}

/******************************************************************************
 * gst_video_scale_fixate_caps 
 *****************************************************************************/
static void gst_tic6xcolorspace_fixate_caps (GstBaseTransform *trans,
     GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
    GstStructure    *outs;
    gint            width, height, framerateNum, framerateDen;
    gboolean        ret;

    g_return_if_fail(gst_caps_is_fixed(caps));

    GST_LOG("begin fixating cap\n");

    /* get the sink dimension/framerate */
    ret = gst_video_format_parse_caps(caps, NULL, &width, &height);
    if (!ret) 
        return;

    ret = gst_video_parse_caps_framerate(caps, &framerateNum, &framerateDen);
    if (!ret) 
        return;

    /* set the dimension/framerate for output caps */
    outs = gst_caps_get_structure(othercaps, 0);
    gst_structure_fixate_field_nearest_int (outs, "width", width);
    gst_structure_fixate_field_nearest_int (outs, "height", height);
    gst_structure_fixate_field_nearest_fraction (outs, "framerate", 
        framerateNum, framerateDen);

    GST_LOG("end fixating cap\n");
}

/******************************************************************************
 * gst_tic6xcolorspace_exit_colorspace
 *    Shut down any running video colorspace, and reset the element state.
 ******************************************************************************/
static gboolean gst_tic6xcolorspace_exit_colorspace(GstTIC6xColorspace *c6xcolorspace)
{
    GST_LOG("begin exit_video\n");

    /* Shut down remaining items */
    if (c6xcolorspace->hEngine) {
        GST_LOG("freeing engine handle\n");
        Engine_close(c6xcolorspace->hEngine);
        c6xcolorspace->hEngine = NULL;
    }

    if (c6xcolorspace->hC6) {
        GST_LOG("freeing C6accel handle\n");
        C6accel_delete(c6xcolorspace->hC6);
        c6xcolorspace->hC6 = NULL;
    }

    if (c6xcolorspace->hOutBufTab) {
        GST_LOG("freeing output buffers\n");
        gst_tidmaibuftab_unref(c6xcolorspace->hOutBufTab);
        c6xcolorspace->hOutBufTab = NULL;
    }

    if (c6xcolorspace->hCoeff) {
        GST_LOG("freeing output buffers\n");
        Buffer_delete(c6xcolorspace->hCoeff);
        c6xcolorspace->hCoeff = NULL;
    }

    if (c6xcolorspace->waitOnBufTab) {
        GST_LOG("deleting  Rendezvous handle\n");
        Rendezvous_delete(c6xcolorspace->waitOnBufTab);
        c6xcolorspace->waitOnBufTab = NULL;
    }

    GST_LOG("end exit_video\n");
    return TRUE;
}

/******************************************************************************
 * Custom ViM Settings for editing this file
 ******************************************************************************/
#if 0
 Tabs (use 4 spaces for indentation)
 vim:set tabstop=4:      /* Use 4 spaces for tabs          */
 vim:set shiftwidth=4:   /* Use 4 spaces for >> operations */
 vim:set expandtab:      /* Expand tabs into white spaces  */
#endif
