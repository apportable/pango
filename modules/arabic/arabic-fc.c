/* Pango
 * arabic-xft.h:
 *
 * Copyright (C) 2000 Red Hat Software
 * Author: Owen Taylor <otaylor@redhat.com>
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

#include <string.h>

#include "arabic-ot.h"

#include "pangoxft.h"
#include "pango-engine.h"
#include "pango-utils.h"

static PangoEngineRange arabic_ranges[] = {
  /* Language characters */
  { 0x060c, 0x06f9, "*" }, /* Arabic */
};

static PangoEngineInfo script_engines[] = {
  {
    "ArabicScriptEngineXft",
    PANGO_ENGINE_TYPE_SHAPE,
    PANGO_RENDER_TYPE_XFT,
    arabic_ranges, G_N_ELEMENTS(arabic_ranges)
  }
};

void
maybe_add_feature (PangoOTRuleset *ruleset,
		   PangoOTInfo    *info,
		   guint           script_index,
		   PangoOTTag      tag,
		   gulong          property_bit)
{
  guint feature_index;
  
  /* 0xffff == default language system */
  if (pango_ot_info_find_feature (info, PANGO_OT_TABLE_GSUB,
				  tag, script_index, 0xffff, &feature_index))
    pango_ot_ruleset_add_feature (ruleset, PANGO_OT_TABLE_GSUB, feature_index,
				  property_bit);
}

static PangoOTRuleset *
get_ruleset (PangoFont *font)
{
  PangoOTRuleset *ruleset;
  static GQuark ruleset_quark = 0;

  PangoOTInfo *info = pango_xft_font_get_ot_info (font);

  if (!ruleset_quark)
    ruleset_quark = g_quark_from_string ("pango-arabic-ruleset");
  
  if (!info)
    return NULL;

  ruleset = g_object_get_qdata (G_OBJECT (font), ruleset_quark);

  if (!ruleset)
    {
      PangoOTTag arab_tag = FT_MAKE_TAG ('a', 'r', 'a', 'b');
      guint script_index;

      ruleset = pango_ot_ruleset_new (info);

      if (pango_ot_info_find_script (info, PANGO_OT_TABLE_GSUB,
				     arab_tag, &script_index))
	{
	  maybe_add_feature (ruleset, info, script_index, FT_MAKE_TAG ('i','s','o','l'), isolated);
	  maybe_add_feature (ruleset, info, script_index, FT_MAKE_TAG ('i','n','i','t'), initial);
	  maybe_add_feature (ruleset, info, script_index, FT_MAKE_TAG ('m','e','d','i'), medial);
	  maybe_add_feature (ruleset, info, script_index, FT_MAKE_TAG ('f','i','n','a'), final);
	  maybe_add_feature (ruleset, info, script_index, FT_MAKE_TAG ('l','i','g','a'), 0xFFFF);
	}

      g_object_set_qdata_full (G_OBJECT (font), ruleset_quark, ruleset,
			       (GDestroyNotify)g_object_unref);
    }

  return ruleset;
}

static void
swap_range (PangoGlyphString *glyphs, int start, int end)
{
  int i, j;
  
  for (i = start, j = end - 1; i < j; i++, j--)
    {
      PangoGlyphInfo glyph_info;
      gint log_cluster;
      
      glyph_info = glyphs->glyphs[i];
      glyphs->glyphs[i] = glyphs->glyphs[j];
      glyphs->glyphs[j] = glyph_info;
      
      log_cluster = glyphs->log_clusters[i];
      glyphs->log_clusters[i] = glyphs->log_clusters[j];
      glyphs->log_clusters[j] = log_cluster;
    }
}

static void
set_glyph (PangoFont *font, PangoGlyphString *glyphs, int i, int offset, PangoGlyph glyph)
{
  glyphs->glyphs[i].glyph = glyph;
  glyphs->log_clusters[i] = offset;
}

static guint
find_char (FT_Face face, PangoFont *font, gunichar wc)
{
  int index = FT_Get_Char_Index (face, wc);

  if (index && index <= face->num_glyphs)
    return index;
  else
    return 0;
}

