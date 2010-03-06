/* Pango
 * pangocairo-coretextfont.c
 *
 * Copyright (C) 2000-2005 Red Hat Software
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2010  Kristian Rietveld  <kris@gtk.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <Carbon/Carbon.h>

#include "pango-impl-utils.h"
#include "pangocoretext-private.h"
#include "pangocairo.h"
#include "pangocairo-private.h"
#include "pangocairo-coretext.h"
#include "pangocairo-coretextfont.h"

struct _PangoCairoCoreTextFont
{
  PangoCoreTextFont font;
  PangoCairoFontPrivate cf_priv;

  double size;
  int absolute_size;
};

struct _PangoCairoCoreTextFontClass
{
  PangoCoreTextFontClass parent_class;
};



static cairo_font_face_t *pango_cairo_core_text_font_create_font_face           (PangoCairoFont *font);
static PangoFontMetrics  *pango_cairo_core_text_font_create_metrics_for_context (PangoCairoFont *font,
                                                                                 PangoContext    *context);

static void
cairo_font_iface_init (PangoCairoFontIface *iface)
{
  iface->create_font_face = pango_cairo_core_text_font_create_font_face;
  iface->create_metrics_for_context = pango_cairo_core_text_font_create_metrics_for_context;
  iface->cf_priv_offset = G_STRUCT_OFFSET (PangoCairoCoreTextFont, cf_priv);
}

G_DEFINE_TYPE_WITH_CODE (PangoCairoCoreTextFont, pango_cairo_core_text_font, PANGO_TYPE_CORE_TEXT_FONT,
    { G_IMPLEMENT_INTERFACE (PANGO_TYPE_CAIRO_FONT, cairo_font_iface_init) });

/* we want get_glyph_extents extremely fast, so we use a small wrapper here
 * to avoid having to lookup the interface data like we do for get_metrics
 * in _pango_cairo_font_get_metrics(). */
static void
pango_cairo_core_text_font_get_glyph_extents (PangoFont      *font,
                                              PangoGlyph      glyph,
                                              PangoRectangle *ink_rect,
                                              PangoRectangle *logical_rect)
{
  PangoCairoCoreTextFont *cafont = (PangoCairoCoreTextFont *) (font);

  _pango_cairo_font_private_get_glyph_extents (&cafont->cf_priv,
					       glyph,
					       ink_rect,
					       logical_rect);
}

static cairo_font_face_t *
pango_cairo_core_text_font_create_font_face (PangoCairoFont *font)
{
  PangoCoreTextFont *ctfont = (PangoCoreTextFont *) (font);
  CTFontRef font_id;
  CGFontRef cgfont;
  cairo_font_face_t *cairo_face;

  font_id = pango_core_text_font_get_ctfont (ctfont);
  cgfont = CTFontCopyGraphicsFont (font_id, NULL);

  cairo_face = cairo_quartz_font_face_create_for_cgfont (cgfont);

  CFRelease (cgfont);

  return cairo_face;
}

static int
max_glyph_width (PangoLayout *layout)
{
  int max_width = 0;
  GSList *l, *r;

  for (l = pango_layout_get_lines_readonly (layout); l; l = l->next)
    {
      PangoLayoutLine *line = l->data;

      for (r = line->runs; r; r = r->next)
	{
	  PangoGlyphString *glyphs = ((PangoGlyphItem *)r->data)->glyphs;
	  int i;

	  for (i = 0; i < glyphs->num_glyphs; i++)
	    if (glyphs->glyphs[i].geometry.width > max_width)
	      max_width = glyphs->glyphs[i].geometry.width;
	}
    }

  return max_width;
}

static PangoFontMetrics *
pango_cairo_core_text_font_create_metrics_for_context (PangoCairoFont *font,
                                                       PangoContext   *context)
{
  PangoCoreTextFont *cfont = (PangoCoreTextFont *) font;
  PangoFontMetrics *metrics;
  PangoFontDescription *font_desc;
  PangoLayout *layout;
  PangoRectangle extents;
  PangoLanguage *language = pango_context_get_language (context);
  const char *sample_str = pango_language_get_sample_string (language);
  CTFontRef ctfont;

  metrics = pango_font_metrics_new ();

  ctfont = pango_core_text_font_get_ctfont (cfont);

  metrics->ascent = CTFontGetAscent (ctfont) * PANGO_SCALE;
  metrics->descent = CTFontGetDescent (ctfont) * PANGO_SCALE;

  metrics->underline_position = CTFontGetUnderlinePosition (ctfont) * PANGO_SCALE;
  metrics->underline_thickness = CTFontGetUnderlineThickness (ctfont) * PANGO_SCALE;

  metrics->strikethrough_position = metrics->ascent / 3;
  metrics->strikethrough_thickness = CTFontGetUnderlineThickness (ctfont) * PANGO_SCALE;

  layout = pango_layout_new (context);
  font_desc = pango_font_describe_with_absolute_size ((PangoFont *) font);
  pango_layout_set_font_description (layout, font_desc);
  pango_layout_set_text (layout, sample_str, -1);
  pango_layout_get_extents (layout, NULL, &extents);

  metrics->approximate_char_width = extents.width / pango_utf8_strwidth (sample_str);

  pango_layout_set_text (layout, "0123456789", -1);
  metrics->approximate_digit_width = max_glyph_width (layout);

  pango_font_description_free (font_desc);
  g_object_unref (layout);

  return metrics;
}

static PangoFontDescription *
pango_cairo_core_text_font_describe_absolute (PangoFont *font)
{
  PangoFontDescription *desc;
  PangoCairoCoreTextFont *cafont = (PangoCairoCoreTextFont *) font;

  desc = pango_font_describe (font);
  pango_font_description_set_absolute_size (desc, cafont->absolute_size);

  return desc;
}

static void
pango_cairo_core_text_font_finalize (GObject *object)
{
  PangoCairoCoreTextFont *cafont = (PangoCairoCoreTextFont *) object;

  _pango_cairo_font_private_finalize (&cafont->cf_priv);

  G_OBJECT_CLASS (pango_cairo_core_text_font_parent_class)->finalize (object);
}

static void
pango_cairo_core_text_font_class_init (PangoCairoCoreTextFontClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  PangoFontClass *font_class = PANGO_FONT_CLASS (class);

  object_class->finalize = pango_cairo_core_text_font_finalize;

  font_class->get_glyph_extents = pango_cairo_core_text_font_get_glyph_extents;
  font_class->get_metrics = _pango_cairo_font_get_metrics;
  font_class->describe_absolute = pango_cairo_core_text_font_describe_absolute;
}

static void
pango_cairo_core_text_font_init (PangoCairoCoreTextFont *cafont G_GNUC_UNUSED)
{
}

PangoCoreTextFont *
_pango_cairo_core_text_font_new (PangoCairoCoreTextFontMap  *cafontmap,
                                 PangoContext               *context,
                                 PangoCoreTextFace          *face,
                                 const PangoFontDescription *desc)
{
  const char *postscript_name;
  PangoCairoCoreTextFont *cafont;
  PangoCoreTextFont *cfont;
  CFStringRef cfstr;
  CTFontRef font_ref;
  CGFontRef font_id;
  double size;
  double dpi;
  cairo_matrix_t font_matrix;

  postscript_name = _pango_core_text_face_get_postscript_name (face);

  /* Calculate size in points */
  size = (double) pango_font_description_get_size (desc) / PANGO_SCALE;

  if (context)
    {
      dpi = pango_cairo_context_get_resolution (context);

      if (dpi <= 0)
	dpi = cafontmap->dpi;
    }
  else
    dpi = cafontmap->dpi;

  if (pango_font_description_get_size_is_absolute (desc))
    size *= dpi / 72.;


  cfstr = CFStringCreateWithCString (NULL, postscript_name,
                                     kCFStringEncodingUTF8);
  font_ref = CTFontCreateWithName (cfstr, size, NULL);
  CFRelease (cfstr);

  /* FIXME: Do we need the synthetic italic handling here? */
  font_id = CTFontCopyGraphicsFont (font_ref, NULL);
  if (!font_id)
    return NULL;

  cafont = g_object_new (PANGO_TYPE_CAIRO_CORE_TEXT_FONT, NULL);
  cfont = PANGO_CORE_TEXT_FONT (cafont);

  _pango_core_text_font_set_font_description (cfont, desc);
  _pango_core_text_font_set_face (cfont, face);

  _pango_core_text_font_set_ctfont (cfont, font_ref);

  if (pango_font_description_get_size_is_absolute (desc))
    cafont->absolute_size = pango_font_description_get_size (desc);
  else
    cafont->absolute_size = pango_font_description_get_size (desc) * (dpi / 72.);

  cafont->size = size;

  cairo_matrix_init_identity (&font_matrix);
  cairo_matrix_scale (&font_matrix, size, size);

  _pango_cairo_font_private_initialize (&cafont->cf_priv,
					(PangoCairoFont *) cafont,
					pango_font_description_get_gravity (desc),
					_pango_cairo_context_get_merged_font_options (context),
					pango_context_get_matrix (context),
					&font_matrix);

  return cfont;
}