static void 
arabic_engine_shape (PangoFont        *font,
		    const char       *text,
		    gint              length,
		    PangoAnalysis    *analysis,
		    PangoGlyphString *glyphs)
{
  int n_chars;
  int i;
  const char *p;
  gulong *properties = NULL;
  gunichar *wcs = NULL;
  FT_Face face;
  PangoOTRuleset *ruleset;

  g_return_if_fail (font != NULL);
  g_return_if_fail (text != NULL);
  g_return_if_fail (length >= 0);
  g_return_if_fail (analysis != NULL);

  face = pango_xft_font_get_face (font);
  g_assert (face);

  n_chars = g_utf8_strlen (text, length);
  pango_glyph_string_set_size (glyphs, n_chars);

  ruleset = get_ruleset (font);
  if (ruleset)
    {
      wcs = g_utf8_to_ucs4 (text, length);
      properties = g_new0 (gulong, n_chars);
      
      Assign_Arabic_Properties (wcs, properties, n_chars);
    }
  
  p = text;
  for (i=0; i < n_chars; i++)
    {
      gunichar wc;
      gunichar mirrored_ch;
      PangoGlyph index;
      char buf[6];
      const char *input;

      wc = g_utf8_get_char (p);

      input = p;
      if (analysis->level % 2)
	if (pango_get_mirror_char (wc, &mirrored_ch))
	  {
	    wc = mirrored_ch;
	    
	    g_unichar_to_utf8 (wc, buf);
	    input = buf;
	  }

      if (wc >= 0x200B && wc <= 0x200F)	/* Zero-width characters */
	{
	  set_glyph (font, glyphs, i, p - text, 0);
	}
      else
	{
	  /* Hack - Microsoft fonts are strange and don't contain the
	   * correct rules to shape ARABIC LETTER FARSI YEH in
	   * medial/initial position. It looks identical to ARABIC LETTER
	   * YEH in these positions, so we substitute
	   */
	  if (wc == 0x6cc && ruleset &&
	      ((properties[i] & (initial | medial)) != (initial | medial)))
	    wc = 0x64a;
	  
	  index = find_char (face, font, wc);

	  if (!index)
	    {
	      set_glyph (font, glyphs, i, p - text,
			 pango_xft_font_get_unknown_glyph (font, wc));
	    }
	  else
	    {
	      set_glyph (font, glyphs, i, p - text, index);
	      
	      if (g_unichar_type (wc) == G_UNICODE_NON_SPACING_MARK)
		{
		  if (i > 0)
		    {
		      glyphs->log_clusters[i] = glyphs->log_clusters[i-1];
#if 0		      
		      PangoRectangle logical_rect, ink_rect;
		      
		      glyphs->glyphs[i].geometry.width = MAX (glyphs->glyphs[i-1].geometry.width,
							      glyphs->glyphs[i].geometry.width);
		      glyphs->glyphs[i-1].geometry.width = 0;
		      
		      /* Some heuristics to try to guess how overstrike glyphs are
		       * done and compensate
		       */
		      pango_font_get_glyph_extents (font, glyphs->glyphs[i].glyph, &ink_rect, &logical_rect);
		      if (logical_rect.width == 0 && ink_rect.x == 0)
			glyphs->glyphs[i].geometry.x_offset = (glyphs->glyphs[i].geometry.width - ink_rect.width) / 2;
#endif
		    }
		}
	    }
	}
      
      p = g_utf8_next_char (p);
    }

  ruleset = get_ruleset (font);

  if (ruleset)
    {
      pango_ot_ruleset_shape (ruleset, glyphs, properties);

      g_free (wcs);
      g_free (properties);

    }

  for (i = 0; i < glyphs->num_glyphs; i++)
    {

      if (glyphs->glyphs[i].glyph)
	{
	  PangoRectangle logical_rect;
	  
	  pango_font_get_glyph_extents (font, glyphs->glyphs[i].glyph, NULL, &logical_rect);
	  glyphs->glyphs[i].geometry.width = logical_rect.width;
	}
      else
	glyphs->glyphs[i].geometry.width = 0;
  
      glyphs->glyphs[i].geometry.x_offset = 0;
      glyphs->glyphs[i].geometry.y_offset = 0;
    }

  /* Simple bidi support */

  if (analysis->level % 2)
    {
      int start, end;

      /* Swap all glyphs */
      swap_range (glyphs, 0, glyphs->num_glyphs);
      
      /* Now reorder glyphs within each cluster back to LTR */
      for (start=0; start<glyphs->num_glyphs;)
	{
	  end = start;
	  while (end < glyphs->num_glyphs &&
		 glyphs->log_clusters[end] == glyphs->log_clusters[start])
	    end++;

	  if (end > start + 1)
	    swap_range (glyphs, start, end);
	  start = end;
	}
    }
}

static PangoCoverage *
arabic_engine_get_coverage (PangoFont  *font,
			   const char *lang)
{
  return pango_font_get_coverage (font, lang);
}

static PangoEngine *
arabic_engine_xft_new ()
{
  PangoEngineShape *result;
  
  result = g_new (PangoEngineShape, 1);

  result->engine.id = PANGO_RENDER_TYPE_XFT;
  result->engine.type = PANGO_ENGINE_TYPE_SHAPE;
  result->engine.length = sizeof (result);
  result->script_shape = arabic_engine_shape;
  result->get_coverage = arabic_engine_get_coverage;

  return (PangoEngine *)result;
}

/* The following three functions provide the public module API for
 * Pango. If we are compiling it is a module, then we name the
 * entry points script_engine_list, etc. But if we are compiling
 * it for inclusion directly in Pango, then we need them to
 * to have distinct names for this module, so we prepend
 * _pango_arabic_
 */
#ifdef MODULE_PREFIX
#define MODULE_ENTRY(func) _pango_arabic_##func
#else
#define MODULE_ENTRY(func) func
#endif

/* List the engines contained within this module
 */
void 
MODULE_ENTRY(script_engine_list) (PangoEngineInfo **engines, gint *n_engines)
{
  *engines = script_engines;
  *n_engines = G_N_ELEMENTS (script_engines);
}

/* Load a particular engine given the ID for the engine
 */
PangoEngine *
MODULE_ENTRY(script_engine_load) (const char *id)
{
  if (!strcmp (id, "ArabicScriptEngineXft"))
    return arabic_engine_xft_new ();
  else
    return NULL;
}

void 
MODULE_ENTRY(script_engine_unload) (PangoEngine *engine)
{
}
